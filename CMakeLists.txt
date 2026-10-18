# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors

cmake_minimum_required(VERSION 3.21)

project(FrameTrellis
    VERSION 1.2.1
    DESCRIPTION "A local-first, open-source desktop video editor for precise, multi-track editing."
    HOMEPAGE_URL "https://github.com/sanskarIN/frametrellis"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(FT_BUILD_APP "Build the Qt Quick desktop application" ON)
option(FT_BUILD_ENGINE "Build the MLT-based media engine and render helper" ON)
option(FT_BUILD_TESTS "Build the automated test suite" ON)
option(FT_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(FT_ENABLE_SANITIZERS "Build with AddressSanitizer and UndefinedBehaviorSanitizer (GCC/Clang)" OFF)
option(FT_WITH_SDL2 "Use SDL2 for audio playback when it is available" ON)

if(FT_BUILD_APP AND NOT FT_BUILD_ENGINE)
    message(FATAL_ERROR "FT_BUILD_APP requires FT_BUILD_ENGINE")
endif()

include(FrameTrellisCompilerOptions)
include(FrameTrellisWindowsResources)

set(FT_QT_COMPONENTS Core Gui Concurrent Test)
if(FT_BUILD_APP)
    list(APPEND FT_QT_COMPONENTS Qml Quick QuickControls2 Svg)
endif()
find_package(Qt6 6.5 REQUIRED COMPONENTS ${FT_QT_COMPONENTS})
qt_standard_project_setup(REQUIRES 6.5)

include(GNUInstallDirs)

if(FT_BUILD_ENGINE)
    include(FrameTrellisFindMlt)
    set(FT_HAVE_SDL2 OFF)
    if(FT_WITH_SDL2)
        find_package(SDL2 CONFIG QUIET)
        if(TARGET SDL2::SDL2)
            set(FT_HAVE_SDL2 ON)
            set(FT_SDL2_TARGET SDL2::SDL2)
        else()
            message(WARNING "SDL2 was not found: the preview will play without sound.")
        endif()
    endif()
    message(STATUS "FrameTrellis: audio playback via SDL2: ${FT_HAVE_SDL2}")
endif()

configure_file(src/core/version.h.in "${CMAKE_CURRENT_BINARY_DIR}/generated/core/version.h" @ONLY)

add_subdirectory(src)

if(FT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

include(FrameTrellisInstall)
