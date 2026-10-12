# SPDX-License-Identifier: GPL-3.0-or-later

# ft_add_test(<name> SOURCES <files...> LIBS <targets...> [LABELS <labels...>])
function(ft_add_test name)
    cmake_parse_arguments(ARG "" "" "SOURCES;LIBS;LABELS" ${ARGN})
    add_executable(${name} ${ARG_SOURCES})
    target_link_libraries(${name} PRIVATE Qt6::Test ${ARG_LIBS} ft_compile_options)
    target_include_directories(${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/support")
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES
        LABELS "${ARG_LABELS}"
        # QT_FORCE_STDERR_LOGGING makes Qt Test write its results to the captured output on
        # Windows too, where it otherwise sends them to the debugger.
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_LOGGING_RULES=ft.*.info=false;SDL_AUDIODRIVER=dummy;QT_FORCE_STDERR_LOGGING=1")
endfunction()

add_subdirectory(unit)
if(FT_BUILD_ENGINE)
    add_subdirectory(integration)
endif()
if(FT_BUILD_APP)
    add_subdirectory(gui)
endif()
