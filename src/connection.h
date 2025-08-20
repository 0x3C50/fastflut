//
// Created by x150 on 17 Aug. 2025.
//

#pragma once
#include <array>
#include <sys/socket.h>

struct connection_t {
	int fd{};
	// sockaddr_storage remote_addr{};
	// socklen_t ra_len{};
	std::array<char, 4096 * 4> read_buffer{};
	size_t rb_write=0;
	size_t rb_read=0;

	char* get_current_ptr_and_advance(size_t len);
	void skip_ahead_to(char delim);
	bool find_where(char delim, size_t& out) const;
};

inline char * connection_t::get_current_ptr_and_advance(size_t len) {
	char* the_addr = &read_buffer[rb_read];
	rb_read += len;
	return the_addr;
}

inline void connection_t::skip_ahead_to(char delim) {
	while (rb_read < rb_write && read_buffer[rb_read] != delim) rb_read++;
	// we're either out of room or at the delim
	if (rb_read < rb_write) rb_read++; // skip over it if we have the space
}

inline bool connection_t::find_where(char delim, size_t& out) const {
	size_t current_read = rb_read;
	while (current_read < rb_write && read_buffer[current_read] != delim) current_read++;
	// have we ran out of room? if yes return false
	if (current_read == rb_write) return false;
	// otherwise we are at the point of the delim
	out = current_read;
	return true;
}
