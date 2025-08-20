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
	std::shared_ptr<global_state> f_global_state;
	std::vector<std::unique_ptr<connection_t>> connections{128};
	io_uring uring;
};

void* epoll_shard_main(void* shart);