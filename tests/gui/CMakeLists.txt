# SPDX-License-Identifier: GPL-3.0-or-later
ft_add_test(tst_gui SOURCES tst_gui.cpp LIBS ft_ui ft_uiplugin ft_test_fixtures LABELS gui)
target_compile_definitions(tst_gui PRIVATE FT_RENDER_HELPER="$<TARGET_FILE:frametrellis-render>")
add_dependencies(tst_gui frametrellis-render)
set_tests_properties(tst_gui PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;SDL_AUDIODRIVER=dummy;QT_LOGGING_RULES=ft.*.info=false;QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 300)

ft_add_test(tst_subtitle_controller SOURCES tst_subtitle_controller.cpp
    LIBS ft_ui ft_uiplugin ft_test_fixtures LABELS gui)
set_tests_properties(tst_subtitle_controller PROPERTIES TIMEOUT 60)
