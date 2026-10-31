# A program that finds the installed monero_c with find_package(). Configure it with -DCMAKE_PREFIX_PATH=<install prefix>
cmake_minimum_required(VERSION 3.20)
project(monero_c_consumer C)

find_package(monero_c REQUIRED)

add_executable(consumer main.c)
target_link_libraries(consumer PRIVATE monero_c::monero_c)
