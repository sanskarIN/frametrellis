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
    bool unexpectedCancel = false;
    bool errorAfterCancel = false;
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--output") == 0) {
            exitWithFailure = std::strstr(argv[i + 1], "fail-after-cancel") != nullptr;
            ignoreCancel = std::strstr(argv[i + 1], "hang-after-cancel") != nullptr;
            unexpectedCancel = std::strstr(argv[i + 1], "unexpected-cancel") != nullptr;
            errorAfterCancel = std::strstr(argv[i + 1], "error-after-cancel") != nullptr;
            break;
        }
    }
    if (unexpectedCancel) {
        // The editor never requested cancellation; exit code 3 must be a failure.
        std::puts(R"(@ft {"event":"started","encoder":"cancel-fixture","total":40})");
        std::fflush(stdout);
        return 3;
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
    if (errorAfterCancel) {
        // Report an actionable error, then use the nominal cancellation code.
        // The explicit error must remain visible to the editor.
        std::puts(R"(@ft {"event":"error","message":"Synthetic render failure before cancellation","detail":"FT_SIMULATED_CANCEL_ERROR"})");
        std::fflush(stdout);
        return 3;
    }
    if (ignoreCancel) {
        std::this_thread::sleep_for(std::chrono::seconds(8));
    }
    return 3;
}
