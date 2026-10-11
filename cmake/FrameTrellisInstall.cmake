# SPDX-License-Identifier: GPL-3.0-or-later
# Installs licensing documents next to the binaries so every package carries them.

include(GNUInstallDirs)

install(FILES
    "${PROJECT_SOURCE_DIR}/LICENSE"
    "${PROJECT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
    "${PROJECT_SOURCE_DIR}/README.md"
    DESTINATION "${CMAKE_INSTALL_DOCDIR}"
    OPTIONAL)
