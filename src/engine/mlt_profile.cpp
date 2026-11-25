// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "engine/mlt_profile.h"

#include <mlt++/Mlt.h>

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <numeric>

namespace ft::engine {

std::unique_ptr<Mlt::Profile> createProfile(int width, int height, const FrameRate& rate)
{
    // MLT's default Profile constructor reads and updates process-wide profile
    // normalization state (mlt_profile_load_file / set_mlt_normalization).
    // Concurrent construction races the shared string storage on MLT 7.22:
    // tst_engine_threads caught a heap-use-after-free under ASan. Serialize
    // creation and initial configuration; no MLT objects escape until ready.
    static std::mutex profileCreationMutex;
    std::lock_guard lock(profileCreationMutex);
    auto profile = std::make_unique<Mlt::Profile>();
    profile->set_explicit(1);
    profile->set_width(width);
    profile->set_height(height);
    profile->set_frame_rate(static_cast<int>(rate.numerator()),
                            static_cast<int>(rate.denominator()));
    profile->set_progressive(1);
    profile->set_sample_aspect(1, 1);
    const int divisor = std::gcd(width, height);
    profile->set_display_aspect(width / std::max(divisor, 1), height / std::max(divisor, 1));
    profile->set_colorspace(709);
    return profile;
}

QSize fitPreviewSize(QSize frameSize, QSize maxSize)
{
    // Cap even callers that omit a display limit: this size eventually reaches
    // native decoders, and a malformed media header can report huge dimensions.
    const QSize decoderLimit(8192, 8192);
    const QSize limit = maxSize.isEmpty() ? decoderLimit : maxSize.boundedTo(decoderLimit);
    if (frameSize.isEmpty()) {
        return QSize(2, 2).boundedTo(limit);
    }
    if (frameSize.width() <= limit.width() && frameSize.height() <= limit.height()) {
        return frameSize;
    }

    using Wide = std::int64_t;
    const Wide width = frameSize.width();
    const Wide height = frameSize.height();
    const bool widthLimited = Wide(limit.width()) * height <= Wide(limit.height()) * width;
    const Wide numerator = widthLimited ? limit.width() : limit.height();
    const Wide denominator = widthLimited ? width : height;

    // Round to a nearby even size without exceeding the caller's limit or
    // overflowing a signed int. A one-pixel axis is the only odd exception
    // when downscaling; it cannot be expanded to two without breaking bounds.
    const auto fitAxis = [numerator, denominator](Wide original, int bound) {
        const Wide cap = std::min(original, Wide(bound));
        const Wide evenCap = cap > 1 ? cap - cap % 2 : 1;
        const Wide rounded = 2 * ((original * numerator + denominator) / (2 * denominator));
        return static_cast<int>(std::clamp(rounded, std::min(Wide(2), evenCap), evenCap));
    };
    return {fitAxis(width, limit.width()), fitAxis(height, limit.height())};
}

} // namespace ft::engine
