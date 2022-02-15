cmake_minimum_required(VERSION 3.21)
project(Client C)
set(THREADS_PREFER_PTHREAD_FLAG TRUE)
find_package(Threads REQUIRED)
set(CMAKE_C_STANDARD 99)

add_executable(Client main.c)
target_link_libraries(Client Threads::Threads)