# SPDX-License-Identifier: GPL-3.0-or-later
add_library(ft_media STATIC
    media_importer.cpp media_importer.h
    media_probe.cpp media_probe.h
    thumbnail_cache.cpp thumbnail_cache.h
    waveform.cpp waveform.h
)
target_link_libraries(ft_media
    PUBLIC ft_engine Qt6::Concurrent
    PRIVATE FrameTrellis::Mlt ft_compile_options)
