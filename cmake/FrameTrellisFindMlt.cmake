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

# MLT locates its modules relative to the executable on Windows, which fails when FrameTrellis
# runs from its build tree. Remember where this MLT keeps its modules and data as a fallback.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_get_variable(FT_MLT_MODULE_DIR mlt-framework-7 moduledir)
    pkg_get_variable(FT_MLT_DATA_DIR mlt-framework-7 mltdatadir)
endif()
if(FT_MLT_MODULE_DIR)
    message(STATUS "FrameTrellis: MLT modules at ${FT_MLT_MODULE_DIR}")
endif()
