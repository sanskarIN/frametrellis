// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
//
// Simulate a child/native library flooding stdout with an unframed line, then
// recover and emit a valid FrameTrellis protocol event. No native crash needed.
#include <cstdio>
#include <string>

int main()
{
    const std::string flood(2 * 1024 * 1024, 'x');
    std::fwrite(flood.data(), 1, flood.size(), stdout);
    std::fputc('\n', stdout);
    // project(25) has 40 timeline frames because its audio lasts longer
    // than its video clip. Keep the first started count consistent.
    std::fputs("@ft {\"event\":\"started\",\"encoder\":\"flood-fixture\",\"total\":40}\n", stdout);
    // A broken native renderer must not rewrite the expected total, move the
    // progress bar backwards or replace a legitimate start event.
    std::fputs("@ft {\"event\":\"progress\",\"frame\":20,\"total\":40}\n", stdout);
    std::fputs("@ft {\"event\":\"progress\",\"frame\":10,\"total\":40}\n", stdout);
    std::fputs("@ft {\"event\":\"progress\",\"frame\":30,\"total\":41}\n", stdout);
    std::fputs("@ft {\"event\":\"started\",\"encoder\":\"duplicate\",\"total\":200}\n", stdout);
    std::fputs("@ft {\"event\":\"progress\",\"frame\":41,\"total\":40}\n", stdout);
    std::fputs("@ft {\"event\":\"progress\",\"frame\":40,\"total\":40}\n", stdout);
    std::fflush(stdout);
    return 43;
}
