//
// Created by x150 on 28 Aug. 2025.
//

#pragma once
#include <cstddef>


struct px_buffer {
	const char* buffer;
	const size_t length;
	size_t index;

	const char *get_current_ptr_and_advance(size_t len);
	void skip_ahead_to(char delim);
	bool find_where(char delim, size_t& out) const;
};