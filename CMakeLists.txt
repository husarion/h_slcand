cmake_minimum_required(VERSION 3.10)

project(h_slcand LANGUAGES C)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra -Wpedantic -O2)

add_executable(h_slcand h_slcand.c)
install(TARGETS h_slcand DESTINATION /usr/local/sbin/)
