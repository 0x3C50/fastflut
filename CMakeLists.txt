cmake_minimum_required(VERSION 4.0)
project(fastflut)

set(CMAKE_CXX_STANDARD 26)

add_subdirectory(lib/spdlog)

add_executable(fastflut src/main.cpp
        src/version.h
        src/unhex.cpp
        src/unhex.h
        src/epoll_shard.cpp
        src/epoll_shard.h
        src/connection.h
        src/parser.cpp
        src/parser.h)

target_link_libraries(fastflut PRIVATE spdlog::spdlog ${CMAKE_CURRENT_LIST_DIR}/lib/liburing/src/liburing.a)
target_include_directories(fastflut PRIVATE lib/liburing/src/include)
target_compile_definitions(fastflut PRIVATE VERSION="abcd" SPDLOG_ACTIVE_LEVEL=0)


add_executable(fastflut_bench bench/main.cpp)


set(BENCHMARK_ENABLE_TESTING OFF)
add_subdirectory(lib/benchmark)
target_link_libraries(fastflut_bench PRIVATE benchmark::benchmark)
#target_compile_options(fastflut_bench PRIVATE -Ofast)
#target_compile_options(fastflut PRIVATE -O0)