# SPDX-License-Identifier: GPL-3.0-or-later

# Export settings only need Qt Core, so they are testable without the media engine.
add_library(ft_rendering STATIC
    export_settings.cpp export_settings.h
    render_protocol.cpp render_protocol.h
)
target_link_libraries(ft_rendering PUBLIC ft_project PRIVATE ft_compile_options)
