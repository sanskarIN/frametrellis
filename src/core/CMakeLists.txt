# SPDX-License-Identifier: GPL-3.0-or-later
add_library(ft_core STATIC
    ids.cpp ids.h
    logging.cpp logging.h
    result.h
)
target_include_directories(ft_core PUBLIC
    "${PROJECT_SOURCE_DIR}/src"
    "${PROJECT_BINARY_DIR}/generated")
target_link_libraries(ft_core PUBLIC Qt6::Core PRIVATE ft_compile_options)
