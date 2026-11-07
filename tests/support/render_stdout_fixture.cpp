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
    std::fputs("@ft {\"event\":\"started\",\"encoder\":\"flood-fixture\",\"total\":100}\n",
               stdout);
    std::fflush(stdout);
    return 43;
}
