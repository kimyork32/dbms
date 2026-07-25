cmake_minimum_required(VERSION 3.10)
project(megatron_db)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include_directories(include)

set(CORE_SOURCES
    src/parser/lexer.cpp
    src/parser/parser.cpp
    src/catalog/catalog.cpp
    src/storage/record/schema.cpp
    src/storage/record/tuple_builder.cpp
    src/storage/record/mvp_utils.cpp
    src/storage/page/slotted_page.cpp
    src/storage/page/io.cpp
    src/storage/page/buffer_pool_manager.cpp
    src/storage/index/b_plus_tree.cpp
    src/storage/index/static_hash.cpp
    src/storage/index/extendible_hash.cpp
    src/storage/index/linear_hash.cpp
    src/storage/engine/disk_storage_engine.cpp
    src/execution/executor.cpp
    src/execution/query_executor.cpp
    src/benchmarks/performance_tests.cpp
)
add_library(megatron_core STATIC ${CORE_SOURCES})

add_executable(megatron src/main.cpp)
target_link_libraries(megatron megatron_core)

add_executable(megatron_benchmark src/benchmarks/benchmark_main.cpp)
target_link_libraries(megatron_benchmark megatron_core)

add_executable(megatron_execution_test src/benchmarks/execution_test.cpp)
target_link_libraries(megatron_execution_test megatron_core)

add_executable(megatron_challenge_test src/benchmarks/challenge_test.cpp)
target_link_libraries(megatron_challenge_test megatron_core)

if(MSVC)
    target_compile_options(megatron PRIVATE /W4)
    target_compile_options(megatron_benchmark PRIVATE /W4)
    target_compile_options(megatron_execution_test PRIVATE /W4)
    target_compile_options(megatron_challenge_test PRIVATE /W4)
    target_compile_options(megatron_core PRIVATE /W4)
else()
    target_compile_options(megatron PRIVATE -Wall -Wextra)
    target_compile_options(megatron_benchmark PRIVATE -Wall -Wextra)
    target_compile_options(megatron_execution_test PRIVATE -Wall -Wextra)
    target_compile_options(megatron_challenge_test PRIVATE -Wall -Wextra)
    target_compile_options(megatron_core PRIVATE -Wall -Wextra)
endif()
