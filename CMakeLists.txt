cmake_minimum_required(VERSION 3.15)
project(gso_rl VERSION 0.1.0 LANGUAGES CXX)

# The library: header-only, C++17, no dependencies.
#   add_subdirectory(gso-rl)  then  target_link_libraries(my_target PRIVATE gso::gso)
# or simply add include/ to your include path.
add_library(gso INTERFACE)
add_library(gso::gso ALIAS gso)
target_include_directories(gso INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_compile_features(gso INTERFACE cxx_std_17)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(GSO_TOP_LEVEL ON)
else()
  set(GSO_TOP_LEVEL OFF)
endif()

option(GSO_BUILD_EXAMPLES "Build the examples" ${GSO_TOP_LEVEL})
option(GSO_BUILD_TESTS "Build the tests" ${GSO_TOP_LEVEL})

if(GSO_TOP_LEVEL AND NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(GSO_WARNINGS $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic> $<$<CXX_COMPILER_ID:MSVC>:/W4>)

if(GSO_BUILD_EXAMPLES)
  add_executable(knapsack examples/knapsack/main.cpp)
  target_link_libraries(knapsack PRIVATE gso::gso)
  target_compile_options(knapsack PRIVATE ${GSO_WARNINGS})
endif()

if(GSO_BUILD_TESTS)
  enable_testing()
  add_executable(gso_tests tests/test_core.cpp)
  target_link_libraries(gso_tests PRIVATE gso::gso)
  target_include_directories(gso_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/examples)
  target_compile_options(gso_tests PRIVATE ${GSO_WARNINGS})
  add_test(NAME gso_tests COMMAND gso_tests)
  add_executable(bandit_benchmark tests/bandit_benchmark.cpp)  # table of docs/04 (not a test)
  target_link_libraries(bandit_benchmark PRIVATE gso::gso)

  # The blank template must always compile, and name the missing part when run.
  add_executable(template_my_problem templates/my_problem/main.cpp)
  target_link_libraries(template_my_problem PRIVATE gso::gso)
  target_compile_options(template_my_problem PRIVATE ${GSO_WARNINGS})
  add_test(NAME template_help COMMAND template_my_problem --help)
  add_test(NAME template_todo COMMAND template_my_problem instance.txt --check)
  set_tests_properties(template_todo PROPERTIES PASS_REGULAR_EXPRESSION "TODO: write MyProblem::make_guide")

  if(GSO_BUILD_EXAMPLES)
    add_test(NAME knapsack_check COMMAND knapsack --check)
    add_test(NAME knapsack_run COMMAND knapsack --budget.seconds=0 --budget.iterations=20 --output.print_config=false)
  endif()
endif()
