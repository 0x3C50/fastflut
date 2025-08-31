//
// Created by x150 on 17 Aug. 2025.
//

#include "epoll_shard.h"

#include <algorithm>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/epoll.h>
#include <sys/socket.h>

#include "global_state.h"
#include "parser.h"
#include "spdlog/spdlog.h"

#define DIE(reason) {perror(reason); exit(1);}

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
	// if (set_nonblock(fd) < 0)
	// 	DIE("set_nonblock()")

	return fd;
}

void handle_help(connection_t * cfdf) {
	// todo
}

void handle_size(connection_t * cfdf) {
	// todo
}

bool handle_px_cmd(shard_state_t* shart, connection_t * conn, px_buffer& buffer) {
	// fixme make fast
	size_t end_current_nibble, board_index;
	unsigned int x_coordinate, y_coordinate;
	uint32_t color;
	size_t readptr_before = buffer.index;
	// we are:
	// PX 123 456 aabbcc
	//    ^ here
	// parse x coordinate
	if (!buffer.find_where(' ', end_current_nibble)) goto incomplete_packet; // out of bounds searching for space

	{
		auto [_, ec] = std::from_chars(buffer.buffer+buffer.index,
										buffer.buffer+end_current_nibble, x_coordinate, 10);
		// invalid input
		if (ec == std::errc::invalid_argument || ec == std::errc::result_out_of_range) goto fall_over_and_die;
		// read the first coordinate
		if (end_current_nibble+1 >= buffer.length) goto incomplete_packet; // packet ends after space, we want more
		buffer.index = end_current_nibble+1; // consume space
	}

	// we are:
	// PX 123 456 aabbcc
	//        ^ here
	// parse y coordinate
	if (!buffer.find_where(' ', end_current_nibble)) goto incomplete_packet; // out of bounds searching for space

	{
		auto [_, ec] = std::from_chars(buffer.buffer+buffer.index,
										buffer.buffer+end_current_nibble, y_coordinate, 10);
		// invalid input
		if (ec == std::errc::invalid_argument || ec == std::errc::result_out_of_range) goto fall_over_and_die;
		// read the second coordinate
		if (end_current_nibble+1 >= buffer.length) goto incomplete_packet; // packet ends after space, we want more
		buffer.index = end_current_nibble+1; // consume space
	}

	{
		// at this point we can figure out the board index, if its out of bounds, we dont even need to bother with hex parsing
		board_index = x_coordinate + (y_coordinate * 512);
		if (board_index >= shart->f_global_state->canvas.size()) goto fall_over_and_die;
	}

	// we are:
	// PX 123 456 aabbcc
	//            ^ here
	// parse color
	if (!buffer.find_where('\n', end_current_nibble)) goto incomplete_packet; // out of bounds searching for end of packet

	{
		auto [_, ec] = std::from_chars(buffer.buffer+buffer.index,
										buffer.buffer+end_current_nibble, color, 16);
		// invalid input
		if (ec == std::errc::invalid_argument || ec == std::errc::result_out_of_range) goto fall_over_and_die;
		// read the color
		buffer.index = end_current_nibble + 1; // we're done
	}

	{
		// apply the pixel
		shart->f_global_state->canvas[board_index] = ntohl(color);
	}

	// SPDLOG_TRACE("PX {} {} {}", x_coordinate, y_coordinate, color);

	return true;


	fall_over_and_die:
	// parsing error, skip this packet and get on with it
	buffer.skip_ahead_to('\n');
	return true;

	incomplete_packet:
	// undo skip space and PX
	buffer.index = readptr_before - 3;
	return false;
}

size_t parse_from(shard_state_t* shard, connection_t *cfdf, px_buffer buffer) {
	size_t started_at = buffer.index;
	while (buffer.index < buffer.length) {
		size_t n_remaining = buffer.length - buffer.index;
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
			goto term;
		}
		switch (buffer.get_current_ptr_and_advance(1)[0]) {
			case 'H':
				// we're at the E and have at least 4 chars headroom, skip ahead 4 chars to consume ELP\n
				buffer.index += 4;
				handle_help(cfdf);
				break;
			case 'S':
				// we're at the 'I' and have at least 4 chars headroom, skip ahead 4 chars to consume IZE\n
				buffer.index += 4;
				handle_size(cfdf);
				break;
			case 'P':
				// we're at the P, >=4 chars, consume 'X ' to land at data
				buffer.index += 2;
				if (!handle_px_cmd(shard, cfdf, buffer)) goto term;
				break;
			default:
				SPDLOG_ERROR("UNKNOWN PACKET!!");
				// unknown packet - ignore by skipping forward to terminator \n
				buffer.skip_ahead_to('\n');
		}
	}
	term:
	return buffer.index - started_at;
}


void prepare_accept(io_uring* uring, int fd) {
	io_uring_sqe *sqe = io_uring_get_sqe(uring);
	if (sqe == nullptr) {
		SPDLOG_ERROR("!!! THE URING SQ IS FULL! WE ARE IN TOO DEEP, THINGS ARE ABOUT TO BREAK!");
		return;
	}
	io_uring_prep_accept(sqe, fd, nullptr, nullptr, SOCK_CLOEXEC);
	io_uring_sqe_set_data(sqe, nullptr); // tag 0 for "accept"
}

static void prepare_recv(shard_state_t* shart, connection_t* cfdf){
	// fixme multishot
	io_uring_sqe *sqe = io_uring_get_sqe(&shart->uring);
	if (sqe == nullptr) {
		SPDLOG_ERROR("!!! THE URING SQ IS FULL! WE ARE IN TOO DEEP, THINGS ARE ABOUT TO BREAK! recv({}) failed", cfdf->fd);
		return;
	}
	// recv with buffer selection — kernel chooses a provided buffer, cqe->flags tells which
	// SPDLOG_DEBUG("uring_read(read_buffer+{}, {})", cfdf->rb_write, cfdf->read_buffer.size()-cfdf->rb_write);
	io_uring_prep_recv_multishot(sqe, cfdf->mapped_fd_index, nullptr, 0, 0);
	sqe->flags |= IOSQE_BUFFER_SELECT | IOSQE_FIXED_FILE;
	sqe->buf_group = cfdf->fd;
	io_uring_sqe_set_data(sqe, cfdf); // tag with fd
}
void handle_io(shard_state_t * shard, connection_t * cfdf, const char * buffer_start, int res) {
	// check short path, do we have data in the buffer? if no, hell yeah!
	//    if yes, we need to do the long path
	// short:
	//   parse directly from buffer_start
	//   if data remains, copy that to cache
	// long:
	//   merge cache and buffer_start data, copy buffer_start into cache
	//   parse from cache
	//   remove parsed data
	if (cfdf->cache_len == 0) {
		// no data!
		size_t amount_read = parse_from(shard, cfdf, px_buffer{buffer_start, static_cast<size_t>(res), 0});
		if (amount_read < res) {
			// need to copy remaining data to cache
			memcpy(cfdf->data_cache.data(), buffer_start + amount_read, res - amount_read);
			cfdf->cache_len += res - amount_read;
		}
	} else {
		// we have cached data :(
		// need to copy buffer start
		// sanity check, can we copy this amount of data?
		if (cfdf->cache_len + res >= data_buffer_size) {
			SPDLOG_ERROR("would overflow buffer trying to copy data into cache. want at most {} bytes, got {}+{}. this should never happen, if the user sent us normal data!", data_buffer_size, cfdf->cache_len, res);
			return;
		}
		memcpy(cfdf->data_cache.data() + cfdf->cache_len, buffer_start, res);
		cfdf->cache_len += res;
		size_t amount_read = parse_from(shard, cfdf, px_buffer{cfdf->data_cache.data(), cfdf->cache_len, 0});
		if (amount_read < cfdf->cache_len) {
			// shift remaining data over
			size_t shift_back = amount_read;
			memmove(cfdf->data_cache.data(), cfdf->data_cache.data() + amount_read, cfdf->cache_len - shift_back);

			cfdf->cache_len = cfdf->cache_len - shift_back;
		} else {
			// we cleared it! yes!
			cfdf->cache_len = 0;
		}
	}
}

void handle_cqe(shard_state_t* shard, io_uring_cqe* cqe, int server_fd) {
	int res = cqe->res;
	void* tag = io_uring_cqe_get_data(cqe);
	uint64_t tag_b = io_uring_cqe_get_data64(cqe);

	if (tag_b == 0) {
		/* accept completed */
		if (res > 0) {
			int cfd = res;

			SPDLOG_TRACE("{}: new connection fd {}", shard->core_index, cfd);

			auto new_conn = std::make_unique<connection_t>();

			new_conn->fd = cfd;
			if (!shard->fd_register_reclaim_indices.empty()) {
				// reclaim existing index
				int the = shard->fd_register_reclaim_indices.back();
				SPDLOG_TRACE("OB {}: reusing existing fd index {}", cfd, the);
				new_conn->mapped_fd_index = the;
				shard->fd_register[the] = cfd;
				shard->fd_register_reclaim_indices.pop_back();
			}
			else {
				// make new one
				new_conn->mapped_fd_index = shard->fd_register.size();
				SPDLOG_TRACE("OB {}: making new fd index {}", cfd, new_conn->mapped_fd_index);
				shard->fd_register.push_back(cfd);
			}

			shard->sync_fd_register();

			new_conn->register_uring_buffers(&shard->uring);

			shard->connections.push_back(std::move(new_conn));
			connection_t* actual_value = shard->connections.back().get();

			SPDLOG_TRACE("{}: socket {}/{} onboarded and ready", shard->core_index, cfd, actual_value->mapped_fd_index);


			// Immediately arm first recv
			prepare_recv(shard, actual_value);
		}
		// Always resubmit accept so we keep listening
		prepare_accept(&shard->uring, server_fd);
	} else if (tag_b == 1) {
		if (cqe->res < 0) {
			SPDLOG_TRACE("administrative NOT ok: {}", cqe->res);
		} else {
			// SPDLOG_TRACE("administrative ok: {}", cqe->res);
		}
	} else {
		/* recv completed; tag is fd */
		// possible UAF if we removed this connection, fixme
		auto* client = static_cast<connection_t *>(tag);

		bool want_recv_again = (cqe->flags & IORING_CQE_F_MORE) == 0;
		// if this is the last one resubmit
		if (res > 0 && want_recv_again) {
			// we might have more data
			prepare_recv(shard, client);
			want_recv_again = false;
		}

		int bid = -1;

		if (cqe->flags & IORING_CQE_F_BUFFER) {
			bid = (cqe->flags >> 16); // liburing packs buffer ID in upper bits
		}

		if (res <= 0) {
			if (bid != -1) {
				// recycle buffer (at submit)
				io_uring_sqe* sqe = io_uring_get_sqe(&shard->uring);
				io_uring_prep_provide_buffers(sqe, client->uring_buffer_region.data() + buffer_size * bid, buffer_size, 1, client->fd, bid);
				io_uring_sqe_set_data64(sqe, 1);
			}


			if (res == -ENOBUFS) {
				// we have no more buffer space, but dont worry! we can retry later
				SPDLOG_WARN("read {}: ENOBUFS! we need more space!", client->fd);
				if (want_recv_again) {
					prepare_recv(shard, client);
					want_recv_again = false;
				}
				return;
			}
			// error
			SPDLOG_ERROR("failed to read from {}: {}/{}", client->fd, -res, strerror(-res));
			close(client->fd);

			io_uring_sqe* sqe = io_uring_get_sqe(&shard->uring);
			io_uring_prep_cancel(sqe, tag, IORING_ASYNC_CANCEL_ALL);
			io_uring_sqe_set_data64(sqe, 1);

			client->unregister_uring_buffers(&shard->uring);
			shard->fd_register[client->mapped_fd_index] = 0;
			shard->fd_register_reclaim_indices.push_back(client->mapped_fd_index);
			// no sync, we haven't changed the mapping

			// remove from vector
			erase_if(shard->connections, [&client](const std::unique_ptr<connection_t>& ptrref) {
				return ptrref.get() == client;
			});
			return;
		}

		if (bid == -1) {
			SPDLOG_ERROR("cqe came in without IORING_CQE_F_BUFFER flag?? don't know where to get the data from then");
			return;
		}

		// SPDLOG_TRACE("recv {} bytes in {}", res, bid);

		// copy buffer and parse
		void* buffer_start = client->uring_buffer_region.data() + buffer_size * static_cast<size_t>(bid);

		handle_io(shard, client, static_cast<char *>(buffer_start), res);


		// recycle buffer (at submit)
		io_uring_sqe *sqe = io_uring_get_sqe(&shard->uring);
		io_uring_prep_provide_buffers(sqe, buffer_start, buffer_size, 1, client->fd, bid);
		io_uring_sqe_set_data64(sqe, 1);

		return;
	}
}

shard_state_t::shard_state_t() {
	fd_register.reserve(128);
	connections.reserve(128);
}

void shard_state_t::sync_fd_register() {
	if (fd_register_initial_published) {
		int e = io_uring_unregister_files(&this->uring);
		if (e != 0) {SPDLOG_ERROR("unregister: {}", strerror(-e));}
	}
	int e = io_uring_register_files(&this->uring, this->fd_register.data(), this->fd_register.size());
	if (e != 0) {SPDLOG_ERROR("register: {}", strerror(-e));}
	fd_register_initial_published = true;
}

void* epoll_shard_main(void* t_arg) {
	auto* shard = static_cast<shard_state_t*>(t_arg);
	// set thread affinity
	{
		cpu_set_t cpu_set;
		CPU_ZERO(&cpu_set);
		CPU_SET(shard->core_index, &cpu_set);
		if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set), &cpu_set) != 0) {
			perror("pthread_setaffinity_np");
			return nullptr;
		}
	}

	SPDLOG_TRACE("core {} startup", shard->core_index);

	int lfd = make_listen_socket("0.0.0.0", 1234);
	// int server_fd_mapped = shard->fd_register.size();
	// shard->fd_register.push_back(lfd);

	SPDLOG_TRACE("core {} socket {}", shard->core_index, lfd);

	io_uring_params params{};
	params.flags |= IORING_SETUP_SINGLE_ISSUER;
	// params.flags |= IORING_SETUP_COOP_TASKRUN;
	params.flags |= IORING_SETUP_SQPOLL;
	params.sq_thread_cpu = shard->core_index+1;
	params.sq_thread_idle = 2000;

	int brh = io_uring_queue_init_params(4096, &shard->uring, &params);
	if (brh != 0) {
		SPDLOG_ERROR("failed iouring init: {}, {}", brh, strerror(-brh));
	}

	// set up buffer pool
	// io_uring_sqe *sqe = io_uring_get_sqe(&shard->uring);
	// shard->f_buffer_region = static_cast<char*>(aligned_alloc(64, n_buffers * buffer_size));
	// io_uring_prep_provide_buffers(sqe, shard->f_buffer_region, buffer_size, n_buffers, 0, 0);
	// io_uring_sqe_set_data64(sqe, 1);


	SPDLOG_TRACE("core {} start main", shard->core_index);
	prepare_accept(&shard->uring, lfd);
	io_uring_submit(&shard->uring);

	// spdlog::info("Core {} reporting for duty", shart_arg->core_index);
	// printf("i am on: %d\n", shart_arg->core_index);
	while (shard->f_global_state->run) {
		io_uring_cqe *cqe[64];
		unsigned int n = io_uring_peek_batch_cqe(&shard->uring, &cqe[0], 64);
		if (!n) {
			// block until at least one completion (timeout could be added)
			__kernel_timespec ks = {.tv_sec = 2, .tv_nsec = 0};
			int r = io_uring_wait_cqe_timeout(&shard->uring, &cqe[0], &ks);
			if (r<0) { if (r==-EINTR || r==-62 /*timeout expired*/) continue; SPDLOG_ERROR("io_uring_wait_cqe fail: {}/{}", -r, strerror(-r)); break; }
			n = 1;
		}
		for (unsigned int i = 0; i < n; i++) {
			handle_cqe(shard, cqe[i], lfd);
		}
		io_uring_cq_advance(&shard->uring, n);
		// SPDLOG_TRACE("io_uring_submit()");
		// Submit any queued SQEs in bulk
		io_uring_submit(&shard->uring);
	}

	io_uring_queue_exit(&shard->uring);

	SPDLOG_INFO("Core {} shutdown", shard->core_index);

	return nullptr;
}
