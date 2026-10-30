# SPDX-License-Identifier: GPL-3.0-or-later
add_library(ft_project STATIC
    media_asset.cpp media_asset.h
    media_audit.cpp media_audit.h
    marker_chapters.cpp marker_chapters.h
    edl_export.cpp edl_export.h
    project.cpp project.h
    project_serializer.cpp project_serializer.h
    project_settings.cpp project_settings.h
    subtitles.cpp subtitles.h
)
target_link_libraries(ft_project PUBLIC ft_timeline PRIVATE ft_compile_options)
