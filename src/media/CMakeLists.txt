# SPDX-License-Identifier: GPL-3.0-or-later
add_library(ft_media STATIC
    media_probe.cpp media_probe.h
)
target_link_libraries(ft_media
    PUBLIC ft_engine
    PRIVATE FrameTrellis::Mlt ft_compile_options)
