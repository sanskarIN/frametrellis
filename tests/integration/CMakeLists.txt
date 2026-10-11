# SPDX-License-Identifier: GPL-3.0-or-later
add_library(ft_test_fixtures STATIC ../support/media_fixtures.cpp ../support/media_fixtures.h)
target_include_directories(ft_test_fixtures PUBLIC ../support)
target_link_libraries(ft_test_fixtures PUBLIC ft_render_engine PRIVATE FrameTrellis::Mlt ft_compile_options)

ft_add_test(tst_engine SOURCES tst_engine.cpp LIBS ft_media ft_test_fixtures FrameTrellis::Mlt LABELS integration)
