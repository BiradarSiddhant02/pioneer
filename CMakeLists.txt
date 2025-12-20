cmake_minimum_required(VERSION 3.20)
project(pioneer VERSION 1.0.0 LANGUAGES C CXX)

# Prefer Clang compiler (warn if not using it)
if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang" OR NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(WARNING "This project is designed for Clang. You're using ${CMAKE_CXX_COMPILER_ID}.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Default to Release build
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler flags
add_compile_options(-Wall -Wextra -Wpedantic)

# Release optimizations and stripping
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-O3 -DNDEBUG)
    add_link_options(-s)  # Strip symbols
endif()

# FetchContent for dependencies - show progress
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# cxxopts - CLI parsing
FetchContent_Declare(
    cxxopts
    URL https://github.com/jarro2783/cxxopts/archive/refs/tags/v3.1.1.tar.gz
)

# nlohmann/json - JSON handling
FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/archive/refs/tags/v3.11.3.tar.gz
)

# tree-sitter core
FetchContent_Declare(
    tree_sitter
    URL https://github.com/tree-sitter/tree-sitter/archive/refs/tags/v0.22.6.tar.gz
)

# tree-sitter-python
FetchContent_Declare(
    tree_sitter_python
    URL https://github.com/tree-sitter/tree-sitter-python/archive/refs/tags/v0.21.0.tar.gz
)

# tree-sitter-c
FetchContent_Declare(
    tree_sitter_c
    URL https://github.com/tree-sitter/tree-sitter-c/archive/refs/tags/v0.21.4.tar.gz
)

# tree-sitter-cpp
FetchContent_Declare(
    tree_sitter_cpp
    URL https://github.com/tree-sitter/tree-sitter-cpp/archive/refs/tags/v0.22.2.tar.gz
)

# Make dependencies available
FetchContent_MakeAvailable(cxxopts json)

# Fetch tree-sitter but don't use their CMake (we build manually)
FetchContent_GetProperties(tree_sitter)
if(NOT tree_sitter_POPULATED)
    FetchContent_Populate(tree_sitter)
endif()

FetchContent_GetProperties(tree_sitter_python)
if(NOT tree_sitter_python_POPULATED)
    FetchContent_Populate(tree_sitter_python)
endif()

FetchContent_GetProperties(tree_sitter_c)
if(NOT tree_sitter_c_POPULATED)
    FetchContent_Populate(tree_sitter_c)
endif()

FetchContent_GetProperties(tree_sitter_cpp)
if(NOT tree_sitter_cpp_POPULATED)
    FetchContent_Populate(tree_sitter_cpp)
endif()

# Build tree-sitter as a static library
add_library(tree_sitter_lib STATIC
    ${tree_sitter_SOURCE_DIR}/lib/src/lib.c
)
target_include_directories(tree_sitter_lib PUBLIC
    ${tree_sitter_SOURCE_DIR}/lib/include
    ${tree_sitter_SOURCE_DIR}/lib/src
)
# Suppress warnings for third-party code
target_compile_options(tree_sitter_lib PRIVATE -w)

# Build tree-sitter-python parser
add_library(tree_sitter_python_lib STATIC
    ${tree_sitter_python_SOURCE_DIR}/src/parser.c
    ${tree_sitter_python_SOURCE_DIR}/src/scanner.c
)
target_include_directories(tree_sitter_python_lib PUBLIC
    ${tree_sitter_SOURCE_DIR}/lib/include
    ${tree_sitter_python_SOURCE_DIR}/src
)
target_compile_options(tree_sitter_python_lib PRIVATE -w)

# Build tree-sitter-c parser
add_library(tree_sitter_c_lib STATIC
    ${tree_sitter_c_SOURCE_DIR}/src/parser.c
)
target_include_directories(tree_sitter_c_lib PUBLIC
    ${tree_sitter_SOURCE_DIR}/lib/include
    ${tree_sitter_c_SOURCE_DIR}/src
)
target_compile_options(tree_sitter_c_lib PRIVATE -w)

# Build tree-sitter-cpp parser
add_library(tree_sitter_cpp_lib STATIC
    ${tree_sitter_cpp_SOURCE_DIR}/src/parser.c
    ${tree_sitter_cpp_SOURCE_DIR}/src/scanner.c
)
target_include_directories(tree_sitter_cpp_lib PUBLIC
    ${tree_sitter_SOURCE_DIR}/lib/include
    ${tree_sitter_cpp_SOURCE_DIR}/src
)
target_compile_options(tree_sitter_cpp_lib PRIVATE -w)

# Pioneer executable
add_executable(pioneer
    src/main.cpp
    src/commands.cpp
    src/indexer.cpp
    src/parser.cpp
    src/query.cpp
    src/graph.cpp
)

target_include_directories(pioneer PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${tree_sitter_SOURCE_DIR}/lib/include
)

target_link_libraries(pioneer PRIVATE
    cxxopts
    nlohmann_json::nlohmann_json
    tree_sitter_lib
    tree_sitter_python_lib
    tree_sitter_c_lib
    tree_sitter_cpp_lib
)

# Install
install(TARGETS pioneer RUNTIME DESTINATION bin)
