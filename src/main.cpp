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
#include <SFML/Window.hpp>

#include "../cmake-build-debug/_deps/sfml-src/include/SFML/Graphics/Sprite.hpp"
#include "../cmake-build-debug/_deps/sfml-src/include/SFML/Graphics/Texture.hpp"
#include "SFML/Graphics/RenderWindow.hpp"
// #include "../cmake-build-debug/_deps/sfml-src/include/SFML/Window/VideoMode.hpp"
// #include "../cmake-build-debug/_deps/sfml-src/include/SFML/Window/Window.hpp"

#define DIE(reason) {perror(reason); exit(1);}

static std::shared_ptr<global_state> global_state_ptr = std::make_shared<global_state>();

extern "C" uint32_t* get_gstate() {
	return global_state_ptr->canvas.data();
}

void render_loop() {
	sf::RenderWindow wd;
	wd.create(sf::VideoMode({512, 512}), "Fastflut");
	wd.setVerticalSyncEnabled(true);
	sf::Texture texture(sf::Vector2u(512, 512));
	sf::Sprite sprite(texture);
	while (wd.isOpen()) {
		if (!global_state_ptr->run) {
			wd.close();
			SPDLOG_INFO("stopped, closing window");
		}
		while (const std::optional ev = wd.pollEvent()) {
			if (ev->is<sf::Event::Closed>()) {
				global_state_ptr->run = false;
				wd.close();
				SPDLOG_INFO("window closed, stopping");
			}
			// if (const auto* resEvent = ev->getIf<sf::Event::Resized>()) {
			// 	sprite.setScale(sf::Vector2f(resEvent->size.x / 512.f, resEvent->size.y / 512.f));
			// }
		}
		texture.update(reinterpret_cast<uint8_t*>(global_state_ptr->canvas.data()));
		wd.draw(sprite);
		wd.display();
	}
}

int main() {
	spdlog::set_level(spdlog::level::trace);
	spdlog::info("Starting fastflut version " VERSION);
	int ncpu = get_nprocs_conf();
	if (ncpu <= 0) ncpu = 1;
	// int ncpu = 4;
	// if (ncpu > 2) ncpu -= 4;
	if (ncpu % 2 != 0) ncpu--;
	int count_worker = ncpu / 2;
	spdlog::info("Core distribution:");
	spdlog::info(" TOT: {}", ncpu);
	spdlog::info(" n uring: {}", ncpu / 2);
	spdlog::info(" n uring worker: {}", ncpu / 2);
	std::vector<pthread_t> thread_list(count_worker);
	std::vector<shard_state_t> shart_list(count_worker);

	__sighandler_t signal_handler = [](int) {
		global_state_ptr->run = false;
	};
	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);


	for (int i = 0; i < count_worker; i++) {
		shart_list[i].f_global_state = global_state_ptr;
		shart_list[i].core_index = i * 2;
		const int rc = pthread_create(&thread_list[i], nullptr, epoll_shard_main, &shart_list[i]);
		if (rc != 0) {
			perror("pthread_create");
			return 1;
		}
		pthread_setname_np(thread_list[i], std::to_string(i).c_str());
	}

	render_loop();

	for (const pthread_t& list : thread_list) {
		pthread_join(list, nullptr);
	}

	spdlog::info("all threads exited, shutting down");

	return 0;
}
