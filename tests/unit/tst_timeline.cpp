// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "timeline/timeline.h"

#include <QHash>
#include <QTest>

using namespace ft;

namespace {

const Id kVideoAsset = QStringLiteral("asset-video");
const Id kAudioAsset = QStringLiteral("asset-audio");
const Id kStillAsset = QStringLiteral("asset-still");

SourceLookup testSources()
{
    return [](const Id& id) -> std::optional<SourceLimits> {
        if (id == kVideoAsset) {
            return SourceLimits{FrameCount{250}, true, true};
        }
        if (id == kAudioAsset) {
            return SourceLimits{FrameCount{500}, false, true};
        }
        if (id == kStillAsset) {
            return SourceLimits{std::nullopt, true, false};
        }
        return std::nullopt;
    };
}

Clip makeClip(const QString& id, const Id& asset, FrameCount start, FrameCount in, FrameCount duration)
{
    Clip c;
    c.id = id;
    c.assetId = asset;
    c.start = start;
    c.sourceIn = in;
    c.duration = duration;
    return c;
}

Timeline makeTimeline()
{
    Timeline t;
    Track v1;
    v1.id = QStringLiteral("v1");
    v1.name = QStringLiteral("V1");
    v1.kind = TrackKind::Video;
    Track v2 = v1;
    v2.id = QStringLiteral("v2");
    v2.name = QStringLiteral("V2");
    Track a1;
    a1.id = QStringLiteral("a1");
    a1.name = QStringLiteral("A1");
    a1.kind = TrackKind::Audio;
    (void)t.insertTrack(v1);
    (void)t.insertTrack(v2);
    (void)t.insertTrack(a1);
    return t;
}

} // namespace

class TimelineTest : public QObject
{
    Q_OBJECT

private slots:
    void insertKeepsClipsSorted()
    {
        Timeline t = makeTimeline();
        QVERIFY(t.insertClip(QStringLiteral("v1"), makeClip(QStringLiteral("c2"), kVideoAsset, 100, 0, 50), testSources()));
        QVERIFY(t.insertClip(QStringLiteral("v1"), makeClip(QStringLiteral("c1"), kVideoAsset, 0, 0, 50), testSources()));
        QVERIFY(t.insertClip(QStringLiteral("v1"), makeClip(QStringLiteral("c3"), kVideoAsset, 50, 0, 50), testSources()));
        const Track* v1 = t.track(QStringLiteral("v1"));
        QCOMPARE(v1->clips.size(), size_t{3});
        QCOMPARE(v1->clips[0].id, QStringLiteral("c1"));
        QCOMPARE(v1->clips[1].id, QStringLiteral("c3"));
        QCOMPARE(v1->clips[2].id, QStringLiteral("c2"));
        QCOMPARE(t.duration(), FrameCount{150});
        QCOMPARE(t.clipCount(), 3);
        QVERIFY(t.validate(testSources()));
    }

    void insertRejectsOverlapAndBadRanges()
    {
        Timeline t = makeTimeline();
        QVERIFY(t.insertClip(QStringLiteral("v1"), makeClip(QStringLiteral("c1"), kVideoAsset, 10, 0, 50), testSources()));
        const Timeline before = t;
        Status overlap = t.insertClip(QStringLiteral("v1"), makeClip(QStringLiteral("c2"), kVideoAsset, 59, 0, 10), testSources());
        QVERIFY(!overlap);
        QCOMPARE(overlap.error().code, ErrorCode::Conflict);
        // Touching clips are fine (end is exclusive).
        QVERIFY(t.insertClip(QStringLiteral("v1"), makeClip(QStringLiteral("c3"), kVideoAsset, 60, 0, 10), testSources()));
        // Past the end of the source.
        QCOMPARE(t.insertClip(QStringLiteral("v2"), makeClip(QStringLiteral("c4"), kVideoAsset, 0, 200, 51), testSources()).error().code,
                 ErrorCode::OutOfRange);
        // Zero duration and negative positions.
        QVERIFY(!t.insertClip(QStringLiteral("v2"), makeClip(QStringLiteral("c5"), kVideoAsset, 0, 0, 0), testSources()));
        QVERIFY(!t.insertClip(QStringLiteral("v2"), makeClip(QStringLiteral("c6"), kVideoAsset, -1, 0, 5), testSources()));
        // Unknown media and unknown track.
        QCOMPARE(t.insertClip(QStringLiteral("v2"), makeClip(QStringLiteral("c7"), QStringLiteral("nope"), 0, 0, 5), testSources()).error().code,
                 ErrorCode::NotFound);
        QCOMPARE(t.insertClip(QStringLiteral("zz"), makeClip(QStringLiteral("c8"), kVideoAsset, 0, 0, 5), testSources()).error().code,
                 ErrorCode::NotFound);
        // Duplicate clip id.
        QVERIFY(!t.insertClip(QStringLiteral("v2"), makeClip(QStringLiteral("c1"), kVideoAsset, 0, 0, 5), testSources()));
        QCOMPARE(t.track(QStringLiteral("v2"))->clips.size(), size_t{0});
        QVERIFY(before.track(QStringLiteral("v1"))->clips.size() == 1);
    }

    void trackKindCompatibility()
    {
        Timeline t = makeTimeline();
        // Audio-only media cannot go on a video track and stills cannot go on audio tracks.
        QVERIFY(!t.insertClip(QStringLiteral("v1"), makeClip(QStringLiteral("c1"), kAudioAsset, 0, 0, 10), testSources()));
        QVERIFY(!t.insertClip(QStringLiteral("a1"), makeClip(QStringLiteral("c2"), kStillAsset, 0, 0, 10), testSources()));
        // Video files with audio can be used on audio tracks.
        QVERIFY(t.insertClip(QStringLiteral("a1"), makeClip(QStringLiteral("c3"), kVideoAsset, 0, 0, 10), testSources()));
        // Stills are unbounded.
        QVERIFY(t.insertClip(QStringLiteral("v1"), makeClip(QStringLiteral("c4"), kStillAsset, 0, 5000, 100000), testSources()));
    }

    void moveClipWithinAndAcrossTracks()
    {
        Timeline t = makeTimeline();
        (void)t.insertClip(QStringLiteral("v1"), makeClip(QStringLiteral("c1"), kVideoAsset, 0, 0, 50), testSources());
        (void)t.insertClip(QStringLiteral("v1"), makeClip(QStringLiteral("c2"), kVideoAsset, 100, 0, 50), testSources());
        QVERIFY(t.moveClip(QStringLiteral("c1"), QStringLiteral("v1"), 200, testSources()));
        QCOMPARE(t.track(QStringLiteral("v1"))->clips.back().id, QStringLiteral("c1"));
        QCOMPARE(t.clip(QStringLiteral("c1"))->start, FrameCount{200});
        // Moving onto another clip is refused and leaves the clip in place.
        QCOMPARE(t.moveClip(QStringLiteral("c1"), QStringLiteral("v1"), 120, testSources()).error().code, ErrorCode::Conflict);
        QCOMPARE(t.clip(QStringLiteral("c1"))->start, FrameCount{200});
        // Overlapping itself while moving is allowed.
        QVERIFY(t.moveClip(QStringLiteral("c1"), QStringLiteral("v1"), 190, testSources()));
        QVERIFY(t.moveClip(QStringLiteral("c1"), QStringLiteral("v2"), 120, testSources()));
        QCOMPARE(t.trackOfClip(QStringLiteral("c1"))->id, QStringLiteral("v2"));
        QVERIFY(!t.moveClip(QStringLiteral("c1"), QStringLiteral("v2"), -5, testSources()));
        QVERIFY(t.validate(testSources()));
    }

    void trimStartAndEnd()
    {
        Timeline t = makeTimeline();
        (void)t.insertClip(QStringLiteral("v1"), makeClip(QStringLiteral("c1"), kVideoAsset, 100, 20, 50), testSources());
        // Trimming the start inward advances the source in-point.
        QVERIFY(t.trimClipStart(QStringLiteral("c1"), 110, testSources()));
        const Clip* c = t.clip(QStringLiteral("c1"));
        QCOMPARE(c->start, FrameCount{110});
        QCOMPARE(c->sourceIn, FrameCount{30});
        QCOMPARE(c->duration, FrameCount{40});
        QCOMPARE(c->end(), FrameCount{150});
        // Extending beyond the first source frame is refused.
        QVERIFY(!t.trimClipStart(QStringLiteral("c1"), 79, testSources()));
        QVERIFY(t.trimClipStart(QStringLiteral("c1"), 80, testSources()));
        QCOMPARE(t.clip(QStringLiteral("c1"))->sourceIn, FrameCount{0});
        // End trims are limited by the source length (250 frames).
        QVERIFY(t.trimClipEnd(QStringLiteral("c1"), 330, testSources()));
        QVERIFY(!t.trimClipEnd(QStringLiteral("c1"), 331, testSources()));
        QVERIFY(!t.trimClipEnd(QStringLiteral("c1"), 80, testSources())); // zero length
        QVERIFY(t.trimClipEnd(QStringLiteral("c1"), 81, testSources()));
        QCOMPARE(t.clip(QStringLiteral("c1"))->duration, FrameCount{1});
    }

    void trimRespectsNeighbours()
    {
        Timeline t = makeTimeline();
        (void)t.insertClip(QStringLiteral("v1"), makeClip(QStringLiteral("a"), kVideoAsset, 0, 0, 50), testSources());
        (void)t.insertClip(QStringLiteral("v1"), makeClip(QStringLiteral("b"), kVideoAsset, 60, 50, 50), testSources());
        QVERIFY(!t.trimClipEnd(QStringLiteral("a"), 61, testSources()));
        QVERIFY(t.trimClipEnd(QStringLiteral("a"), 60, testSources()));
        QVERIFY(!t.trimClipStart(QStringLiteral("b"), 59, testSources()));
    }

    void trimClampsFades()
    {
        Timeline t = makeTimeline();
        Clip c = makeClip(QStringLiteral("c1"), kVideoAsset, 0, 0, 100);
        c.properties.fadeIn = 30;
        c.properties.fadeOut = 30;
        (void)t.insertClip(QStringLiteral("v1"), c, testSources());
        QVERIFY(t.trimClipEnd(QStringLiteral("c1"), 40, testSources()));
        const Clip* trimmed = t.clip(QStringLiteral("c1"));
        QVERIFY(trimmed->properties.fadeIn + trimmed->properties.fadeOut <= trimmed->duration);
    }

    void splitClip()
    {
        Timeline t = makeTimeline();
        Clip c = makeClip(QStringLiteral("c1"), kVideoAsset, 100, 10, 60);
        c.properties.gain = 0.5;
        c.properties.fadeIn = 5;
        c.properties.fadeOut = 7;
        (void)t.insertClip(QStringLiteral("v1"), c, testSources());
        QVERIFY(!t.splitClip(QStringLiteral("c1"), 100)); // on the boundary
        QVERIFY(!t.splitClip(QStringLiteral("c1"), 160));
        Result<Id> right = t.splitClip(QStringLiteral("c1"), 130, QStringLiteral("c2"));
        QVERIFY(right);
        QCOMPARE(*right, QStringLiteral("c2"));
        const Clip* left = t.clip(QStringLiteral("c1"));
        const Clip* r = t.clip(QStringLiteral("c2"));
        QCOMPARE(left->duration, FrameCount{30});
        QCOMPARE(left->properties.fadeIn, FrameCount{5});
        QCOMPARE(left->properties.fadeOut, FrameCount{0});
        QCOMPARE(r->start, FrameCount{130});
        QCOMPARE(r->sourceIn, FrameCount{40});
        QCOMPARE(r->duration, FrameCount{30});
        QCOMPARE(r->properties.fadeIn, FrameCount{0});
        QCOMPARE(r->properties.fadeOut, FrameCount{7});
        QCOMPARE(r->properties.gain, 0.5);
        // The pieces are contiguous and cover the original source range.
        QCOMPARE(left->end(), r->start);
        QCOMPARE(left->sourceOut(), r->sourceIn);
        QVERIFY(t.validate(testSources()));
    }

    void rippleRemoveShiftsFollowingClips()
    {
        Timeline t = makeTimeline();
        (void)t.insertClip(QStringLiteral("v1"), makeClip(QStringLiteral("a"), kVideoAsset, 0, 0, 50), testSources());
        (void)t.insertClip(QStringLiteral("v1"), makeClip(QStringLiteral("b"), kVideoAsset, 50, 0, 30), testSources());
        (void)t.insertClip(QStringLiteral("v1"), makeClip(QStringLiteral("c"), kVideoAsset, 100, 0, 20), testSources());
        (void)t.insertClip(QStringLiteral("v2"), makeClip(QStringLiteral("d"), kVideoAsset, 100, 0, 20), testSources());
        QVERIFY(t.rippleRemoveClip(QStringLiteral("b")));
        QCOMPARE(t.clip(QStringLiteral("c"))->start, FrameCount{70});
        QCOMPARE(t.clip(QStringLiteral("a"))->start, FrameCount{0});
        QCOMPARE(t.clip(QStringLiteral("d"))->start, FrameCount{100}); // other tracks untouched
        QVERIFY(t.removeClip(QStringLiteral("a")));
        QVERIFY(!t.clip(QStringLiteral("a")));
        QVERIFY(!t.removeClip(QStringLiteral("a")));
    }

    void lockedTracksRejectEdits()
    {
        Timeline t = makeTimeline();
        (void)t.insertClip(QStringLiteral("v1"), makeClip(QStringLiteral("a"), kVideoAsset, 0, 0, 50), testSources());
        QVERIFY(t.setTrackLocked(QStringLiteral("v1"), true));
        QCOMPARE(t.moveClip(QStringLiteral("a"), QStringLiteral("v1"), 10, testSources()).error().code, ErrorCode::Conflict);
        QVERIFY(!t.trimClipEnd(QStringLiteral("a"), 40, testSources()));
        QVERIFY(!t.splitClip(QStringLiteral("a"), 20));
        QVERIFY(!t.removeClip(QStringLiteral("a")));
        QVERIFY(!t.insertClip(QStringLiteral("v1"), makeClip(QStringLiteral("b"), kVideoAsset, 60, 0, 10), testSources()));
        QVERIFY(!t.moveClip(QStringLiteral("a"), QStringLiteral("v2"), 0, testSources()));
        QVERIFY(t.setTrackLocked(QStringLiteral("v1"), false));
        QVERIFY(t.moveClip(QStringLiteral("a"), QStringLiteral("v2"), 0, testSources()));
    }

    void trackOperations()
    {
        Timeline t = makeTimeline();
        QVERIFY(t.renameTrack(QStringLiteral("v1"), QStringLiteral("  Main  ")));
        QCOMPARE(t.track(QStringLiteral("v1"))->name, QStringLiteral("Main"));
        QVERIFY(!t.renameTrack(QStringLiteral("v1"), QStringLiteral("   ")));
        QVERIFY(t.moveTrack(QStringLiteral("a1"), 0));
        QCOMPARE(t.tracks().front().id, QStringLiteral("a1"));
        QVERIFY(t.moveTrack(QStringLiteral("a1"), 99)); // clamps to the end
        QCOMPARE(t.tracks().back().id, QStringLiteral("a1"));
        QVERIFY(t.setTrackMuted(QStringLiteral("a1"), true));
        QVERIFY(t.setTrackHidden(QStringLiteral("v2"), true));
        QVERIFY(t.track(QStringLiteral("a1"))->muted);
        QVERIFY(t.track(QStringLiteral("v2"))->hidden);
        Track duplicate;
        duplicate.id = QStringLiteral("v1");
        duplicate.name = QStringLiteral("dup");
        QVERIFY(!t.insertTrack(duplicate));
        Result<Track> removed = t.removeTrack(QStringLiteral("v2"));
        QVERIFY(removed);
        QCOMPARE(t.trackCount(), 2);
        QVERIFY(!t.removeTrack(QStringLiteral("v2")));
    }

    void trackLimit()
    {
        Timeline t;
        for (int i = 0; i < kMaxTracks; ++i) {
            Track track;
            track.id = QStringLiteral("t%1").arg(i);
            track.name = track.id;
            QVERIFY(t.insertTrack(track));
        }
        Track extra;
        extra.id = QStringLiteral("extra");
        extra.name = extra.id;
        QCOMPARE(t.insertTrack(extra).error().code, ErrorCode::OutOfRange);
    }

    void clipPropertyValidation()
    {
        Timeline t = makeTimeline();
        (void)t.insertClip(QStringLiteral("v1"), makeClip(QStringLiteral("a"), kVideoAsset, 0, 0, 50), testSources());
        ClipProperties p;
        p.gain = 2.0;
        p.muted = true;
        p.video.brightness = 0.25;
        QVERIFY(t.setClipProperties(QStringLiteral("a"), p));
        QCOMPARE(t.clip(QStringLiteral("a"))->properties, p);
        p.gain = 10.0;
        QVERIFY(!t.setClipProperties(QStringLiteral("a"), p));
        p.gain = 1.0;
        p.fadeIn = 40;
        p.fadeOut = 20;
        QVERIFY(!t.setClipProperties(QStringLiteral("a"), p)); // fades exceed the clip
        p.fadeOut = 10;
        QVERIFY(t.setClipProperties(QStringLiteral("a"), p));
        p.video.opacity = 1.5;
        QVERIFY(!t.setClipProperties(QStringLiteral("a"), p));
    }

    void validateDetectsCorruption()
    {
        Timeline t = makeTimeline();
        (void)t.insertClip(QStringLiteral("v1"), makeClip(QStringLiteral("a"), kVideoAsset, 0, 0, 50), testSources());
        std::vector<Track> tracks = t.tracks();
        tracks[0].clips.push_back(makeClip(QStringLiteral("b"), kVideoAsset, 40, 0, 20)); // overlaps
        Timeline corrupted;
        corrupted.restoreTracks(tracks);
        QCOMPARE(corrupted.validate(testSources()).error().code, ErrorCode::InvalidDocument);
        tracks[0].clips.pop_back();
        tracks[1].clips.push_back(makeClip(QStringLiteral("a"), kVideoAsset, 0, 0, 20)); // duplicate id
        corrupted.restoreTracks(tracks);
        QVERIFY(!corrupted.validate(testSources()));
    }

    void editPointsAndSnapping()
    {
        Timeline t = makeTimeline();
        (void)t.insertClip(QStringLiteral("v1"), makeClip(QStringLiteral("a"), kVideoAsset, 10, 0, 50), testSources());
        (void)t.insertClip(QStringLiteral("v2"), makeClip(QStringLiteral("b"), kVideoAsset, 100, 0, 20), testSources());
        const std::vector<FrameCount> points = t.editPoints();
        QCOMPARE(points, (std::vector<FrameCount>{0, 10, 60, 100, 120}));
        QCOMPARE(t.editPoints(QStringLiteral("b")), (std::vector<FrameCount>{0, 10, 60}));
        QCOMPARE(*snapToNearest(63, 5, points), FrameCount{60});
        QCOMPARE(*snapToNearest(97, 5, points), FrameCount{100});
        QVERIFY(!snapToNearest(80, 5, points).has_value());
        QCOMPARE(*snapToNearest(-3, 5, points), FrameCount{0});
        QVERIFY(!snapToNearest(50, 5, {}).has_value());
        QVERIFY(t.usesAsset(kVideoAsset));
        QCOMPARE(t.clipsUsingAsset(kVideoAsset).size(), size_t{2});
    }
};

QTEST_GUILESS_MAIN(TimelineTest)
#include "tst_timeline.moc"
