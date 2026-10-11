# SPDX-License-Identifier: GPL-3.0-or-later
add_library(ft_engine STATIC
    frame_utils.cpp frame_utils.h
    mlt_profile.cpp mlt_profile.h
    mlt_runtime.cpp mlt_runtime.h
    still_producer.cpp still_producer.h
    timeline_graph.cpp timeline_graph.h
)
target_link_libraries(ft_engine
    PUBLIC ft_project Qt6::Gui
    PRIVATE FrameTrellis::Mlt ft_compile_options)
