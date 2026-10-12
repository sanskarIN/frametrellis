# SPDX-License-Identifier: GPL-3.0-or-later
add_library(ft_test_fixtures STATIC ../support/media_fixtures.cpp ../support/media_fixtures.h)
target_include_directories(ft_test_fixtures PUBLIC ../support)
target_link_libraries(ft_test_fixtures PUBLIC ft_render_engine PRIVATE FrameTrellis::Mlt ft_compile_options)

ft_add_test(tst_engine SOURCES tst_engine.cpp LIBS ft_media ft_test_fixtures FrameTrellis::Mlt LABELS integration)
ft_add_test(tst_render SOURCES tst_render.cpp LIBS ft_render_engine ft_test_fixtures FrameTrellis::Mlt LABELS integration)
target_compile_definitions(tst_render PRIVATE FT_RENDER_HELPER="$<TARGET_FILE:frametrellis-render>")
add_dependencies(tst_render frametrellis-render)
ft_add_test(tst_playback SOURCES tst_playback.cpp LIBS ft_playback ft_media ft_test_fixtures LABELS integration)

# The installation self-test of the render helper, exactly as packages run it.
add_test(NAME render_self_test
    COMMAND frametrellis-render --self-test "${CMAKE_CURRENT_BINARY_DIR}/render-self-test")
set_tests_properties(render_self_test PROPERTIES
    LABELS integration
    FAIL_REGULAR_EXPRESSION "(^|\n)FAIL "
    ENVIRONMENT "QT_LOGGING_RULES=ft.*.info=false")

# A module that cannot be loaded must be reported with the loader's reason, so that a broken
# package explains itself.
set(FT_BROKEN_MLT_REPOSITORY "${CMAKE_CURRENT_BINARY_DIR}/broken-mlt-repository")
file(WRITE "${FT_BROKEN_MLT_REPOSITORY}/libmltbroken${CMAKE_SHARED_LIBRARY_SUFFIX}"
    "not a shared library\n")
add_test(NAME render_self_test_reports_unloadable_modules
    COMMAND frametrellis-render --self-test "${CMAKE_CURRENT_BINARY_DIR}/render-self-test-broken")
set_tests_properties(render_self_test_reports_unloadable_modules PROPERTIES
    LABELS integration
    PASS_REGULAR_EXPRESSION "FAIL engine: [^\n]*failed to dlopen [^\n]*libmltbroken"
    ENVIRONMENT "FRAMETRELLIS_MLT_REPOSITORY=${FT_BROKEN_MLT_REPOSITORY};QT_LOGGING_RULES=ft.*.info=false")
