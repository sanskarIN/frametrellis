// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
//
// Intentionally failing child process for the RenderJob integration regression.
// This is not a real renderer or crash generator: it exits with a normal nonzero
// status after emitting a protocol event and noisy stderr, so CI stays safe.
#include <cstdio>
#include <string>

int main()
{
    std::fputs("{\"event\":\"started\",\"encoder\":\"failure-fixture\",\"total\":100}\n", stdout);
    std::fflush(stdout);

    const std::string noise(24'000, 'x');
    std::fwrite(noise.data(), 1, noise.size(), stderr);
    std::fputs("\nFT_SIMULATED_RENDER_FAILURE\n", stderr);
    std::fflush(stderr);
    return 42;
}
