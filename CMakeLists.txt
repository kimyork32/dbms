cmake_minimum_required(VERSION 3.10)
project(megatron_db)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Incluir cabeceras
include_directories(include)

# Definir fuentes
set(SOURCES
    src/main.cpp
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

# Crear ejecutable
add_executable(megatron ${SOURCES})

# Opciones de compilación adicionales para mayor seguridad/calidad (opcional)
if(MSVC)
    target_compile_options(megatron PRIVATE /W4)
else()
    target_compile_options(megatron PRIVATE -Wall -Wextra)
endif()
