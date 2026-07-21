cmake_minimum_required(VERSION 3.20)
project(tex-core-installed-consumer LANGUAGES C)

find_package(tex-core CONFIG REQUIRED)

add_executable(tex-core-installed-consumer ../c/main.c)
target_link_libraries(tex-core-installed-consumer PRIVATE tex-core::tex-core)
