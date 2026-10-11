# SPDX-License-Identifier: GPL-3.0-or-later
# Shared warning, sanitizer and definition settings applied to every FrameTrellis target.

add_library(ft_compile_options INTERFACE)

if(MSVC)
    target_compile_options(ft_compile_options INTERFACE /W4 /permissive- /utf-8 /Zc:__cplusplus)
    if(FT_WARNINGS_AS_ERRORS)
        target_compile_options(ft_compile_options INTERFACE /WX)
    endif()
else()
    target_compile_options(ft_compile_options INTERFACE
        -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wcast-align
        -Woverloaded-virtual -Wnull-dereference -Wimplicit-fallthrough)
    if(FT_WARNINGS_AS_ERRORS)
        target_compile_options(ft_compile_options INTERFACE -Werror)
    endif()
    if(FT_ENABLE_SANITIZERS)
        target_compile_options(ft_compile_options INTERFACE
            -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
        target_link_options(ft_compile_options INTERFACE -fsanitize=address,undefined)
    endif()
endif()

target_compile_definitions(ft_compile_options INTERFACE
    QT_NO_CAST_FROM_ASCII
    QT_NO_URL_CAST_FROM_STRING
    QT_NO_NARROWING_CONVERSIONS_IN_CONNECT
    QT_NO_FOREACH
    QT_DISABLE_DEPRECATED_UP_TO=0x060500)

if(WIN32)
    # Keep <windows.h> from defining min/max macros and pulling in rarely used headers.
    target_compile_definitions(ft_compile_options INTERFACE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
