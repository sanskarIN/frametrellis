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

# Regression: large timeline updates must not invalidate unchanged clip delegates.
ft_add_test(tst_timeline_model SOURCES tst_timeline_model.cpp LIBS ft_ui LABELS gui)
set_tests_properties(tst_timeline_model PROPERTIES TIMEOUT 60)

# A recovered unsaved edit must survive another crash before the user saves it.
ft_add_test(tst_recovery SOURCES tst_recovery.cpp LIBS ft_ui LABELS gui)
set_tests_properties(tst_recovery PROPERTIES TIMEOUT 60)

# Protect unsaved timeline edits before starting a potentially long export.
ft_add_test(tst_export_autosave SOURCES tst_export_autosave.cpp
    LIBS ft_ui ft_test_fixtures LABELS gui)
target_compile_definitions(tst_export_autosave PRIVATE
    FT_RENDER_HELPER="$<TARGET_FILE:frametrellis-render>")
add_dependencies(tst_export_autosave frametrellis-render)
set_tests_properties(tst_export_autosave PROPERTIES TIMEOUT 120)
