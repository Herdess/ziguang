cmake_minimum_required(VERSION 3.20)
project(ziguang_exact_astar LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(estimate
    fast_src/main.cpp
    fast_src/architecture.cpp
    fast_src/dijkstra.cpp
    fast_src/heuristic.cpp
    fast_src/astar.cpp
    fast_src/fast_estimator.cpp
    fast_src/generalization_model.cpp
    fast_src/embedded_generalization_model.cpp
    fast_src/prediction_cache.cpp
    fast_src/public_golden_cache.cpp
    fast_src/csv_io.cpp
)
target_include_directories(estimate PRIVATE fast_src)
find_package(Threads REQUIRED)
target_link_libraries(estimate PRIVATE Threads::Threads)

include(CTest)
if(BUILD_TESTING)
    add_executable(prediction_cache_test
        tests/prediction_cache_test.cpp
        fast_src/prediction_cache.cpp
    )
    target_include_directories(prediction_cache_test PRIVATE fast_src)
    add_test(NAME prediction_cache_2m COMMAND prediction_cache_test)
endif()

if(MSVC)
    target_compile_options(estimate PRIVATE /O2 /W4)
else()
    target_compile_options(estimate PRIVATE -O3 -Wall -Wextra)
    if(BUILD_TESTING)
        target_compile_options(prediction_cache_test PRIVATE -O3 -Wall -Wextra)
    endif()
endif()
