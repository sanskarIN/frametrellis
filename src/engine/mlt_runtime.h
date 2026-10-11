// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include "core/result.h"

#include <QString>
#include <QStringList>

namespace ft::engine {

/// Which MLT services this installation provides. FrameTrellis checks these at startup so that a
/// broken or incomplete installation produces a clear message instead of failing later.
struct EngineCapabilities
{
    QString mltVersion;
    QString repositoryPath;
    bool avformatProducer = false;
    bool avformatConsumer = false;
    bool colorProducer = false;
    bool mixTransition = false;
    QString compositor; ///< "affine" or "composite"; empty if neither exists
    bool volumeFilter = false;
    bool brightnessFilter = false;
    bool eqFilter = false; ///< libavfilter "eq" bridge for brightness/contrast/saturation

    /// Services without which editing cannot work at all.
    [[nodiscard]] QStringList missingEssentials() const;
    /// Optional services whose absence disables individual features.
    [[nodiscard]] QStringList missingOptional() const;
};

/// Process-wide MLT framework lifetime. initialize() must be called once from the main thread
/// before any other engine code runs; shutdown() only after every MLT object has been destroyed.
class MltRuntime
{
public:
    static Status initialize();
    static bool isInitialized();
    static void shutdown();
    static const EngineCapabilities& capabilities();
};

} // namespace ft::engine
