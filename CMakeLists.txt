cmake_minimum_required(VERSION 3.10)
project(megatron_db)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(include)

set(CORE_SOURCES
    src/parser/parser.cpp
    src/storage/Schema.cpp
    src/storage/SlottedPage.cpp
    src/storage/TupleBuilder.cpp
    src/storage/io.cpp
    src/storage/mvp_utils.cpp
    src/storage/disk_storage_engine.cpp
    src/storage/BufferPoolManager.cpp
    src/execution/QueryExecutor.cpp
    src/benchmarks/PerformanceTests.cpp
)
add_library(megatron_core STATIC ${CORE_SOURCES})

add_executable(megatron src/main.cpp)
target_link_libraries(megatron megatron_core)

add_executable(megatron_benchmark src/benchmarks/benchmark_main.cpp)
target_link_libraries(megatron_benchmark megatron_core)

if(MSVC)
    target_compile_options(megatron PRIVATE /W4)
    target_compile_options(megatron_benchmark PRIVATE /W4)
    target_compile_options(megatron_core PRIVATE /W4)
else()
    target_compile_options(megatron PRIVATE -Wall -Wextra)
    target_compile_options(megatron_benchmark PRIVATE -Wall -Wextra)
    target_compile_options(megatron_core PRIVATE -Wall -Wextra)
endif()
