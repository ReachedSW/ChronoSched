cmake_minimum_required(VERSION 3.20)

project(ChronoSched VERSION 0.1.0 LANGUAGES CXX)

option(CHRONOSCHED_BUILD_TESTS "Build ChronoSched tests" ON)
option(CHRONOSCHED_BUILD_EXAMPLES "Build ChronoSched examples" ON)
option(CHRONOSCHED_BUILD_BENCHMARKS "Build ChronoSched benchmarks" OFF)
option(CHRONOSCHED_BUILD_LUA_EXAMPLE "Build the optional Lua integration example" OFF)
option(CHRONOSCHED_ENABLE_ASAN "Enable AddressSanitizer on supported compilers" OFF)
option(CHRONOSCHED_ENABLE_TSAN "Enable ThreadSanitizer on supported compilers" OFF)

add_library(chronosched src/scheduler.cpp src/task_handle.cpp)
add_library(ChronoSched::chronosched ALIAS chronosched)
target_compile_features(chronosched PUBLIC cxx_std_20)
target_include_directories(chronosched PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(chronosched PRIVATE -Wall -Wextra -Wpedantic)
elseif(MSVC)
    target_compile_options(chronosched PRIVATE /W4 /FS)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    if(CHRONOSCHED_ENABLE_ASAN)
        target_compile_options(chronosched PRIVATE -fsanitize=address -fno-omit-frame-pointer)
        # chronosched is static by default: consumers must link the sanitizer runtime too.
        target_link_options(chronosched INTERFACE -fsanitize=address)
    endif()
    if(CHRONOSCHED_ENABLE_TSAN)
        target_compile_options(chronosched PRIVATE -fsanitize=thread -fno-omit-frame-pointer)
        target_link_options(chronosched INTERFACE -fsanitize=thread)
    endif()
elseif(CHRONOSCHED_ENABLE_ASAN OR CHRONOSCHED_ENABLE_TSAN)
    message(FATAL_ERROR "ChronoSched sanitizer options require GCC or Clang")
endif()

if(CHRONOSCHED_BUILD_EXAMPLES)
    add_executable(chronosched_basic examples/basic.cpp)
    target_link_libraries(chronosched_basic PRIVATE ChronoSched::chronosched)

    add_executable(chronosched_cancellation examples/cancellation.cpp)
    target_link_libraries(chronosched_cancellation PRIVATE ChronoSched::chronosched)
endif()

if(CHRONOSCHED_BUILD_BENCHMARKS)
    add_executable(chronosched_benchmark benchmarks/scheduler_benchmark.cpp)
    target_link_libraries(chronosched_benchmark PRIVATE ChronoSched::chronosched)
endif()

if(CHRONOSCHED_BUILD_LUA_EXAMPLE)
    find_package(Lua REQUIRED)
    add_executable(chronosched_lua examples/lua/main.cpp)
    target_link_libraries(chronosched_lua PRIVATE ChronoSched::chronosched Lua::Lua)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
install(TARGETS chronosched EXPORT ChronoSchedTargets)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT ChronoSchedTargets NAMESPACE ChronoSched:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ChronoSched)
configure_package_config_file(cmake/ChronoSchedConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/ChronoSchedConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ChronoSched)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/ChronoSchedConfigVersion.cmake
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/ChronoSchedConfig.cmake ${CMAKE_CURRENT_BINARY_DIR}/ChronoSchedConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ChronoSched)

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

    add_executable(chronosched_tests tests/scheduler_tests.cpp tests/test_main.cpp)
    target_link_libraries(chronosched_tests PRIVATE ChronoSched::chronosched Catch2::Catch2)
    target_include_directories(chronosched_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(chronosched_tests PRIVATE -Wall -Wextra -Wpedantic)
    elseif(MSVC)
        target_compile_options(chronosched_tests PRIVATE /W4 /FS)
    endif()
    add_test(NAME chronosched_tests COMMAND chronosched_tests --no-pause)
endif()
