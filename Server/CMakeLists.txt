cmake_minimum_required(VERSION 3.21)
project(Server C)
find_package(OpenMP REQUIRED)
set(CMAKE_C_STANDARD 99)

add_executable(Server main.c)
target_link_libraries(Server PRIVATE OpenMP::OpenMP_C)