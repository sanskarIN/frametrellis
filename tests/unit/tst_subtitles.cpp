// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "editing/project_document.h"
#include "editing/subtitle_edits.h"
#include "project/project_serializer.h"

#include <QJsonArray>
#include <QSignalSpy>
#include <QTest>

using namespace ft;

namespace {
SubtitleCue cue(const QString& id, std::int64_t start = 0, std::int64_t end = 2000,
                const QString& text = QStringLiteral("A subtitle"))
{
    return {id, start, end, text};
}
Project project()
{
    return Project::createNew(QStringLiteral("Subtitles"), {});
}
QJsonObject withTrack(QJsonObject document, const QJsonValue& track)
{
    QJsonObject body = document[QStringLiteral("project")].toObject();
    body[QStringLiteral("subtitles")] = track;
    document[QStringLiteral("project")] = body;
    return document;
}
} // namespace

class SubtitleTest : public QObject
{
    Q_OBJECT
private slots:
    void exactClockAndHalfOpenSampling()
    {
        for (const FrameRate& rate :
             FrameRate::standardRates() + QList<FrameRate>{FrameRate::integer(1000)}) {
            for (FrameCount frame : {FrameCount{0}, FrameCount{1}, FrameCount{29}, FrameCount{1001},
                                     FrameCount{30000}}) {
                const auto ms = subtitleMillisecondsAtFrame(frame, rate);
                QVERIFY(ms);
                const auto back = subtitleFrameAtMilliseconds(*ms, rate);
                QVERIFY(back);
                QCOMPARE(*back, frame);
            }
        }
        const auto rate = *FrameRate::create(30000, 1001);
        QCOMPARE(*subtitleFrameAtMilliseconds(1000, rate), FrameCount{30});
        QCOMPARE(*subtitleFrameAtMilliseconds(2000, rate), FrameCount{60});
        SubtitleTrack track;
        track.cues = {cue(QStringLiteral("a"), 1000, 2000), cue(QStringLiteral("b"), 2000, 3000)};
        QVERIFY(!subtitleAtFrame(track, 29, rate));
        QCOMPARE(subtitleAtFrame(track, 30, rate)->id, QStringLiteral("a"));
        QCOMPARE(subtitleAtFrame(track, 59, rate)->id, QStringLiteral("a"));
        QCOMPARE(subtitleAtFrame(track, 60, rate)->id, QStringLiteral("b"));
        QVERIFY(!subtitleAtFrame(track, 90, rate));
        QVERIFY(!subtitleAtFrame(track, -1, rate));
        // A cue shorter than a frame is preserved, even when no sampled frame sees it.
        track.cues = {cue(QStringLiteral("tiny"), 1, 2)};
        QVERIFY(validateSubtitles(track));
        QVERIFY(!subtitleAtFrame(track, 0, FrameRate::integer(25)));
        QVERIFY(!subtitleAtFrame(track, 1, FrameRate::integer(25)));
        QVERIFY(subtitleAtFrame(track, 1, FrameRate::integer(1000)));
        QVERIFY(!subtitleAtFrame(track, 2, FrameRate::integer(1000)));
    }

    void timestampsAreStrictAndRoundtrip()
    {
        for (std::int64_t ms :
             {std::int64_t{0}, std::int64_t{1}, std::int64_t{3'723'004}, kMaxSubtitleTimeMs}) {
            const auto parsed = parseSubtitleTime(formatSubtitleTime(ms));
            QVERIFY(parsed);
            QCOMPARE(*parsed, ms);
        }
        QCOMPARE(formatSubtitleTime(3'723'004), QStringLiteral("01:02:03,004"));
        for (const QString& bad :
             {QString(), QStringLiteral("1:02:03,004"), QStringLiteral("00:60:00,000"),
              QStringLiteral("00:00:60,000"), QStringLiteral("00:00:00.000"),
              QStringLiteral("00:00:00,00"), QStringLiteral("1000:00:00,000"),
              QStringLiteral("00:00:00,000\n"), QStringLiteral(" 00:00:00,000")}) {
            QVERIFY2(!parseSubtitleTime(bad), qPrintable(bad));
        }
        QVERIFY(formatSubtitleTime(-1).isEmpty());
        QVERIFY(!subtitleFrameAtMilliseconds(kMaxSubtitleTimeMs + 1, FrameRate::integer(25)));
        QVERIFY(!subtitleMillisecondsAtFrame(-1, FrameRate::integer(25)));
    }

    void unicodeSrtImportSortsAndExportsExactMilliseconds()
    {
        const QByteArray source =
            QByteArray::fromHex("efbbbf") +
            QStringLiteral("7\r\n00:00:03,005 --> 00:00:04,010\r\nदूसरी पंक्ति\r\nLine "
                           "2\r\n\r\n2\r\n00:00:00,001-->00:00:01,002\r\n<b>Literal</b> 😀\r\n")
                .toUtf8();
        const auto parsed = srt::fromBytes(source);
        QVERIFY2(parsed, qPrintable(parsed ? QString() : parsed.error().toString()));
        QCOMPARE(parsed->cues.size(), size_t{2});
        QCOMPARE(parsed->cues[0].startMs, std::int64_t{1});
        QCOMPARE(parsed->cues[1].endMs, std::int64_t{4010});
        const auto encoded = srt::toBytes(*parsed);
        QVERIFY(encoded);
        QCOMPARE(*encoded,
                 QStringLiteral("1\n00:00:00,001 --> 00:00:01,002\n<b>Literal</b> "
                                "😀\n\n2\n00:00:03,005 --> 00:00:04,010\nदूसरी पंक्ति\nLine 2\n\n")
                     .toUtf8());
        const auto again = srt::fromBytes(*encoded);
        QVERIFY(again);
        QCOMPARE(*srt::toBytes(*again), *encoded);
        QVERIFY(srt::fromBytes(QByteArray("1\r00:00:00,000 --> 00:00:01,000\rText\r")));
        QVERIFY(srt::fromBytes({})->cues.empty());
    }

    void webVttExportPreservesTimesUnicodeAndLiteralText()
    {
        SubtitleTrack track;
        track.cues = {
            cue(QStringLiteral("one"), 1, 1002, QStringLiteral("नमस्ते <b>&</b> 😀\nNext --> line")),
            cue(QStringLiteral("two"), 3'723'004, 3'724'005, QStringLiteral("End"))};
        const SubtitleTrack before = track;
        const auto encoded = webvtt::toBytes(track);
        QVERIFY(encoded);
        QCOMPARE(*encoded, QStringLiteral("WEBVTT\n\n"
                                          "00:00:00.001 --> 00:00:01.002\n"
                                          "नमस्ते &lt;b&gt;&amp;&lt;/b&gt; 😀\nNext --&gt; line\n\n"
                                          "01:02:03.004 --> 01:02:04.005\nEnd\n\n")
                               .toUtf8());
        QCOMPARE(track, before); // Sidecar serialization is read-only.
        QVERIFY(!encoded->contains('\r'));
        QCOMPARE(*webvtt::toBytes(SubtitleTrack{}), QByteArray("WEBVTT\n\n"));
        track.cues[1].startMs = 1000; // Overlap must not produce partial output.
        QVERIFY(!webvtt::toBytes(track));
        track.cues[1].startMs = 3'723'004;
        track.cues[0].text = QStringLiteral("Invalid\n\ncaption");
        QVERIFY(!webvtt::toBytes(track));
    }

    void malformedSrtAndUnicodeAreRefused()
    {
        for (const QByteArray& bytes :
             {QByteArray("hello"), QByteArray("0\n00:00:00,000 --> 00:00:01,000\nText"),
              QByteArray("1"), QByteArray("1\n00:00:00,000 -> 00:00:01,000\nText"),
              QByteArray("1\n00:00:00,000 --> 00:00:01,000"),
              QByteArray("1\n00:00:01,000 --> 00:00:00,000\nText"),
              QByteArray("1\n00:00:00,000 --> 00:00:00,000\nText"),
              QByteArray("1\n00:00:00,000 --> 00:60:01,000\nText"),
              QByteArray("1\n00:00:00,000 --> 00:00:02,000\nFirst\n\n2\n00:00:01,000 --> "
                         "00:00:03,000\nOverlap"),
              QByteArray::fromHex("fffe3100"), QByteArray::fromHex("c3")}) {
            QVERIFY2(!srt::fromBytes(bytes), bytes.constData());
        }
        QVERIFY(!srt::fromBytes(QByteArray(static_cast<int>(kMaxSrtBytes + 1), ' ')));
        const QByteArray prefix("1\n00:00:00,000 --> 00:00:01,000\n");
        QVERIFY(!srt::fromBytes(prefix + QByteArray(kMaxSubtitleTextLength + 1, 'a')));
        QVERIFY(!srt::fromBytes(prefix + QByteArray("bad\0text", 8)));
    }

    void modelValidationIsAtomicAndEnforcesBudgets()
    {
        Project p = project();
        SubtitleTrack track;
        track.cues = {cue(QStringLiteral("later"), 2000, 3000),
                      cue(QStringLiteral("first"), 0, 2000)};
        QVERIFY(p.setSubtitles(track));
        QCOMPARE(p.subtitles().cues[0].id, QStringLiteral("first"));
        const Project before = p;
        for (const SubtitleCue& bad : std::vector<SubtitleCue>{
                 cue(QString(), 0, 100), cue(QStringLiteral("first"), 3000, 4000),
                 cue(QStringLiteral("bad"), -1, 100), cue(QStringLiteral("bad"), 3000, 3000),
                 cue(QStringLiteral("bad"), 3000, kMaxSubtitleTimeMs + 1),
                 cue(QStringLiteral("bad"), 1000, 2500),
                 cue(QStringLiteral("bad"), 3000, 4000, QStringLiteral(" \n")),
                 cue(QStringLiteral("bad"), 3000, 4000, QStringLiteral("a\n\nb")),
                 cue(QStringLiteral("bad"), 3000, 4000, QString(QChar(0xd800)))}) {
            SubtitleTrack invalid = p.subtitles();
            invalid.cues.push_back(bad);
            QVERIFY(!p.setSubtitles(invalid));
            QCOMPARE(p, before);
        }
        SubtitleTrack limit;
        for (int i = 0; i <= kMaxSubtitleCues; ++i) {
            limit.cues.push_back(cue(QStringLiteral("cue-%1").arg(i), i * 2, i * 2 + 1));
        }
        QVERIFY(!p.setSubtitles(limit));
        QCOMPARE(p, before);
        limit.cues.pop_back();
        QVERIFY(p.setSubtitles(limit));
        SubtitleTrack budget;
        for (int i = 0; i <= kMaxSubtitleTotalTextLength / kMaxSubtitleTextLength; ++i) {
            budget.cues.push_back(cue(QStringLiteral("cue-%1").arg(i), i * 2, i * 2 + 1,
                                      QString(kMaxSubtitleTextLength, QLatin1Char('a'))));
        }
        QVERIFY(!validateSubtitles(budget));
        limit.fontPercent = 1;
        QVERIFY(!validateSubtitles(limit));
        limit.fontPercent = 11;
        QVERIFY(!validateSubtitles(limit));
    }

    void commandsHaveExactUndoAndAtomicImport()
    {
        ProjectDocument doc(project());
        QSignalSpy changes(&doc, &ProjectDocument::changed);
        const Project initial = doc.project();
        QVERIFY(doc.execute(edits::addSubtitle(cue(QStringLiteral("a")))));
        QVERIFY(changes.last().at(0).value<ChangeSet>().subtitles);
        QVERIFY(doc.execute(edits::appendSubtitles(
            {cue(QStringLiteral("b"), 2000, 3000), cue(QStringLiteral("c"), 3000, 4000)})));
        const Project three = doc.project();
        QVERIFY(!doc.execute(edits::appendSubtitles(
            {cue(QStringLiteral("ok"), 5000, 6000), cue(QStringLiteral("bad"), 1000, 1500)})));
        QCOMPARE(doc.project(), three);
        QVERIFY(doc.undo());
        QCOMPARE(doc.project().subtitles().cues.size(), size_t{1});
        QVERIFY(doc.redo());
        QCOMPARE(doc.project(), three);
        QVERIFY(doc.execute(edits::updateSubtitle(
            cue(QStringLiteral("a"), 1, 2000, QStringLiteral("नमस्ते\nHello")))));
        const Project edited = doc.project();
        QVERIFY(doc.undo());
        QCOMPARE(doc.project(), three);
        QVERIFY(doc.redo());
        QCOMPARE(doc.project(), edited);
        QVERIFY(doc.execute(edits::setSubtitleAppearance(false, 8, false)));
        const Project styled = doc.project();
        QVERIFY(doc.execute(edits::clearSubtitles()));
        QVERIFY(doc.project().subtitles().cues.empty());
        QCOMPARE(doc.project().subtitles().fontPercent, 8);
        QVERIFY(doc.undo());
        QCOMPARE(doc.project(), styled);
        QVERIFY(doc.execute(edits::removeSubtitle(QStringLiteral("b"))));
        QVERIFY(doc.undo());
        QCOMPARE(doc.project(), styled);
        while (doc.canUndo()) {
            QVERIFY(doc.undo());
        }
        QCOMPARE(doc.project(), initial);
        QVERIFY(!doc.isModified());
        QVERIFY(!doc.execute(edits::removeSubtitle(QStringLiteral("missing"))));
        QVERIFY(!doc.canUndo());
        QVERIFY(doc.execute(edits::setSubtitleAppearance(true, 5, true)));
        QVERIFY(!doc.canUndo());
    }
    void formatSixPreservesCuesOptionsAndLegacyLoads()
    {
        Project p = project();
        QJsonObject legacy = project_format::toJson(p);
        QCOMPARE(legacy[QStringLiteral("version")].toInt(), 2);
        for (int version : {1, 2, 3, 4, 5, 6}) {
            legacy[QStringLiteral("version")] = version;
            QVERIFY(project_format::fromJson(legacy));
        }
        SubtitleTrack track;
        track.cues = {cue(QStringLiteral("a"), 1, 1002, QStringLiteral("नमस्ते\nHello")),
                      cue(QStringLiteral("b"), 3005, 4010)};
        track.burnIn = false;
        track.fontPercent = 8;
        track.background = false;
        QVERIFY(p.setSubtitles(track));
        const QJsonObject saved = project_format::toJson(p);
        QCOMPARE(saved[QStringLiteral("version")].toInt(), 6);
        const auto loaded = project_format::fromBytes(project_format::toBytes(p));
        QVERIFY(loaded);
        QCOMPARE(loaded->subtitles(), track);
        for (int version : {1, 2, 3, 4, 5}) {
            QJsonObject wrong = saved;
            wrong[QStringLiteral("version")] = version;
            QVERIFY(!project_format::fromJson(wrong));
        }
        // SRT times survive a frame-rate change while the media timeline is empty.
        const QByteArray before = *srt::toBytes(p.subtitles());
        ProjectSettings settings = p.settings();
        settings.frameRate = *FrameRate::create(30000, 1001);
        QVERIFY(p.setSettings(settings));
        QCOMPARE(*srt::toBytes(p.subtitles()), before);
        track.cues.clear();
        QVERIFY(p.setSubtitles(track));
        QCOMPARE(project_format::toJson(p)[QStringLiteral("version")].toInt(), 6);
        QVERIFY(p.setSubtitles({}));
        QCOMPARE(project_format::toJson(p)[QStringLiteral("version")].toInt(), 2);
    }

    void malformedProjectSubtitleFieldsFailAtomically()
    {
        Project p = project();
        SubtitleTrack track;
        track.cues = {cue(QStringLiteral("a"))};
        QVERIFY(p.setSubtitles(track));
        QJsonObject document = project_format::toJson(p);
        const QJsonObject valid =
            document[QStringLiteral("project")].toObject()[QStringLiteral("subtitles")].toObject();
        const auto rejects = [&](const QJsonValue& value) {
            const auto parsed = project_format::fromJson(withTrack(document, value));
            return !parsed && parsed.error().code == ErrorCode::InvalidDocument;
        };
        QVERIFY(rejects(QJsonValue::Null));
        QVERIFY(rejects(QJsonArray{}));
        for (const QString& field : {QStringLiteral("burnIn"), QStringLiteral("fontPercent"),
                                     QStringLiteral("background"), QStringLiteral("cues")}) {
            QJsonObject bad = valid;
            bad.remove(field);
            QVERIFY(rejects(bad));
            bad = valid;
            bad[field] = QStringLiteral("wrong type");
            QVERIFY(rejects(bad));
        }
        const QJsonObject validCue = valid[QStringLiteral("cues")].toArray()[0].toObject();
        const auto rejectCue = [&](const QJsonValue& value) {
            QJsonObject bad = valid;
            bad[QStringLiteral("cues")] = QJsonArray{value};
            return rejects(bad);
        };
        QVERIFY(rejectCue(QJsonValue::Null));
        for (const QString& field : {QStringLiteral("id"), QStringLiteral("startMs"),
                                     QStringLiteral("endMs"), QStringLiteral("text")}) {
            QJsonObject bad = validCue;
            bad.remove(field);
            QVERIFY(rejectCue(bad));
            bad = validCue;
            bad[field] = true;
            QVERIFY(rejectCue(bad));
        }
        for (const auto& entry : std::vector<std::pair<QString, QJsonValue>>{
                 {QStringLiteral("startMs"), -1},
                 {QStringLiteral("startMs"), 0.5},
                 {QStringLiteral("endMs"), 0},
                 {QStringLiteral("endMs"), static_cast<double>(kMaxSubtitleTimeMs + 1)},
                 {QStringLiteral("text"), QStringLiteral("a\n\nb")},
                 {QStringLiteral("text"), QString(kMaxSubtitleTextLength + 1, QLatin1Char('a'))}}) {
            QJsonObject bad = validCue;
            bad[entry.first] = entry.second;
            QVERIFY(rejectCue(bad));
        }
        QJsonObject bad = valid;
        bad[QStringLiteral("cues")] = QJsonArray{validCue, validCue};
        QVERIFY(rejects(bad));
        QJsonArray oversized;
        for (int i = 0; i <= kMaxSubtitleCues; ++i)
            oversized.append(validCue);
        bad[QStringLiteral("cues")] = oversized;
        QVERIFY(rejects(bad));
        for (int font : {1, 11}) {
            bad = valid;
            bad[QStringLiteral("fontPercent")] = font;
            QVERIFY(rejects(bad));
        }
        document[QStringLiteral("version")] = project_format::kCurrentVersion + 1;
        QVERIFY(!project_format::fromJson(document));
    }
};

QTEST_GUILESS_MAIN(SubtitleTest)
#include "tst_subtitles.moc"
