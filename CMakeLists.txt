cmake_minimum_required(VERSION 3.10)

project(h_slcand LANGUAGES C)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra -Wpedantic -O2)

include(ExternalProject)

file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/libgpiod-build/include)

ExternalProject_Add(libgpiod-build
  GIT_REPOSITORY git://git.kernel.org/pub/scm/libs/libgpiod/libgpiod.git
  GIT_TAG v2.1
  # EXCLUDE_FROM_ALL TRUE
  PREFIX ${CMAKE_CURRENT_BINARY_DIR}/libgpiod-build
  CONFIGURE_COMMAND <SOURCE_DIR>/autogen.sh --prefix=<INSTALL_DIR> --enable-tools=no
  BUILD_COMMAND make -j ${N}
  INSTALL_COMMAND make install INSTALL_PREFIX=${CMAKE_CURRENT_BINARY_DIR}/libgpiod-build
  BUILD_IN_SOURCE 1
)

ExternalProject_Get_Property(libgpiod-build install_dir)
include_directories(${install_dir}/include)

add_executable(h_slcand h_slcand.c)
target_link_libraries(h_slcand ${CMAKE_CURRENT_BINARY_DIR}/libgpiod-build/lib/libgpiod.so)


install(TARGETS h_slcand DESTINATION /usr/local/sbin/)