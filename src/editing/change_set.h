// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include "core/ids.h"

#include <QMetaType>
#include <QSet>

namespace ft {

/// Describes which parts of a project an edit touched so views can refresh selectively.
struct ChangeSet
{
    bool trackStructure = false; ///< tracks added, removed or reordered
    QSet<Id> tracks;             ///< tracks whose properties or clips changed
    bool media = false;          ///< media list or media metadata changed
    bool settings = false;       ///< project settings or name changed
    bool markers = false;        ///< sequence review markers changed
    bool subtitles = false;      ///< sequence subtitle text, timing or appearance changed

    [[nodiscard]] bool isEmpty() const
    {
        return !trackStructure && tracks.isEmpty() && !media && !settings && !markers && !subtitles;
    }

    void merge(const ChangeSet& other)
    {
        trackStructure = trackStructure || other.trackStructure;
        tracks.unite(other.tracks);
        media = media || other.media;
        settings = settings || other.settings;
        markers = markers || other.markers;
        subtitles = subtitles || other.subtitles;
    }

    static ChangeSet everything()
    {
        ChangeSet c;
        c.trackStructure = true;
        c.media = true;
        c.settings = true;
        c.markers = true;
        c.subtitles = true;
        return c;
    }
};

} // namespace ft

Q_DECLARE_METATYPE(ft::ChangeSet)
