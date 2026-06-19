cmake_minimum_required(VERSION 3.12)
project(qoi_encoder LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)

# TinyExpr -- C source, compiled as C
add_library(tinyexpr_c tinyexpr.c)
target_include_directories(tinyexpr_c PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Main encoder
add_executable(qoi_encoder main.cpp)
target_include_directories(qoi_encoder PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(qoi_encoder PRIVATE tinyexpr_c)

# Optional: SymEngine FX support
find_package(SymEngine QUIET)
if(NOT SymEngine_FOUND)
    # Try pkg-config
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(SYMENGINE symengine QUIET)
    endif()
endif()

# Fallback: manually set paths if SymEngine installed to ~/.local
if(NOT SymEngine_FOUND AND NOT SYMENGINE_FOUND)
    set(SYMENGINE_INCLUDE_DIR "$ENV{HOME}/.local/include" CACHE PATH "")
    set(SYMENGINE_LIB_DIR "$ENV{HOME}/.local/lib" CACHE PATH "")
    set(GMP_INCLUDE_DIR "$ENV{CONDA_PREFIX}/include" CACHE PATH "")
    set(GMP_LIB_DIR "$ENV{CONDA_PREFIX}/lib" CACHE PATH "")
    if(EXISTS "${SYMENGINE_INCLUDE_DIR}/symengine/basic.h" AND
       EXISTS "${SYMENGINE_LIB_DIR}/libsymengine.a")
        set(SYMENGINE_FOUND TRUE)
        message(STATUS "SymEngine found at ${SYMENGINE_LIB_DIR}")
    endif()
endif()

if(SYMENGINE_FOUND OR SYMENGINE_FOUND)
    target_compile_definitions(qoi_encoder PRIVATE QOI_ENCODER_HAS_FX)
    target_include_directories(qoi_encoder PRIVATE ${SYMENGINE_INCLUDE_DIR} ${GMP_INCLUDE_DIR})
    target_link_directories(qoi_encoder PRIVATE ${SYMENGINE_LIB_DIR} ${GMP_LIB_DIR})
    target_link_libraries(qoi_encoder PRIVATE symengine gmp)
    message(STATUS "FX mode enabled")
else()
    message(STATUS "FX mode disabled (SymEngine not found)")
endif()
