// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
//
// The editor opens media on several threads at once as soon as a project is loaded: the preview
// builds its graph while thumbnails and waveforms are computed in the background. MLT sets up
// some global state lazily on first use without a lock, so the very first files opened in a
// process must not race. This test runs in its own process so that nothing has opened a file
// before the threads start.

#include "engine/mlt_profile.h"
#include "engine/mlt_runtime.h"
#include "engine/timeline_graph.h"
#include "media_fixtures.h"

#include <QTemporaryDir>
#include <QTest>
#include <mlt++/Mlt.h>

#include <atomic>
#include <latch>
#include <thread>
#include <vector>

using namespace ft;

class TestEngineThreads : public QObject
{
    Q_OBJECT

private slots:
    void firstFilesOpenedConcurrently()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const Status started = engine::MltRuntime::initialize();
        QVERIFY2(started, qPrintable(started ? QString() : started.error().toString()));
        // Neither fixture is written with MLT, so no file has been opened through it yet.
        const QString tone = testing::makeTone(dir.path(), QStringLiteral("tone.wav"), 1.0);
        const QString still =
            testing::makeStill(dir.path(), QStringLiteral("still.png"), Qt::cyan, QSize(32, 18));
        QVERIFY(!tone.isEmpty());
        QVERIFY(!still.isEmpty());

        constexpr int threadCount = 8;
        std::latch start(threadCount);
        std::atomic<int> opened = 0;
        std::vector<std::thread> threads;
        threads.reserve(threadCount);
        for (int i = 0; i < threadCount; ++i) {
            threads.emplace_back([&, i] {
                auto profile = engine::createProfile(64, 36, FrameRate::integer(25));
                start.arrive_and_wait();
                const bool image = i % 2 == 1;
                std::unique_ptr<Mlt::Producer> producer =
                    engine::TimelineGraphBuilder::openProducer(
                        *profile, image ? still : tone, image ? MediaKind::Image : MediaKind::Audio,
                        /*audioOnly=*/!image);
                if (producer) {
                    std::unique_ptr<Mlt::Frame> frame(producer->get_frame());
                    if (frame && frame->is_valid()) {
                        ++opened;
                    }
                }
            });
        }
        for (std::thread& thread : threads) {
            thread.join();
        }
        QCOMPARE(opened.load(), threadCount);
    }

    void profilesRemainSafeAcrossConcurrentRebuilds()
    {
        constexpr int threadCount = 12;
        constexpr int iterations = 24;
        std::latch start(threadCount);
        std::atomic<int> created = 0;
        std::vector<std::thread> threads;
        threads.reserve(threadCount);
        for (int i = 0; i < threadCount; ++i) {
            threads.emplace_back([&] {
                start.arrive_and_wait();
                for (int j = 0; j < iterations; ++j) {
                    if (engine::createProfile(64, 36, FrameRate::integer(25))) {
                        ++created;
                    }
                }
            });
        }
        for (std::thread& thread : threads) {
            thread.join();
        }
        QCOMPARE(created.load(), threadCount * iterations);
    }
};

QTEST_GUILESS_MAIN(TestEngineThreads)
#include "tst_engine_threads.moc"
