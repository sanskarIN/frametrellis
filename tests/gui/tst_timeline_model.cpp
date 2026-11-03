// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "ui/timeline_model.h"

#include <QSignalSpy>
#include <QTest>

using namespace ft;

namespace {

Track makeVideoTrack(int count)
{
    Track track;
    track.id = QStringLiteral("video-track");
    track.kind = TrackKind::Video;
    track.name = QStringLiteral("V1");
    for (int i = 0; i < count; ++i) {
        Clip clip;
        clip.id = QStringLiteral("clip-%1").arg(i);
        clip.assetId = QStringLiteral("unavailable-source");
        clip.start = static_cast<FrameCount>(i) * 10;
        clip.duration = 5;
        track.clips.push_back(std::move(clip));
    }
    return track;
}

void verifyRange(const QSignalSpy& spy, int signal, int first, int last)
{
    const QList<QVariant> args = spy.at(signal);
    QCOMPARE(args.at(0).value<QModelIndex>().row(), first);
    QCOMPARE(args.at(1).value<QModelIndex>().row(), last);
}

} // namespace

class TimelineModelTest : public QObject
{
    Q_OBJECT

private slots:
    void unchangedLargeTrackDoesNotInvalidateDelegates()
    {
        // Repeated playback/selection refreshes must not rebind 10,000 unchanged clips.
        const Project project = Project::createNew(QStringLiteral("Timeline model"), {});
        const Track track = makeVideoTrack(10'000);
        ui::ClipListModel model;
        model.setClips(track, project, {});
        QCOMPARE(model.rowCount(), 10'000);

        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
        QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
        QSignalSpy count(&model, &ui::ClipListModel::countChanged);
        model.setClips(track, project, {});
        QCOMPARE(changed.size(), 0);
        QCOMPARE(reset.size(), 0);
        QCOMPARE(count.size(), 0);
    }

    void onlyAffectedClipRangesAreInvalidated()
    {
        const Project project = Project::createNew(QStringLiteral("Timeline model"), {});
        Track track = makeVideoTrack(12);
        ui::ClipListModel model;
        model.setClips(track, project, {});
        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
        QSignalSpy reset(&model, &QAbstractItemModel::modelReset);

        track.clips[4].properties.gain = 0.5;
        model.setClips(track, project, {});
        QCOMPARE(changed.size(), 1);
        verifyRange(changed, 0, 4, 4);
        QCOMPARE(reset.size(), 0);
        changed.clear();

        // Consecutive edits coalesce, while an unrelated clip stays untouched.
        track.clips[2].properties.muted = true;
        track.clips[3].properties.muted = true;
        track.clips[9].duration = 7;
        model.setClips(track, project, {});
        QCOMPARE(changed.size(), 2);
        verifyRange(changed, 0, 2, 3);
        verifyRange(changed, 1, 9, 9);
        QCOMPARE(reset.size(), 0);
        changed.clear();

        const QSet<Id> selection{track.clips[7].id};
        model.setClips(track, project, selection);
        QCOMPARE(changed.size(), 1);
        verifyRange(changed, 0, 7, 7);
        QCOMPARE(model.data(model.index(7), ui::ClipListModel::SelectedRole).toBool(), true);
        QCOMPARE(reset.size(), 0);
    }

    void structuralChangeStillResetsModel()
    {
        const Project project = Project::createNew(QStringLiteral("Timeline model"), {});
        Track track = makeVideoTrack(3);
        ui::ClipListModel model;
        model.setClips(track, project, {});

        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
        QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
        QSignalSpy count(&model, &ui::ClipListModel::countChanged);
        track.clips.push_back(makeVideoTrack(4).clips.back());
        model.setClips(track, project, {});
        QCOMPARE(model.rowCount(), 4);
        QCOMPARE(reset.size(), 1);
        QCOMPARE(count.size(), 1);
        QCOMPARE(changed.size(), 0);
    }
};

QTEST_GUILESS_MAIN(TimelineModelTest)
#include "tst_timeline_model.moc"
