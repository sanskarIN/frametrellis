// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
//
// Remains idle until the parent delivers a cancel command to the stdin pipe.
// This catches cancellation requested before QProcess has emitted started.
#include <cstdio>
#include <cstring>

int main()
{
    char command[32]{};
    if (!std::fgets(command, sizeof(command), stdin)) {
        return 52;
    }
    return std::strncmp(command, "cancel", 6) == 0 ? 3 : 53;
}
