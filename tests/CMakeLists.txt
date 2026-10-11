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
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_LOGGING_RULES=ft.*.info=false")
endfunction()

add_subdirectory(unit)
