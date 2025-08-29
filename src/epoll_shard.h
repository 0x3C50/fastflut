//
// Created by x150 on 17 Aug. 2025.
//

#pragma once
#include <memory>
#include <vector>

#include "connection.h"
#include "global_state.h"
#include "liburing.h"

struct shard_state_t {
	int core_index = 0;
	std::shared_ptr<global_state> f_global_state{};
	// char* f_buffer_region{};
	std::vector<int> fd_register{};
	// lifo for performance reasons. otherwise big moves. here we can just pop the end
	std::vector<int> fd_register_reclaim_indices{};
	bool fd_register_initial_published = false;
	std::vector<std::unique_ptr<connection_t>> connections{};
	io_uring uring{};

	shard_state_t();
	void sync_fd_register();
};

void* epoll_shard_main(void* t_arg);