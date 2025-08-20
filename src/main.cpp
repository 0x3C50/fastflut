#include <cmath>
#include <csignal>
#include <thread>
#include <sys/sysinfo.h>
#include <vector>

#include "version.h"
#include <algorithm>
#include <fcntl.h>
#include <sys/epoll.h>

#include "spdlog/spdlog.h"

#include <liburing.h>

#include "epoll_shard.h"
#include "global_state.h"

#define DIE(reason) {perror(reason); exit(1);}

#define MAX_EVENTS  4096
#define BACKLOG     32768
#define READ_BUFSZ  1<<20  // 1 MB scratch to demo draining reads

static std::shared_ptr<global_state> global_state_ptr = std::make_shared<global_state>();

int main() {
	spdlog::set_level(spdlog::level::trace);
	spdlog::info("Starting fastflut version " VERSION);
	int ncpu = get_nprocs_conf();
	if (ncpu <= 0) ncpu = 1;
	// int ncpu = 4;
	spdlog::info("Core distribution:");
	spdlog::info(" TOT: {}", ncpu);
	spdlog::info("   All cores assigned to socket IO");
	std::vector<pthread_t> thread_list(ncpu);
	std::vector<shard_state_t> shart_list(ncpu);

	__sighandler_t signal_handler = [](int) {
		global_state_ptr->run = false;
	};
	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);


	for (int i = 0; i < ncpu; i++) {
		shart_list[i].f_global_state = global_state_ptr;
		shart_list[i].core_index = i;
		const int rc = pthread_create(&thread_list[i], nullptr, epoll_shard_main, &shart_list[i]);
		if (rc != 0) {
			perror("pthread_create");
			return 1;
		}
		pthread_setname_np(thread_list[i], std::to_string(i).c_str());
	}

	for (const pthread_t& list : thread_list) {
		pthread_join(list, nullptr);
	}

	spdlog::info("all threads exited, shutting down");

	return 0;
}