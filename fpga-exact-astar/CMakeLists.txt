cmake_minimum_required(VERSION 3.20)
project(ziguang_exact_astar LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(exact_astar
    fast_src/main.cpp
    fast_src/architecture.cpp
    fast_src/dijkstra.cpp
    fast_src/heuristic.cpp
    fast_src/astar.cpp
    fast_src/csv_io.cpp
)
target_include_directories(exact_astar PRIVATE fast_src)

if(MSVC)
    target_compile_options(exact_astar PRIVATE /O2 /W4)
else()
    target_compile_options(exact_astar PRIVATE -O3 -Wall -Wextra)
endif()

