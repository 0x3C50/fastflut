#include <iostream>
#include <benchmark/benchmark.h>
#include <experimental/simd>

static char* bruh = "ffaabbcc";
static unsigned long x;

static void BM_strtoul(benchmark::State& state) {
	for (auto _ : state) {
		x = std::strtoul(bruh, nullptr, 16);
	}
}

BENCHMARK(BM_strtoul);

static void BM_stol(benchmark::State& state) {
	for (auto _ : state) {
		x = std::stoul(bruh, nullptr, 16);
	}
}

BENCHMARK(BM_stol);

extern "C" long get_x() {
	return x;
}
extern "C" void set_input(char* y) {
	bruh = y;
}

static void BM_fc(benchmark::State& state) {
	for (auto v : state) {
		std::from_chars(bruh, bruh+8, x, 16);
	}
}

BENCHMARK(BM_fc);

static constexpr std::array<char, 256> hex_lut = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 0, 0, 0, 0, 0,
	0, 10,11,12,13,14,15,0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 10,11,12,13,14,15,0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};


template <std::size_t Ln>
uint32_t conv_me(const char* inp) {
	static_assert(Ln % 2 == 0);
	uint32_t out = 0;
	size_t shift = (Ln-1) * 4;
	for (int i = 0; i < Ln; i++) {
		const uint8_t v = hex_lut[static_cast<unsigned char>(inp[i])];
		out |= (v << shift);
		shift -= 4;
	}
	return out;
}

static void BM_mc_no_validation(benchmark::State& state) {
	for (auto _ : state) {
		x = conv_me<8>(bruh);
	}
}

BENCHMARK(BM_mc_no_validation);

BENCHMARK_MAIN();
// int main(int argc, char *argv[]) {
// 	std::cout << conv_me<8>(bruh) << "\n";
// }
