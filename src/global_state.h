//
// Created by x150 on 17 Aug. 2025.
//

#pragma once

#define CANVAS_WIDTH 512
#define CANVAS_HEIGHT 512
#include <array>
#include <cstdint>

struct global_state {
	bool run = true;
	std::array<uint32_t, CANVAS_WIDTH * CANVAS_HEIGHT> canvas{};
};