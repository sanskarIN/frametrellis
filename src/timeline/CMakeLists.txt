# SPDX-License-Identifier: GPL-3.0-or-later
add_library(ft_timeline STATIC
    clip.h
    timeline.cpp timeline.h
    track.cpp track.h
)
target_link_libraries(ft_timeline PUBLIC ft_core PRIVATE ft_compile_options)
