// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
//
// Intentionally failing child process for the RenderJob integration regression.
// It leaves a partial MP4 at the exact job-owned path, then exits with status 42.
// This does not intentionally cause a native process crash.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

int main(int argc, char** argv)
{
    std::filesystem::path output;
    std::string token;
    for (int i = 1; i + 1 < argc; ++i) {
        const std::string_view option(argv[i]);
        if (option == "--output") {
            output = argv[++i];
        } else if (option == "--render-token") {
            token = argv[++i];
        }
    }
    if (output.empty() || token.empty()) {
        return 3;
    }
    const std::filesystem::path part =
        output.parent_path() / ("." + output.stem().string() + "." + token + ".part.mp4");
    {
        std::ofstream file(part, std::ios::binary);
        if (!file) {
            return 4;
        }
        file << "unfinished synthetic output";
    }

    std::fputs("@ft {\"event\":\"started\",\"encoder\":\"failure-fixture\",\"total\":100}\n",
               stdout);
    std::fflush(stdout);

    // A library can flood stderr during a long export; exercise bounded draining
    // without triggering a native crash. The final marker must remain available.
    const std::string noise(2 * 1024 * 1024, 'x');
    std::fwrite(noise.data(), 1, noise.size(), stderr);
    std::fputs("\nFT_SIMULATED_RENDER_FAILURE\n", stderr);
    std::fflush(stderr);
    return 42;
}
