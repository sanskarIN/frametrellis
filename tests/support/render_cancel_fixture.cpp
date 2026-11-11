// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
//
// Remains idle until the parent delivers a cancel command to the stdin pipe.
// This catches cancellation requested before QProcess has emitted started.
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

// The test output filename selects special fault cases without environment
// variables or any real encoder/media. Ordinary cancels still exit with 3.
int main(int argc, char** argv)
{
    bool exitWithFailure = false;
    bool ignoreCancel = false;
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--output") == 0) {
            exitWithFailure = std::strstr(argv[i + 1], "fail-after-cancel") != nullptr;
            ignoreCancel = std::strstr(argv[i + 1], "hang-after-cancel") != nullptr;
            break;
        }
    }
    char command[32]{};
    if (!std::fgets(command, sizeof(command), stdin)) {
        return 52;
    }
    if (std::strncmp(command, "cancel", 6) != 0) {
        return 53;
    }
    if (exitWithFailure) {
        return 42; // Real helper error after a cancel request must not be masked.
    }
    if (ignoreCancel) {
        std::this_thread::sleep_for(std::chrono::seconds(8));
    }
    return 3;
}
