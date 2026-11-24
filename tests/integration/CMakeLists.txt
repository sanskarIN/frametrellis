# SPDX-License-Identifier: GPL-3.0-or-later
add_library(ft_test_fixtures STATIC ../support/media_fixtures.cpp ../support/media_fixtures.h)
target_include_directories(ft_test_fixtures PUBLIC ../support)
target_link_libraries(ft_test_fixtures PUBLIC ft_render_engine PRIVATE FrameTrellis::Mlt ft_compile_options)

ft_add_test(tst_media_importer SOURCES tst_media_importer.cpp LIBS ft_media LABELS integration)
ft_add_test(tst_engine SOURCES tst_engine.cpp LIBS ft_editing ft_media ft_test_fixtures FrameTrellis::Mlt LABELS integration)
ft_add_test(tst_render SOURCES tst_render.cpp LIBS ft_editing ft_render_engine ft_test_fixtures FrameTrellis::Mlt LABELS integration)
target_compile_definitions(tst_render PRIVATE FT_RENDER_HELPER="$<TARGET_FILE:frametrellis-render>")
add_dependencies(tst_render frametrellis-render)

# Prove that an unexpected helper exit is a failed job, not an editor shutdown,
# and that a retry is possible without corrupting the previous output.
add_executable(ft_render_fault_fixture ../support/render_fault_fixture.cpp)
target_link_libraries(ft_render_fault_fixture PRIVATE ft_compile_options)
target_compile_definitions(tst_render PRIVATE
    FT_FAKE_RENDER_HELPER="$<TARGET_FILE:ft_render_fault_fixture>")
add_dependencies(tst_render ft_render_fault_fixture)

add_executable(ft_render_stdout_fixture ../support/render_stdout_fixture.cpp)
target_link_libraries(ft_render_stdout_fixture PRIVATE ft_compile_options)
target_compile_definitions(tst_render PRIVATE
    FT_STDOUT_RENDER_HELPER="$<TARGET_FILE:ft_render_stdout_fixture>")
add_dependencies(tst_render ft_render_stdout_fixture)

# Even zero-exit helpers must report the exact expected destination and
# produce a nonempty MP4. Exercise the editor-side checks independently.
add_executable(ft_render_success_fixture ../support/render_success_fixture.cpp)
target_link_libraries(ft_render_success_fixture PRIVATE Qt6::Core ft_compile_options)
target_compile_definitions(tst_render PRIVATE
    FT_SUCCESS_RENDER_HELPER="$<TARGET_FILE:ft_render_success_fixture>")
add_dependencies(tst_render ft_render_success_fixture)

add_executable(ft_render_cancel_fixture ../support/render_cancel_fixture.cpp)
target_link_libraries(ft_render_cancel_fixture PRIVATE ft_compile_options)
target_compile_definitions(tst_render PRIVATE
    FT_CANCEL_RENDER_HELPER="$<TARGET_FILE:ft_render_cancel_fixture>")
add_dependencies(tst_render ft_render_cancel_fixture)
ft_add_test(tst_playback SOURCES tst_playback.cpp LIBS ft_playback ft_media ft_test_fixtures LABELS integration)
ft_add_test(tst_engine_threads SOURCES tst_engine_threads.cpp LIBS ft_test_fixtures FrameTrellis::Mlt LABELS integration)

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

# A factory with an incomplete MLT module directory must not leave a usable
# global runtime flag behind, even after a second initialization attempt.
ft_add_test(tst_engine_incomplete_runtime
    SOURCES tst_engine_incomplete_runtime.cpp
    LIBS ft_playback FrameTrellis::Mlt
    LABELS integration)
set_tests_properties(tst_engine_incomplete_runtime PROPERTIES
    ENVIRONMENT "FRAMETRELLIS_MLT_REPOSITORY=${FT_BROKEN_MLT_REPOSITORY};QT_LOGGING_RULES=ft.*.info=false;QT_FORCE_STDERR_LOGGING=1")
