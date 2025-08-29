//
// Created by x150 on 28 Aug. 2025.
//

#include "parser.h"

const char *px_buffer::get_current_ptr_and_advance(size_t len) {
	const char *the_addr = buffer + index;
	index += len;
	return the_addr;
}

void px_buffer::skip_ahead_to(char delim) {
	while (index < length && buffer[index] != delim) index++;
	// we're either out of room or at the delim
	if (index < length) index++; // skip over it if we have the space
}

bool px_buffer::find_where(char delim, size_t& out) const {
	size_t current_read = index;
	while (current_read < length && buffer[current_read] != delim) current_read++;
	// have we ran out of room? if yes return false
	if (current_read == length) return false;
	// otherwise we are at the point of the delim
	out = current_read;
	return true;
}