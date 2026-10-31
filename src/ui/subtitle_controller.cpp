// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "ui/subtitle_controller.h"

#include "editing/subtitle_edits.h"
#include "ui/playback_controller.h"
#include "ui/project_controller.h"

#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QUrl>

#include <algorithm>
#include <cmath>

namespace ft::ui {
namespace {
Result<QString> subtitlePath(const QString& input, const QString& extension)
{
    const QUrl url(input);
    const bool drive = input.size() >= 3 && input.at(0).isLetter() &&
                       input.at(1) == QLatin1Char(':') &&
                       (input.at(2) == QLatin1Char('/') || input.at(2) == QLatin1Char('\\'));
    if (input.isEmpty() || (!url.scheme().isEmpty() && !url.isLocalFile() && !drive)) {
        return makeError(ErrorCode::InvalidArgument, QStringLiteral("Choose a local .%1 file.").arg(extension));
    }
    const QString path = ProjectController::localPath(input);
    if (QFileInfo(path).suffix().compare(extension, Qt::CaseInsensitive) != 0) {
        return makeError(ErrorCode::InvalidArgument,
                         QStringLiteral("Choose a file with the .%1 extension.").arg(extension));
    }
    return path;
}
} // namespace

SubtitleController::SubtitleController(ProjectController* project, PlaybackController* playback,
                                       QObject* parent)
    : QAbstractListModel(parent)
    , m_project(project)
    , m_playback(playback)
{
    connect(m_project, &ProjectController::documentReplaced, this, [this] { refresh(true); });
    connect(m_project, &ProjectController::projectChanged, this, [this](const ChangeSet& changes) {
        if (changes.subtitles) {
            refresh();
        } else {
            m_activeRow = activeRow();
            ++m_revision;
            if (rowCount() > 0)
                emit dataChanged(index(0), index(rowCount() - 1));
            emit cuesChanged();
            emit stateChanged();
        }
    });
    connect(m_playback, &PlaybackController::positionChanged, this,
            &SubtitleController::onPositionChanged);
    connect(m_playback, &PlaybackController::playingChanged, this,
            &SubtitleController::stateChanged);
    refresh(true);
}

int SubtitleController::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_track.cues.size());
}

QVariant SubtitleController::data(const QModelIndex& modelIndex, int role) const
{
    if (!modelIndex.isValid() || modelIndex.row() < 0 || modelIndex.row() >= rowCount())
        return {};
    const SubtitleCue& cue = m_track.cues[static_cast<size_t>(modelIndex.row())];
    switch (role) {
    case CueIdRole:
        return cue.id;
    case StartLabelRole:
        return formatSubtitleTime(cue.startMs);
    case EndLabelRole:
        return formatSubtitleTime(cue.endMs);
    case CaptionRole:
        return cue.text;
    case ActiveRole:
        return modelIndex.row() == m_activeRow;
    case SelectedRole:
        return cue.id == m_selected;
    case OutsideRole:
        return !cueCanSeek(cue);
    default:
        return {};
    }
}

QHash<int, QByteArray> SubtitleController::roleNames() const
{
    return {{CueIdRole, "cueId"},     {StartLabelRole, "startLabel"}, {EndLabelRole, "endLabel"},
            {CaptionRole, "caption"}, {ActiveRole, "active"},         {SelectedRole, "selected"},
            {OutsideRole, "outside"}};
}

int SubtitleController::selectedIndex() const
{
    return m_rows.value(m_selected, -1);
}

QVariantMap SubtitleController::selectedCue() const
{
    const int row = selectedIndex();
    if (row < 0)
        return {};
    const SubtitleCue& cue = m_track.cues[static_cast<size_t>(row)];
    return {{QStringLiteral("id"), cue.id},
            {QStringLiteral("start"), formatSubtitleTime(cue.startMs)},
            {QStringLiteral("end"), formatSubtitleTime(cue.endMs)},
            {QStringLiteral("text"), cue.text}};
}

void SubtitleController::refresh(bool replaceDocument)
{
    const QVariantMap before = selectedCue();
    const int oldRow = selectedIndex();
    const SubtitleTrack& after = m_project->project().subtitles();
    const bool sameIds =
        !replaceDocument && after.cues.size() == m_track.cues.size() &&
        std::equal(after.cues.begin(), after.cues.end(), m_track.cues.begin(),
                   [](const SubtitleCue& a, const SubtitleCue& b) { return a.id == b.id; });
    if (!sameIds)
        beginResetModel();
    m_track = after;
    m_rows.clear();
    for (int i = 0; i < rowCount(); ++i)
        m_rows.insert(m_track.cues[static_cast<size_t>(i)].id, i);
    if (replaceDocument)
        m_selected.clear();
    if (!m_rows.contains(m_selected)) {
        const int fallback = std::clamp(oldRow, 0, std::max(0, rowCount() - 1));
        m_selected = rowCount() > 0 ? m_track.cues[static_cast<size_t>(fallback)].id : Id{};
    }
    m_activeRow = activeRow();
    if (!sameIds)
        endResetModel();
    else if (rowCount() > 0)
        emit dataChanged(index(0), index(rowCount() - 1));
    ++m_revision;
    setError({});
    emit cuesChanged();
    if (before != selectedCue() || oldRow != selectedIndex())
        emit selectedCueChanged();
    emit stateChanged();
}

int SubtitleController::activeRow() const
{
    if (m_playback->positionFrames() >= m_project->project().timeline().duration())
        return -1;
    const SubtitleCue* cue = subtitleAtFrame(m_track, m_playback->positionFrames(),
                                             m_project->project().settings().frameRate);
    return cue ? m_rows.value(cue->id, -1) : -1;
}

void SubtitleController::onPositionChanged()
{
    const int before = m_activeRow;
    m_activeRow = activeRow();
    if (before >= 0)
        emit dataChanged(index(before), index(before), {ActiveRole});
    if (m_activeRow >= 0 && before != m_activeRow)
        emit dataChanged(index(m_activeRow), index(m_activeRow), {ActiveRole});
    emit stateChanged();
}

bool SubtitleController::cueCanSeek(const SubtitleCue& cue) const
{
    const FrameRate& rate = m_project->project().settings().frameRate;
    const auto first = subtitleFrameAtMilliseconds(cue.startMs, rate);
    const auto end = subtitleFrameAtMilliseconds(cue.endMs, rate);
    return first && end && *end > *first && *first < m_project->project().timeline().duration();
}

bool SubtitleController::canSeek() const
{
    return selectedIndex() >= 0 && cueCanSeek(m_track.cues[static_cast<size_t>(selectedIndex())]);
}

bool SubtitleController::selectCue(const QString& id, bool seek)
{
    if (!m_rows.contains(id))
        return fail(tr("The subtitle was not found."));
    const int before = selectedIndex();
    m_selected = id;
    if (before >= 0)
        emit dataChanged(index(before), index(before), {SelectedRole});
    emit dataChanged(index(selectedIndex()), index(selectedIndex()), {SelectedRole});
    ++m_revision;
    emit cuesChanged();
    emit selectedCueChanged();
    emit stateChanged();
    return !seek || !canSeek() || seekSelected();
}

bool SubtitleController::seekSelected()
{
    if (!canSeek())
        return fail(tr("This cue has no sampled frame inside the current video. Its exact times "
                       "are retained."));
    const auto frame =
        subtitleFrameAtMilliseconds(m_track.cues[static_cast<size_t>(selectedIndex())].startMs,
                                    m_project->project().settings().frameRate);
    if (!frame)
        return fail(tr("The subtitle time could not be converted to a video frame."));
    m_playback->pause();
    m_playback->seek(static_cast<double>(*frame));
    return true;
}

void SubtitleController::previousCue()
{
    const int row = selectedIndex() - 1;
    if (row >= 0)
        (void)selectCue(m_track.cues[static_cast<size_t>(row)].id);
}
void SubtitleController::nextCue()
{
    const int row = selectedIndex() + 1;
    if (row < rowCount())
        (void)selectCue(m_track.cues[static_cast<size_t>(row)].id);
}

std::optional<SubtitleCue> SubtitleController::cueAtPlayhead() const
{
    const auto& project = m_project->project();
    const FrameCount frame = m_playback->positionFrames();
    const auto start = subtitleMillisecondsAtFrame(frame, project.settings().frameRate);
    const auto sequenceEnd =
        subtitleMillisecondsAtFrame(project.timeline().duration(), project.settings().frameRate);
    if (rowCount() >= kMaxSubtitleCues || m_playback->playing() ||
        frame >= project.timeline().duration() || !start || !sequenceEnd ||
        *start >= kMaxSubtitleTimeMs ||
        subtitleAtFrame(m_track, frame, project.settings().frameRate))
        return std::nullopt;
    std::int64_t end = std::min({*start + 2000, *sequenceEnd, kMaxSubtitleTimeMs});
    const auto next = std::upper_bound(
        m_track.cues.begin(), m_track.cues.end(), *start,
        [](std::int64_t time, const SubtitleCue& cue) { return time < cue.startMs; });
    if (next != m_track.cues.end())
        end = std::min(end, next->startMs);
    if (end <= *start)
        return std::nullopt;
    return SubtitleCue{{}, *start, end, tr("New subtitle")};
}

bool SubtitleController::canAdd() const
{
    return cueAtPlayhead().has_value();
}

bool SubtitleController::addAtPlayhead()
{
    auto cue = cueAtPlayhead();
    if (!cue)
        return fail(tr("Pause inside a free subtitle gap in the video to add a cue."));
    cue->id = createId();
    const Id id = cue->id;
    return execute(edits::addSubtitle(*cue)) && selectCue(id, false);
}

bool SubtitleController::updateSelected(const QString& start, const QString& end,
                                        const QString& text)
{
    if (selectedIndex() < 0)
        return fail(tr("Select a subtitle to edit."));
    const auto first = parseSubtitleTime(start.trimmed());
    const auto last = parseSubtitleTime(end.trimmed());
    if (!first)
        return fail(first.error().message);
    if (!last)
        return fail(last.error().message);
    QString caption = text;
    caption.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    caption.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    return execute(edits::updateSubtitle({m_selected, *first, *last, caption}));
}
bool SubtitleController::removeSelected()
{
    return selectedIndex() >= 0 ? execute(edits::removeSubtitle(m_selected))
                                : fail(tr("Select a subtitle to remove."));
}
bool SubtitleController::clearAll()
{
    return execute(edits::clearSubtitles());
}
bool SubtitleController::setAppearance(bool burn, int percent, bool plate)
{
    return execute(edits::setSubtitleAppearance(burn, percent, plate));
}
bool SubtitleController::resetAppearance()
{
    return setAppearance(true, 5, true);
}

bool SubtitleController::importSrt(const QString& input)
{
    const auto path = subtitlePath(input, QStringLiteral("srt"));
    if (!path)
        return fail(path.error().message);
    if (!QFileInfo(*path).isFile())
        return fail(tr("Choose an existing SRT file."));
    QFile file(*path);
    if (!file.open(QIODevice::ReadOnly))
        return fail(tr("Could not open the SRT file: %1").arg(file.errorString()));
    const QByteArray bytes = file.read(kMaxSrtBytes + 1);
    if (file.error() != QFileDevice::NoError)
        return fail(tr("Could not read the SRT file."));
    const auto imported = srt::fromBytes(bytes);
    if (!imported)
        return fail(imported.error().message);
    if (imported->cues.empty())
        return fail(tr("The SRT file contains no subtitle cues."));
    if (!execute(edits::appendSubtitles(imported->cues)))
        return false;
    (void)selectCue(imported->cues.front().id);
    emit statusMessage(tr("Imported %1 subtitle cues.").arg(imported->cues.size()));
    return true;
}

bool SubtitleController::exportSrt(const QString& input)
{
    const auto path = subtitlePath(input, QStringLiteral("srt"));
    if (!path)
        return fail(path.error().message);
    const auto& project = m_project->project();
    if (project.assetByPath(*path) ||
        (!m_project->filePath().isEmpty() &&
         canonicalMediaKey(*path) == canonicalMediaKey(m_project->filePath())))
        return fail(tr("Choose an SRT path separate from the project and its source media."));
    const auto bytes = srt::toBytes(project.subtitles());
    if (!bytes)
        return fail(bytes.error().message);
    QSaveFile file(*path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(*bytes) != bytes->size() || !file.commit())
        return fail(tr("Could not save the SRT file. The previous file was kept."));
    setError({});
    emit statusMessage(tr("Exported %1 subtitle cues.").arg(m_track.cues.size()));
    return true;
}

bool SubtitleController::exportWebVtt(const QString& input)
{
    const auto path = subtitlePath(input, QStringLiteral("vtt"));
    if (!path)
        return fail(path.error().message);
    const auto& project = m_project->project();
    if (project.assetByPath(*path) ||
        (!m_project->filePath().isEmpty() &&
         canonicalMediaKey(*path) == canonicalMediaKey(m_project->filePath())))
        return fail(tr("Choose a WebVTT path separate from the project and its source media."));
    const auto bytes = webvtt::toBytes(project.subtitles());
    if (!bytes)
        return fail(bytes.error().message);
    QSaveFile file(*path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(*bytes) != bytes->size() || !file.commit())
        return fail(tr("Could not save the WebVTT file. The previous file was kept."));
    setError({});
    emit statusMessage(tr("Exported %1 WebVTT subtitle cues.").arg(m_track.cues.size()));
    return true;
}

QString SubtitleController::playheadTimestamp() const
{
    const auto value = subtitleMillisecondsAtFrame(m_playback->positionFrames(),
                                                   m_project->project().settings().frameRate);
    return value ? formatSubtitleTime(*value) : QString();
}

QVariantList SubtitleController::visibleCues(double firstFrame, double lastFrame) const
{
    QVariantList visible;
    if (!std::isfinite(firstFrame) || !std::isfinite(lastFrame) || firstFrame < 0 ||
        lastFrame <= firstFrame || lastFrame > static_cast<double>(kMaxTimelineFrames))
        return visible;
    const FrameRate& rate = m_project->project().settings().frameRate;
    const auto firstMs =
        subtitleMillisecondsAtFrame(static_cast<FrameCount>(std::floor(firstFrame)), rate);
    if (!firstMs)
        return visible;
    auto it = std::lower_bound(
        m_track.cues.begin(), m_track.cues.end(), *firstMs,
        [](const SubtitleCue& cue, std::int64_t time) { return cue.endMs <= time; });
    for (; it != m_track.cues.end(); ++it) {
        const auto first = subtitleFrameAtMilliseconds(it->startMs, rate);
        const auto end = subtitleFrameAtMilliseconds(it->endMs, rate);
        if (!first || !end || *first >= lastFrame)
            break;
        const FrameCount clippedEnd = std::min(*end, m_project->project().timeline().duration());
        if (*first >= clippedEnd || clippedEnd <= firstFrame)
            continue;
        visible.append(QVariantMap{{QStringLiteral("id"), it->id},
                                   {QStringLiteral("text"), it->text},
                                   {QStringLiteral("first"), static_cast<double>(*first)},
                                   {QStringLiteral("end"), static_cast<double>(clippedEnd)},
                                   {QStringLiteral("selected"), it->id == m_selected}});
    }
    return visible;
}

bool SubtitleController::execute(std::unique_ptr<EditCommand> command)
{
    m_project->document()->breakMerge();
    const Status result = m_project->document()->execute(std::move(command));
    if (!result)
        return fail(result.error().message);
    setError({});
    return true;
}
void SubtitleController::setError(const QString& error)
{
    if (m_error == error)
        return;
    m_error = error;
    emit errorChanged();
}
bool SubtitleController::fail(const QString& error)
{
    setError(error);
    emit statusMessage(error);
    return false;
}
} // namespace ft::ui
