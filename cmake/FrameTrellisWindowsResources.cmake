# SPDX-License-Identifier: GPL-3.0-or-later
# Adds a Windows version resource (and optionally the application icon) to an executable.
#
#   ft_add_windows_resources(<target> DESCRIPTION <text> [ICON])
function(ft_add_windows_resources target)
    if(NOT WIN32)
        return()
    endif()
    cmake_parse_arguments(ARG "ICON" "DESCRIPTION" "" ${ARGN})
    set(FT_RC_NAME "${target}")
    set(FT_RC_DESCRIPTION "${ARG_DESCRIPTION}")
    set(FT_RC_ICON "")
    if(ARG_ICON)
        set(FT_RC_ICON "IDI_ICON1 ICON \"${PROJECT_SOURCE_DIR}/resources/windows/frametrellis.ico\"")
    endif()
    set(output "${CMAKE_CURRENT_BINARY_DIR}/${target}_version.rc")
    configure_file("${PROJECT_SOURCE_DIR}/resources/windows/version.rc.in" "${output}" @ONLY)
    target_sources(${target} PRIVATE "${output}")
endfunction()
