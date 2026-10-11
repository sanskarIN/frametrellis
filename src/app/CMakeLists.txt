# SPDX-License-Identifier: GPL-3.0-or-later
set(FT_APP_RESOURCES)
if(WIN32)
    list(APPEND FT_APP_RESOURCES "${PROJECT_SOURCE_DIR}/resources/windows/frametrellis.rc")
endif()

qt_add_executable(frametrellis WIN32 MACOSX_BUNDLE main.cpp ${FT_APP_RESOURCES})
if(WIN32)
    set_property(SOURCE "${PROJECT_SOURCE_DIR}/resources/windows/frametrellis.rc" APPEND PROPERTY COMPILE_DEFINITIONS
        FT_VERSION_MAJOR=${PROJECT_VERSION_MAJOR} FT_VERSION_MINOR=${PROJECT_VERSION_MINOR}
        FT_VERSION_PATCH=${PROJECT_VERSION_PATCH} FT_VERSION_STRING="\"${PROJECT_VERSION}\"")
endif()
target_link_libraries(frametrellis PRIVATE ft_ui ft_uiplugin ft_compile_options)
set_target_properties(frametrellis PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin"
    MACOSX_BUNDLE_GUI_IDENTIFIER io.github.sanskarin.frametrellis
    MACOSX_BUNDLE_BUNDLE_NAME FrameTrellis
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
add_dependencies(frametrellis frametrellis-render)

install(TARGETS frametrellis
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    BUNDLE DESTINATION .)

if(UNIX AND NOT APPLE)
    install(FILES "${PROJECT_SOURCE_DIR}/resources/linux/frametrellis.desktop"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
    install(FILES "${PROJECT_SOURCE_DIR}/resources/linux/frametrellis.svg"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps")
endif()
