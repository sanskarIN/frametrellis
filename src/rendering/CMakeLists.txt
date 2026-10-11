# SPDX-License-Identifier: GPL-3.0-or-later

# Export settings and the helper-process job controller only need Qt Core, so they are
# testable without the media engine.
add_library(ft_rendering STATIC
    export_settings.cpp export_settings.h
    render_job.cpp render_job.h
    render_protocol.cpp render_protocol.h
)
target_link_libraries(ft_rendering PUBLIC ft_project PRIVATE ft_compile_options)

if(FT_BUILD_ENGINE)
    add_library(ft_render_engine STATIC
        render_runner.cpp render_runner.h
        sample_media.cpp sample_media.h
        self_test.cpp self_test.h
    )
    target_link_libraries(ft_render_engine
        PUBLIC ft_rendering ft_media
        PRIVATE FrameTrellis::Mlt ft_compile_options)

    qt_add_executable(frametrellis-render render_main.cpp)
    ft_add_windows_resources(frametrellis-render DESCRIPTION "FrameTrellis render helper" ICON)
    target_link_libraries(frametrellis-render PRIVATE ft_render_engine ft_compile_options)
    set_target_properties(frametrellis-render PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
    install(TARGETS frametrellis-render RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()
