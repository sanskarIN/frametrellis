# SPDX-License-Identifier: GPL-3.0-or-later
# Locates the MLT 7 framework. Prefers MLT's own CMake package and falls back to pkg-config,
# which is what most Linux distributions and MSYS2 ship. Defines the FrameTrellis::Mlt target.

set(FT_MLT_MIN_VERSION 7.14)

find_package(Mlt7 ${FT_MLT_MIN_VERSION} CONFIG QUIET)
if(TARGET Mlt7::mlt++)
    add_library(FrameTrellis::Mlt INTERFACE IMPORTED)
    target_link_libraries(FrameTrellis::Mlt INTERFACE Mlt7::mlt++ Mlt7::mlt)
    set(FT_MLT_VERSION "${Mlt7_VERSION}")
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FT_MLTPP REQUIRED IMPORTED_TARGET mlt++-7>=${FT_MLT_MIN_VERSION})
    add_library(FrameTrellis::Mlt INTERFACE IMPORTED)
    target_link_libraries(FrameTrellis::Mlt INTERFACE PkgConfig::FT_MLTPP)
    set(FT_MLT_VERSION "${FT_MLTPP_VERSION}")
endif()

message(STATUS "FrameTrellis: using MLT ${FT_MLT_VERSION}")
