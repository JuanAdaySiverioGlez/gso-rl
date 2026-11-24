cmake_minimum_required(VERSION 3.15)
project(my_problem LANGUAGES CXX)

# Where the gso-rl repository is. The default works while this folder is inside the repository.
# After copying it elsewhere:  cmake -S . -B build -DGSO_RL_DIR=/path/to/gso-rl
set(GSO_RL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." CACHE PATH "Path to the gso-rl repository")
add_subdirectory(${GSO_RL_DIR} gso-rl EXCLUDE_FROM_ALL)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_executable(my_problem main.cpp)
target_link_libraries(my_problem PRIVATE gso::gso)
