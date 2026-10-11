# SPDX-License-Identifier: GPL-3.0-or-later
add_subdirectory(core)
add_subdirectory(timeline)
add_subdirectory(project)
add_subdirectory(editing)

if(FT_BUILD_ENGINE)
    add_subdirectory(engine)
    add_subdirectory(media)
endif()
