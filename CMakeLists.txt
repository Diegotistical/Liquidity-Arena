cmake_minimum_required(VERSION 3.20)
project(LiquidityArena VERSION 2.0.0 LANGUAGES CXX)

# ── C++20 strict ──────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Compiler warnings (applied via target, not globally) ─────────────
# We use target_compile_options below so warnings only apply to our code,
# not to fetched dependencies (GoogleTest, Google Benchmark).

# ── Sanitizer option ─────────────────────────────────────────────────
option(ARENA_SANITIZERS "Enable AddressSanitizer + UndefinedBehaviorSanitizer" OFF)
if(ARENA_SANITIZERS AND NOT MSVC)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

# ── Platform-specific: Winsock for networking ────────────────────────
if(WIN32)
    set(PLATFORM_LIBS ws2_32)
endif()

# ── Fetch GoogleTest ─────────────────────────────────────────────────
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# ── Fetch Google Benchmark ───────────────────────────────────────────
FetchContent_Declare(
    googlebenchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG        v1.9.1
)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googlebenchmark)

# ── Cross-platform warning flags ─────────────────────────────────────
# Define a function to apply strict warnings + -Werror to our targets only.
function(arena_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX /permissive-)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Werror -O2)
    endif()
endfunction()

# ── Engine library (core matching engine, no networking) ─────────────
add_library(arena_core STATIC
    engine/src/order_book.cpp
    engine/src/matching_engine.cpp
)
target_include_directories(arena_core PUBLIC engine/include)
target_link_libraries(arena_core PUBLIC ${PLATFORM_LIBS})
arena_set_warnings(arena_core)
target_compile_definitions(arena_core PUBLIC
    ARENA_VERSION="${PROJECT_VERSION}"
)

# ── Engine executable (TCP server) ───────────────────────────────────
add_executable(arena_engine
    engine/main.cpp
    engine/src/tcp_server.cpp
)
target_link_libraries(arena_engine PRIVATE arena_core ${PLATFORM_LIBS})
arena_set_warnings(arena_engine)

# ── Test executable ──────────────────────────────────────────────────
enable_testing()
add_executable(arena_tests
    engine/tests/object_pool_test.cpp
    engine/tests/price_level_test.cpp
    engine/tests/order_book_test.cpp
    engine/tests/matching_engine_test.cpp
)
target_link_libraries(arena_tests PRIVATE arena_core GTest::gtest_main)
arena_set_warnings(arena_tests)
include(GoogleTest)
gtest_discover_tests(arena_tests)

# ── Benchmark executable ────────────────────────────────────────────
add_executable(arena_bench
    engine/bench/latency_bench.cpp
)
target_link_libraries(arena_bench PRIVATE arena_core benchmark::benchmark_main)
arena_set_warnings(arena_bench)
