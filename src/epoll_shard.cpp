//
// Created by x150 on 17 Aug. 2025.
//

#include "epoll_shard.h"

#include <algorithm>
#include <fcntl.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/epoll.h>
#include <sys/socket.h>

#include "global_state.h"
#include "spdlog/spdlog.h"

#define DIE(reason) {perror(reason); exit(1);}

static int set_nonblock(const int fd) {
	int fl = fcntl(fd, F_GETFL, 0);
	if (fl < 0) return -1;
	return fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

static int make_listen_socket(const char *bind_ip, uint16_t port) {
	int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, IPPROTO_TCP);
	if (fd < 0) DIE("socket()");

	int one = 1;
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) < 0)
		DIE("setsockopt(SO_REUSEADDR)")
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one)) < 0)
		DIE("setsockopt(SO_REUSEPORT)")

	sockaddr_in addr = {};
	addr.sin_family = AF_INET;
	addr.sin_port   = htons(port);
	addr.sin_addr.s_addr = bind_ip ? inet_addr(bind_ip) : htonl(INADDR_ANY);

	if (bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0)
		DIE("bind")
	if (listen(fd, 32768) < 0)
		DIE("listen")
	if (set_nonblock(fd) < 0)
		DIE("set_nonblock()")

	return fd;
}

void handle_help(connection_t * cfdf) {
	// todo
}

void handle_size(connection_t * cfdf) {
	// todo
}

void handle_px_cmd(shard_state_t* shart, connection_t * cfdf) {
	// fixme make fast
	size_t end_current_nibble, board_index;
	unsigned int x_coordinate, y_coordinate;
	uint32_t color;
	// we are:
	// PX 123 456 aabbcc
	//    ^ here
	// parse x coordinate
	if (!cfdf->find_where(' ', end_current_nibble)) goto fall_over_and_die; // out of bounds searching for space

	{
		auto [_, ec] = std::from_chars(&cfdf->read_buffer[cfdf->rb_read],
										&cfdf->read_buffer[end_current_nibble], x_coordinate, 10);
		// invalid input
		if (ec == std::errc::invalid_argument || ec == std::errc::result_out_of_range) goto fall_over_and_die;
		// read the first coordinate
		if (end_current_nibble+1 >= cfdf->rb_read) goto fall_over_and_die; // packet ends after space, we want more
		cfdf->rb_read = end_current_nibble+1; // consume space
	}

	// we are:
	// PX 123 456 aabbcc
	//        ^ here
	// parse y coordinate
	if (!cfdf->find_where(' ', end_current_nibble)) goto fall_over_and_die; // out of bounds searching for space

	{
		auto [_, ec] = std::from_chars(&cfdf->read_buffer[cfdf->rb_read],
										&cfdf->read_buffer[end_current_nibble], y_coordinate, 10);
		// invalid input
		if (ec == std::errc::invalid_argument || ec == std::errc::result_out_of_range) goto fall_over_and_die;
		// read the second coordinate
		if (end_current_nibble+1 >= cfdf->rb_read) goto fall_over_and_die; // packet ends after space, we want more
		cfdf->rb_read = end_current_nibble+1; // consume space
	}

	{
		// at this point we can figure out the board index, if its out of bounds, we dont even need to bother with hex parsing
		board_index = x_coordinate * y_coordinate;
		if (board_index >= shart->f_global_state->canvas.size()) goto fall_over_and_die;
	}

	// we are:
	// PX 123 456 aabbcc
	//            ^ here
	// parse color
	if (!cfdf->find_where('\n', end_current_nibble)) goto fall_over_and_die; // out of bounds searching for end of packet

	{
		auto [_, ec] = std::from_chars(&cfdf->read_buffer[cfdf->rb_read],
										&cfdf->read_buffer[end_current_nibble], color, 16);
		// invalid input
		if (ec == std::errc::invalid_argument || ec == std::errc::result_out_of_range) goto fall_over_and_die;
		// read the color
		cfdf->rb_read = end_current_nibble; // we're done
	}

	{
		// apply the pixel
		shart->f_global_state->canvas[board_index] = color;
	}

	return;


	fall_over_and_die:
	// parsing error, skip this packet and get on with it
	cfdf->skip_ahead_to('\n');
}

static void try_parse(shard_state_t* shart, connection_t * cfdf) {
	while (cfdf->rb_read < cfdf->rb_write) {
		size_t n_remaining = cfdf->rb_write - cfdf->rb_read;
		/*
		 Commands:
		 - HELP
		 - SIZE
		 - PX <x> <y>
		 - PX <x> <y> <rrggbb(aa)>
		 all terminated with \n
		 -> shortest command is 5 chars long ([HELP/SIZE]\n)
		 -> longest command is technically infinite, but in our case we limit the canvas to 65535x65535, so PX 65535 65535 RRGGBBAA\n -> 24 chars
		 */
		if (n_remaining < 5) {
			// not enough data to interpret a packet
			break;
		}
		switch (cfdf->get_current_ptr_and_advance(1)[0]) {
			case 'H':
				// we're at the E and have at least 4 chars headroom, skip ahead 4 chars to consume ELP\n
				cfdf->rb_read += 4;
				handle_help(cfdf);
				break;
			case 'S':
				// we're at the 'I' and have at least 4 chars headroom, skip ahead 4 chars to consume IZE\n
				cfdf->rb_read += 4;
				handle_size(cfdf);
				break;
			case 'P':
				// we're at the P, >=4 chars, consume 'X ' to land at data
				cfdf->rb_read += 2;
				handle_px_cmd(shart, cfdf);
				break;
			default:
				// unknown packet - ignore by skipping forward to terminator \n
				cfdf->skip_ahead_to('\n');

		}
	}
	// read all we could
}

static void handle_client_io(shard_state_t* shart, connection_t* cfdf) {
	const int cfd = cfdf->fd;

	for (;;) {
		// if we have read data, move the entire buffer to the left and offset everything back
		// fixme some sort of ring buffer?
		if (cfdf->rb_read != 0) {
			size_t shift_back = cfdf->rb_read;
			std::shift_left(cfdf->read_buffer.begin(), cfdf->read_buffer.end(), shift_back);
			cfdf->rb_read = 0;
			cfdf->rb_write = cfdf->rb_write - shift_back;
		}
		// we can at most read however many bytes we have left in storage
		ssize_t n = read(cfd, cfdf->read_buffer.data()+cfdf->rb_write, cfdf->read_buffer.size()-cfdf->rb_write);
		if (n > 0) {
			cfdf->rb_write += n;
			// alright, we read things! time to parse what we can
			try_parse(shart, cfdf);
			continue; // try read again (edge-triggered drain)
		} else if (n == 0) {
			// peer closed
			close(cfd);
			return;
		} else {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				return; // drained
			} else if (errno == EINTR) {
				continue;
			} else {
				perror("read");
				// error
				close(cfd);
				return;
			}
		}
	}
}

void prepare_accept(io_uring* uring, int fd) {
	io_uring_sqe *sqe = io_uring_get_sqe(uring);
	if (sqe == nullptr) {
		SPDLOG_ERROR("!!! THE URING SQ IS FULL! WE ARE IN TOO DEEP, THINGS ARE ABOUT TO BREAK!");
		return;
	}
	io_uring_prep_accept(sqe, fd, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
	io_uring_sqe_set_data(sqe, nullptr); // tag 0 for "accept"
}

static void prepare_recv(shard_state_t* shart, connection_t* cfdf){
	// fixme multishot
	struct io_uring_sqe *sqe = io_uring_get_sqe(&shart->uring);
	if (sqe == nullptr) {
		SPDLOG_ERROR("!!! THE URING SQ IS FULL! WE ARE IN TOO DEEP, THINGS ARE ABOUT TO BREAK! recv({}) failed", cfdf->fd);
		return;
	}
	// recv with buffer selection — kernel chooses a provided buffer, cqe->flags tells which
	// SPDLOG_DEBUG("uring_read(read_buffer+{}, {})", cfdf->rb_write, cfdf->read_buffer.size()-cfdf->rb_write);
	io_uring_prep_recv(sqe, cfdf->fd, cfdf->read_buffer.data()+cfdf->rb_write, cfdf->read_buffer.size()-cfdf->rb_write, 0);
	// sqe->flags |= IOSQE_BUFFER_SELECT;
	// sqe->buf_group = 0;
	io_uring_sqe_set_data(sqe, cfdf); // tag with fd
}

void handle_cqe(shard_state_t* shart, io_uring_cqe* cqe, int server_fd) {
	int res = cqe->res;
	void* tag = io_uring_cqe_get_data(cqe);

	if (tag == nullptr) {
		/* accept completed */
		if (res == 0) {
			puts("?=???");
			SPDLOG_ERROR("MYSTERY CQE: flags {}", cqe->flags);
			return;
		}
		if (res > 0) {
			int cfd = res;
			// minimal socket tuning
			constexpr int one=1; setsockopt(cfd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
			// create conn

			auto new_conn = std::make_unique<connection_t>();

			new_conn->fd = cfd;

			shart->connections.push_back(std::move(new_conn));
			connection_t* actual_value = shart->connections.back().get();

			SPDLOG_TRACE("core {} accept socket: {}", shart->core_index, cfd);


			// Immediately arm first recv
			prepare_recv(shart, actual_value);
		}
		// Always resubmit accept so we keep listening
		prepare_accept(&shart->uring, server_fd);
	} else {
		/* recv completed; tag is fd */
		auto* cfdf = static_cast<connection_t *>(tag);

		if (res < 0) {
			// error
			SPDLOG_ERROR("failed to read from {}: {}/{}", cfdf->fd, -res, strerror(-res));
			close(cfdf->fd);
			// remove from vector
			erase_if(shart->connections, [&cfdf](const std::unique_ptr<connection_t>& ptrref) {
				return ptrref.get() == cfdf;
			});
			return;
		}

		if (res == 0) {
			SPDLOG_ERROR("{} didnt read anything, closing", cfdf->fd);
			close(cfdf->fd);
			// remove from vector
			erase_if(shart->connections, [&cfdf](const std::unique_ptr<connection_t>& ptrref) {
				return ptrref.get() == cfdf;
			});
			return;
		}

		// SPDLOG_DEBUG("read() {} bytes", res);

		// possible UAF if we removed this connection, fixme

		// append to stash and parse

		cfdf->rb_write += res;
		// alright, we read things! time to parse what we can
		try_parse(shart, cfdf);

		// if we have read data, move the entire buffer to the left and offset everything back
		// fixme some sort of ring buffer?
		if (cfdf->rb_read != 0) {
			size_t shift_back = cfdf->rb_read;
			std::shift_left(cfdf->read_buffer.begin(), cfdf->read_buffer.end(), shift_back);
			cfdf->rb_read = 0;
			cfdf->rb_write = cfdf->rb_write - shift_back;
		}

		// re-arm next recv on this socket
		prepare_recv(shart, cfdf);
	}
}

void* epoll_shard_main(void* shart) {
	auto* shart_arg = static_cast<shard_state_t*>(shart);
	int mc = shart_arg->core_index;
	// set thread affinity
	{
		cpu_set_t cpu_set;
		CPU_ZERO(&cpu_set);
		CPU_SET(shart_arg->core_index, &cpu_set);
		if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set), &cpu_set) != 0) {
			perror("pthread_setaffinity_np");
			return nullptr;
		}
	}

	SPDLOG_TRACE("core {} startup", mc);

	int lfd = make_listen_socket("0.0.0.0", 1234);

	SPDLOG_TRACE("core {} socket {}", mc, lfd);

	// io_uring uring{};

	// accept + (read for each socket) = 1 + socket limit. i assume we wont go above 4095 sockets per thread, if we do, god help us
	io_uring_queue_init(4096, &shart_arg->uring, 0);

	// int ep = epoll_create1(EPOLL_CLOEXEC);
	// if (ep < 0) DIE("epoll_create1")

	// SPDLOG_TRACE("core {} epoll {}", mc, ep);

	// epoll_event ev = { .events = static_cast<uint32_t>(EPOLLIN | EPOLLET), .data = {.fd=lfd} };
	// if (epoll_ctl(ep, EPOLL_CTL_ADD, lfd, &ev) < 0)
	// 	DIE("epoll_ctl")

	// std::array<epoll_event, 4096> recv_events{};

	SPDLOG_TRACE("core {} start main", mc);
	prepare_accept(&shart_arg->uring, lfd);
	io_uring_submit(&shart_arg->uring);

	// spdlog::info("Core {} reporting for duty", shart_arg->core_index);
	// printf("i am on: %d\n", shart_arg->core_index);
	while (shart_arg->f_global_state->run) {
		io_uring_cqe *cqe[64];
		unsigned int n = io_uring_peek_batch_cqe(&shart_arg->uring, &cqe[0], 64);
		if (!n) {
			// block until at least one completion (timeout could be added)
			__kernel_timespec ks = {.tv_sec = 2, .tv_nsec = 0};
			int r = io_uring_wait_cqe_timeout(&shart_arg->uring, &cqe[0], &ks);
			if (r<0) { if (r==-EINTR || r==-62 /*timeout expired*/) continue; SPDLOG_ERROR("io_uring_wait_cqe fail: {}/{}", -r, strerror(-r)); break; }
			n = 1;
		}
		for (unsigned int i = 0; i < n; i++) {
			handle_cqe(shart_arg, cqe[i], lfd);
			io_uring_cqe_seen(&shart_arg->uring, cqe[i]);
		}
		// Submit any queued SQEs in bulk
		io_uring_submit(&shart_arg->uring);
		// const int n_ev = epoll_wait(ep, recv_events.data(), 4096, 2000);
		// if (n_ev < 0) {
		// 	// err
		// 	if (errno == EINTR) continue;
		// 	DIE("epoll_wait()")
		// }
		// for (int i = 0; i < n_ev; i++) {
		// 	int fd = recv_events[i].data.fd;
		// 	uint32_t e = recv_events[i].events;
		// 	// SPDLOG_TRACE("event: {}", e);
		//
		// 	if (fd == lfd) {
		// 		// fd to act on is our server fd; we have a new connection
		// 		// accept until we cant anymore
		// 		for (;;) {
		//
		// 		}
		// 	} else {
		// 		auto* the_connection_ptr = static_cast<connection_t *>(recv_events[i].data.ptr);
		// 		if (e & (EPOLLERR | EPOLLHUP)) {
		// 			// error or disconnect, nuke the socket
		// 			SPDLOG_TRACE("core {} socket close due to err or hangup: {}", mc, the_connection_ptr->fd);
		// 			close(the_connection_ptr->fd);
		// 			// remove from vector
		// 			erase_if(shart_arg->connections, [&the_connection_ptr](const std::unique_ptr<connection_t>& ptrref) {
		// 				return ptrref.get() == the_connection_ptr;
		// 			});
		// 			continue;
		// 		}
		// 		if (e & EPOLLIN) {
		// 			// SPDLOG_TRACE("core {} new socket data: {}", mc, the_connection_ptr->fd);
		// 			// new data!
		// 			handle_client_io(shart_arg, the_connection_ptr);
		// 		}
		// 		// Optionally EPOLLOUT handling if you send data back to clients
		// 	}
		// }
	}

	SPDLOG_INFO("Core {} shutdown", shart_arg->core_index);

	return nullptr;
}
