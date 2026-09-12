cmake_minimum_required(VERSION 3.20)
project(OrderBook CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Release flags: full optimisation + native CPU instructions
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG   "-O0 -g -fsanitize=address,undefined")

# ── Third-party deps ──────────────────────────────────────────────────────────
include(FetchContent)

FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/v1.14.0.zip
)
FetchContent_Declare(
  benchmark
  URL https://github.com/google/benchmark/archive/v1.8.3.zip
)

set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest benchmark)

# ── Core library ──────────────────────────────────────────────────────────────
add_library(orderbook_lib
    src/OrderBook.cpp
    src/MatchingEngine.cpp
)
target_include_directories(orderbook_lib PUBLIC include)

# ── Demo binary ───────────────────────────────────────────────────────────────
add_executable(main main.cpp)
target_link_libraries(main PRIVATE orderbook_lib)

# ── Unit tests ────────────────────────────────────────────────────────────────
enable_testing()
add_executable(tests tests/test_orderbook.cpp)
target_link_libraries(tests PRIVATE orderbook_lib GTest::gtest_main)
include(GoogleTest)
gtest_discover_tests(tests)

# ── Micro-benchmarks ─────────────────────────────────────────────────────────
add_executable(bench benchmarks/bench_orderbook.cpp)
target_link_libraries(bench PRIVATE orderbook_lib benchmark::benchmark benchmark::benchmark_main)

# ── Market simulator (Phase 7) ────────────────────────────────────────────────
add_executable(sim
    simulator/sim_main.cpp
    simulator/MarketSimulator.cpp
)
target_include_directories(sim PRIVATE simulator include)
target_link_libraries(sim PRIVATE orderbook_lib)
# std::filesystem needs explicit linking on GCC < 9
target_link_libraries(sim PRIVATE stdc++fs)
