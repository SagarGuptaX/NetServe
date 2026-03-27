cmake_minimum_required(VERSION 3.16)
project(NetServe LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(-Wall -Wextra -Wpedantic)

add_executable(netserve
    src/main.cpp
    src/net/server.cpp
    src/concurrency/thread_pool.cpp
    src/http/http_parser.cpp
)

target_include_directories(netserve PRIVATE src)

# Place the binary at the project root so it sits beside cache_audit.
# This means ./netserve and ./cache_audit are always co-located,
# and ./cache_audit resolves correctly at runtime.
set_target_properties(netserve PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}
)

find_package(Threads REQUIRED)
target_link_libraries(netserve PRIVATE Threads::Threads)