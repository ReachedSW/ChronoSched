cmake_minimum_required(VERSION 3.20)

project(ChronoSched VERSION 0.1.0 LANGUAGES CXX)

option(CHRONOSCHED_BUILD_TESTS "Build ChronoSched tests" ON)
option(CHRONOSCHED_BUILD_EXAMPLES "Build ChronoSched examples" ON)

add_library(chronosched src/scheduler.cpp src/task_handle.cpp)
add_library(ChronoSched::chronosched ALIAS chronosched)
target_compile_features(chronosched PUBLIC cxx_std_20)
target_include_directories(chronosched PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(chronosched PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(CHRONOSCHED_BUILD_EXAMPLES)
    add_executable(chronosched_basic examples/basic.cpp)
    target_link_libraries(chronosched_basic PRIVATE ChronoSched::chronosched)

    add_executable(chronosched_cancellation examples/cancellation.cpp)
    target_link_libraries(chronosched_cancellation PRIVATE ChronoSched::chronosched)
endif()

if(CHRONOSCHED_BUILD_TESTS)
    include(CTest)
    include(FetchContent)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.7.1
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(Catch2)

    add_executable(chronosched_tests tests/scheduler_tests.cpp)
    target_link_libraries(chronosched_tests PRIVATE ChronoSched::chronosched Catch2::Catch2WithMain)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(chronosched_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME chronosched_tests COMMAND chronosched_tests)
endif()
