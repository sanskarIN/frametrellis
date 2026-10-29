# SPDX-License-Identifier: GPL-3.0-or-later
add_library(ft_editing STATIC
    change_set.h
    command_history.cpp command_history.h
    edit_command.cpp edit_command.h
    marker_edits.cpp marker_edits.h
    subtitle_edits.cpp subtitle_edits.h
    project_document.cpp project_document.h
    timeline_edits.cpp timeline_edits.h
)
target_link_libraries(ft_editing PUBLIC ft_project PRIVATE ft_compile_options)
