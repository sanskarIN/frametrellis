# SPDX-License-Identifier: GPL-3.0-or-later
add_library(ft_playback STATIC
    audio_output.cpp audio_output.h
    playback_engine.cpp playback_engine.h
)
target_link_libraries(ft_playback
    PUBLIC ft_engine
    PRIVATE FrameTrellis::Mlt ft_compile_options)
if(FT_HAVE_SDL2)
    target_link_libraries(ft_playback PRIVATE ${FT_SDL2_TARGET})
    target_compile_definitions(ft_playback PRIVATE FT_HAVE_SDL2)
endif()
