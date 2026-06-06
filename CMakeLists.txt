cmake_minimum_required(VERSION 3.18)
project(SZ3TestBuild VERSION 3.3.2)

# Test-oriented build entry for SZ3:
# - no install rules
# - outputs stay inside repository (test/bin, test/lib)

set(SZ3_DATA_VERSION 3.3.2)

option(BUILD_SHARED_LIBS "build shared libraries by default" ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /bigobj")
else ()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wreorder -Wsign-compare -Wsuggest-override -Wold-style-cast -Wcast-qual -Wextra-semi -Wnon-virtual-dtor -Wcast-align -Wunused-variable")
endif ()

set(default_build_type "Release")
if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "No build type was set. Setting build type to ${default_build_type}.")
    set(CMAKE_BUILD_TYPE ${default_build_type} CACHE
            STRING "Choose the type to build" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release"
            "MinSizeRel" "RelWithDebInfo")
endif ()

# Keep all outputs inside this repository for server-side testing.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/test/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/test/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/test/lib)

# Multi-config generators (VS/Xcode) need config-specific output dirs.
foreach(cfg Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER ${cfg} CFG_UPPER)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CFG_UPPER} ${CMAKE_SOURCE_DIR}/test/bin)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${CFG_UPPER} ${CMAKE_SOURCE_DIR}/test/lib)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${CFG_UPPER} ${CMAKE_SOURCE_DIR}/test/lib)
endforeach()

find_package(PkgConfig)

configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/include/SZ3/version.hpp.in
        ${CMAKE_CURRENT_BINARY_DIR}/include/SZ3/version.hpp)

add_library(SZ3 INTERFACE)
target_include_directories(
        SZ3 INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
)
target_compile_features(SZ3
        INTERFACE cxx_std_17
)

find_package(OpenMP)
if (OpenMP_FOUND)
    target_link_libraries(SZ3 INTERFACE OpenMP::OpenMP_CXX)
endif ()

option(SZ3_DEBUG_TIMINGS "print debug timing information" OFF)
if (SZ3_DEBUG_TIMINGS)
    target_compile_definitions(SZ3 INTERFACE SZ3_DEBUG_TIMINGS=1)
else ()
    target_compile_definitions(SZ3 INTERFACE SZ3_DEBUG_TIMINGS=0)
endif ()

option(SZ3_USE_SKA_HASH "use ska hashmap instead of std hashmap, default is ska for better speed" ON)
if (SZ3_USE_SKA_HASH)
    target_compile_definitions(SZ3 INTERFACE SZ3_USE_SKA_HASH=1)
else ()
    target_compile_definitions(SZ3 INTERFACE SZ3_USE_SKA_HASH=0)
endif ()

# Prefer bundled zstd for reproducible server builds.
set(SZ3_USE_BUNDLED_ZSTD ON CACHE BOOL "prefer the bundled version of Zstd" FORCE)
pkg_search_module(ZSTD IMPORTED_TARGET libzstd)
if (ZSTD_FOUND AND NOT SZ3_USE_BUNDLED_ZSTD)
    target_link_libraries(SZ3 INTERFACE PkgConfig::ZSTD)
else ()
    add_subdirectory(tools/zstd)
    target_link_libraries(SZ3 INTERFACE zstd)
endif ()

find_package(GSL)
if (GSL_FOUND)
    target_compile_definitions(SZ3 INTERFACE -DSZ3_ENABLE_GSL)
    target_link_libraries(SZ3 INTERFACE GSL::gsl)
endif ()

# Build only command-line tools for testing.
set(BUILD_SZ3_BINARY ON CACHE BOOL "build SZ3 tools" FORCE)
if (BUILD_SZ3_BINARY)
    add_subdirectory(tools/sz3)
    add_subdirectory(tools/sz3c)
endif ()

# Optional tools disabled in test-oriented build.
set(BUILD_TESTING OFF CACHE BOOL "Build tests" FORCE)
set(BUILD_H5Z_FILTER OFF CACHE BOOL "build the H5Z-SZ3 filter" FORCE)
set(BUILD_MDZ OFF CACHE BOOL "build MDZ (MMD-SZ)" FORCE)
set(BUILD_PARAVIEW_PLUGIN OFF CACHE BOOL "build the ParaView plugin" FORCE)
