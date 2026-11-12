// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
// Synthetic protocol-compliant helper that deliberately lies about completing.
// It never runs codecs; only the editor-side existence/size gate is under test.
#include <cstdio>
#include <fstream>
#include <string>

int main(int argc, char* argv[])
{
    std::string output;
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == "--output") {
            output = argv[i + 1];
            break;
        }
    }
    if (output.empty()) {
        return 2;
    }

    if (output.find("fixture-empty-success") != std::string::npos) {
        std::ofstream file(output, std::ios::binary);
        if (!file) {
            return 2;
        }
    } else if (output.find("fixture-nonempty-success") != std::string::npos) {
        std::ofstream file(output, std::ios::binary);
        if (!file) {
            return 2;
        }
        file.write("test", 4);
    }
    std::fputs("@ft {\"event\":\"started\",\"encoder\":\"synthetic-fixture\",\"total\":25}\n",
               stdout);
    std::fputs("@ft {\"event\":\"finished\"}\n", stdout);
    std::fflush(stdout);
    return 0;
}
