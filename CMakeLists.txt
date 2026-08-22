cmake_minimum_required(VERSION 3.16)
project(lob-matching-engine
    VERSION   0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ──────────────────────────────────────────────
# Library: the matching engine core
# ──────────────────────────────────────────────
add_library(lob
    src/book.cpp
)
target_include_directories(lob PUBLIC include)

# ──────────────────────────────────────────────
# Benchmark executable
# ──────────────────────────────────────────────
add_executable(bench_submit bench/bench_submit.cpp)
target_link_libraries(bench_submit PRIVATE lob)

# ──────────────────────────────────────────────
# CLI tool: read CSV orders, print trade log
# ──────────────────────────────────────────────
add_executable(lob_cli tools/lob_cli.cpp)
target_link_libraries(lob_cli PRIVATE lob)

# ──────────────────────────────────────────────
# Tests (using Catch2)
# ──────────────────────────────────────────────
option(LOB_BUILD_TESTS "Build unit tests" ON)

if(LOB_BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.7.1
    )
    FetchContent_MakeAvailable(Catch2)

    add_executable(tests
        tests/test_matching.cpp
    )
    target_link_libraries(tests PRIVATE lob Catch2::Catch2WithMain)

    include(CTest)
    include(Catch)
    catch_discover_tests(tests)
endif()
