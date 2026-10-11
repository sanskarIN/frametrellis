# SPDX-License-Identifier: GPL-3.0-or-later

# The user interface: C++ view models and items plus the QML files, built as the static QML
# module "FrameTrellis" that the application (and the GUI tests) import.
qt_add_library(ft_ui STATIC)
qt_policy(SET QTP0004 NEW)

set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
file(GLOB FT_UI_ICONS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" CONFIGURE_DEPENDS icons/*.svg)

qt_add_qml_module(ft_ui
    URI FrameTrellis
    VERSION 1.0
    OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/qml/FrameTrellis"
    QML_FILES
        qml/Main.qml
        qml/Theme.qml
        qml/controls/DialogButtons.qml
        qml/controls/EmptyState.qml
        qml/controls/FtButton.qml
        qml/controls/FtProgressBar.qml
        qml/controls/FtSlider.qml
        qml/controls/IconButton.qml
        qml/controls/PanelFrame.qml
        qml/controls/ValueSlider.qml
        qml/dialogs/AboutDialog.qml
        qml/dialogs/ConfirmDialog.qml
        qml/dialogs/ExportDialog.qml
        qml/dialogs/FtDialog.qml
        qml/dialogs/MessageBox.qml
        qml/dialogs/NewProjectDialog.qml
        qml/dialogs/ShortcutsDialog.qml
        qml/dialogs/WelcomeOverlay.qml
        qml/panels/ClipItem.qml
        qml/panels/InspectorPanel.qml
        qml/panels/MediaBinPanel.qml
        qml/panels/MonitorPanel.qml
        qml/panels/TimeRuler.qml
        qml/panels/TimelinePanel.qml
        qml/panels/TrackHeader.qml
    SOURCES
        app_controller.cpp app_controller.h
        export_controller.cpp export_controller.h
        frame_view.cpp frame_view.h
        log_file.cpp log_file.h
        media_bin_model.cpp media_bin_model.h
        media_controller.cpp media_controller.h
        playback_controller.cpp playback_controller.h
        project_controller.cpp project_controller.h
        thumbnail_provider.cpp thumbnail_provider.h
        timeline_controller.cpp timeline_controller.h
        timeline_model.cpp timeline_model.h
    RESOURCES
        ${FT_UI_ICONS}
)

target_link_libraries(ft_ui
    PUBLIC Qt6::Quick Qt6::QuickControls2 ft_editing ft_media ft_playback ft_rendering
    PRIVATE ft_compile_options)
