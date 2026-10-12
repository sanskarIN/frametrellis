// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include <QString>

#include <functional>
#include <vector>

namespace ft::rendering {

/// One check of the installation self-test.
struct SelfTestStep
{
    QString name;
    bool passed = false;
    QString detail; ///< what was found, or why the check failed
};

struct SelfTestReport
{
    std::vector<SelfTestStep> steps;
    QString projectPath; ///< the generated project, usable for further checks
    QString outputPath;  ///< the exported MP4

    [[nodiscard]] bool passed() const;
};

/// Exercises the media engine end to end inside @p workDirectory: checks the MLT services the
/// editor needs, generates sample media, probes it, builds and saves a project, reloads it,
/// exports it to MP4 and verifies the result. It needs no sample files and no network, so it
/// shows whether a package is complete on a machine without development tools.
///
/// Requires ft::engine::MltRuntime::initialize(). @p onStep is called after every check.
SelfTestReport runSelfTest(const QString& workDirectory,
                           const std::function<void(const SelfTestStep&)>& onStep = {});

} // namespace ft::rendering
