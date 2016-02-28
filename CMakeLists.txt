cmake_minimum_required(VERSION 2.8)
project (server)
add_executable(server server.c)
target_link_libraries(server ev)
