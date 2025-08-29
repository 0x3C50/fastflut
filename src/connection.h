//
// Created by x150 on 17 Aug. 2025.
//

#pragma once
#include <array>
#include <sys/socket.h>

#include "liburing.h"

constexpr size_t n_buffers = 2048;
constexpr size_t buffer_size = 512;
constexpr size_t data_buffer_size = 4096;

struct connection_t {
	int fd{};
	int mapped_fd_index{};
	// sockaddr_storage remote_addr{};
	// socklen_t ra_len{};
	std::vector<char> uring_buffer_region{};
	std::vector<char> data_cache{};
	size_t cache_len{};
	// size_t rb_read=0;

	connection_t();
	~connection_t() = default;

	void register_uring_buffers(io_uring *uring);
	void unregister_uring_buffers(io_uring *uring) const;
};

inline connection_t::connection_t() {
	data_cache.resize(data_buffer_size);
	uring_buffer_region.resize(n_buffers * buffer_size);
	//
	// for (char* &uring_buffer : uring_buffers) {
	// 	uring_buffer = new char[512];
	// }
}

inline void connection_t::register_uring_buffers(io_uring *uring) {
	io_uring_sqe *sqe = io_uring_get_sqe(uring);
	io_uring_prep_provide_buffers(sqe, this->uring_buffer_region.data(), buffer_size, n_buffers, this->fd, 0);
	io_uring_sqe_set_data64(sqe, 1);
}

inline void connection_t::unregister_uring_buffers(io_uring *uring) const {
	io_uring_sqe *sqe = io_uring_get_sqe(uring);
	io_uring_prep_remove_buffers(sqe, n_buffers, this->fd);
	io_uring_sqe_set_data64(sqe, 1);
}
