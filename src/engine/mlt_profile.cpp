// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "engine/mlt_profile.h"

#include <mlt++/Mlt.h>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace ft::engine {

std::unique_ptr<Mlt::Profile> createProfile(int width, int height, const FrameRate& rate)
{
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
    if (frameSize.isEmpty()) {
        return {2, 2};
    }
    if (maxSize.isEmpty() ||
        (frameSize.width() <= maxSize.width() && frameSize.height() <= maxSize.height())) {
        return frameSize;
    }
    const double scale = std::min(static_cast<double>(maxSize.width()) / frameSize.width(),
                                  static_cast<double>(maxSize.height()) / frameSize.height());
    auto even = [](double v) { return std::max(2, static_cast<int>(std::lround(v / 2.0)) * 2); };
    return {even(frameSize.width() * scale), even(frameSize.height() * scale)};
}

} // namespace ft::engine
