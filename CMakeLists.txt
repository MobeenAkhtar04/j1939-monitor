cmake_minimum_required(VERSION 3.16)
project(j1939_monitor VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
add_compile_options(-Wall -Wextra -Wpedantic)

# ---- core library: everything testable without hardware -------------------------
add_library(j1939core
  src/id.cpp
  src/decoder.cpp
  src/transport.cpp
  src/fault_state_machine.cpp
  src/monitor.cpp
  src/sinks.cpp
  src/io.cpp)
target_include_directories(j1939core PUBLIC include)

add_executable(j1939_monitor apps/monitor/main.cpp)
target_link_libraries(j1939_monitor PRIVATE j1939core)

add_executable(j1939_sim apps/sim/main.cpp)
target_link_libraries(j1939_sim PRIVATE j1939core)

# ---- Qt dashboard (optional: skipped if Qt 6 is not installed) -------------------
find_package(Qt6 QUIET COMPONENTS Widgets Network)
if(Qt6_FOUND)
  set(CMAKE_AUTOMOC ON)
  add_executable(j1939_dashboard
    apps/dashboard/main.cpp
    apps/dashboard/gauge.cpp
    apps/dashboard/gauge.hpp
    apps/dashboard/dashboard.cpp
    apps/dashboard/dashboard.hpp)
  target_link_libraries(j1939_dashboard PRIVATE Qt6::Widgets Qt6::Network)
else()
  message(STATUS "Qt6 not found - skipping j1939_dashboard")
endif()

# ---- tests ----------------------------------------------------------------------
option(BUILD_TESTING "Build unit tests" ON)
if(BUILD_TESTING)
  enable_testing()
  find_package(GTest QUIET)
  if(NOT GTest_FOUND)
    include(FetchContent)
    FetchContent_Declare(googletest
      URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip)
    FetchContent_MakeAvailable(googletest)
  endif()
  add_executable(j1939_tests
    tests/test_id.cpp
    tests/test_decoder.cpp
    tests/test_transport.cpp
    tests/test_state_machine.cpp
    tests/test_traces.cpp)
  target_link_libraries(j1939_tests PRIVATE j1939core GTest::gtest_main)
  target_compile_definitions(j1939_tests PRIVATE TRACE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
  include(GoogleTest)
  gtest_discover_tests(j1939_tests)
endif()
