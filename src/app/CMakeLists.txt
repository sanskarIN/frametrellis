# SPDX-License-Identifier: GPL-3.0-or-later
qt_add_executable(frametrellis WIN32 MACOSX_BUNDLE main.cpp)
ft_add_windows_resources(frametrellis DESCRIPTION "FrameTrellis video editor" ICON)
target_link_libraries(frametrellis PRIVATE ft_ui ft_uiplugin ft_compile_options)
set_target_properties(frametrellis PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin"
    MACOSX_BUNDLE_GUI_IDENTIFIER io.github.sanskarin.frametrellis
    MACOSX_BUNDLE_BUNDLE_NAME FrameTrellis
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
add_dependencies(frametrellis frametrellis-render)

if(APPLE)
    # The GUI runs inside frametrellis.app/Contents/MacOS, not the build's bin
    # directory. Put its render-helper beside it so Finder-launched exports
    # work in both build-tree and installed .app bundles.
    add_custom_command(TARGET frametrellis POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:frametrellis-render>"
            "$<TARGET_FILE_DIR:frametrellis>/frametrellis-render"
        COMMENT "Copy render helper into macOS application bundle"
        VERBATIM)
endif()

install(TARGETS frametrellis
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    BUNDLE DESTINATION .)

if(UNIX AND NOT APPLE)
    install(FILES "${PROJECT_SOURCE_DIR}/resources/linux/frametrellis.desktop"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
    install(FILES "${PROJECT_SOURCE_DIR}/resources/linux/frametrellis.svg"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps")
endif()
