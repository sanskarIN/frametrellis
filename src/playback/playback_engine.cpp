// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "playback/playback_engine.h"

#include "core/logging.h"
#include "engine/frame_utils.h"
#include "engine/mlt_profile.h"
#include "engine/mlt_runtime.h"
#include "engine/timeline_graph.h"
#include "playback/audio_output.h"

#include <QMetaObject>
#include <QPointer>
#include <mlt++/Mlt.h>

#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <thread>

namespace ft::playback {

namespace {

using Clock = std::chrono::steady_clock;
constexpr int kAudioFrequency = 48000;
constexpr int kAudioChannels = 2;
/// How far ahead of the speaker the engine renders audio.
constexpr double kAudioLeadSeconds = 0.12;

struct Command
{
    enum class Type {
        SetProject,
        ClearProject,
        Play,
        Pause,
        Seek,
        Step,
        PreviewSize,
        Audio,
        Volume,
        Quit
    };
    explicit Command(Type t)
        : type(t)
    {}
    Type type;
    std::shared_ptr<const Project> project;
    FrameCount frame = 0;
    QSize size;
    bool flag = false;
    double value = 1.0;
    quint64 generation = 0;
};

} // namespace

class PlaybackEngine::Worker
{
public:
    explicit Worker(PlaybackEngine* owner)
        : m_owner(owner)
        , m_thread([this] { run(); })
    {}

    ~Worker()
    {
        post(Command{Command::Type::Quit});
        if (m_thread.joinable()) {
            m_thread.join();
        }
    }

    void post(Command command)
    {
        {
            std::lock_guard lock(m_mutex);
            switch (command.type) {
            case Command::Type::SetProject:
            case Command::Type::ClearProject:
            case Command::Type::Play:
            case Command::Type::Pause:
            case Command::Type::Seek:
            case Command::Type::Step:
            case Command::Type::PreviewSize:
                command.generation = ++m_requestedGeneration;
                break;
            default:
                break;
            }
            if (command.type == Command::Type::Seek) {
                m_owner->m_position = std::clamp<FrameCount>(
                    command.frame, 0, std::max<FrameCount>(m_owner->duration() - 1, 0));
                // Coalesce: only the latest pending seek matters.
                for (auto it = m_queue.begin(); it != m_queue.end();) {
                    it = it->type == Command::Type::Seek ? m_queue.erase(it) : it + 1;
                }
            }
            m_queue.push_back(std::move(command));
        }
        m_cv.notify_one();
    }

    QImage takeFrame(FrameCount* position)
    {
        // The same lock orders request invalidation, publication and delivery. An old
        // queued GUI callback cannot deliver a frame from before a newer seek or edit.
        std::lock_guard commandLock(m_mutex);
        std::lock_guard lock(m_frameMutex);
        m_deliveryPending = false;
        *position = m_framePosition;
        if (!m_frameFresh || m_frameGeneration != m_requestedGeneration) {
            m_frameFresh = false;
            return {};
        }
        m_frameFresh = false;
        return m_frame;
    }

private:
    void run();
    bool applyCommands(std::deque<Command>& commands);
    void rebuild();
    void ensureProfile();
    std::unique_ptr<Mlt::Frame> renderFrame(FrameCount position, QImage* image);
    void publish(const QImage& image, FrameCount position);
    void setPlaying(bool playing);
    /// Waits until the deadline or until a command arrives. Returns false if interrupted.
    bool sleepUntil(Clock::time_point deadline);
    void openAudio();

    PlaybackEngine* m_owner;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<Command> m_queue;
    quint64 m_requestedGeneration = 0; ///< guarded by m_mutex

    std::mutex m_frameMutex;
    QImage m_frame;
    FrameCount m_framePosition = 0;
    bool m_deliveryPending = false;
    bool m_frameFresh = false;
    quint64 m_frameGeneration = 0;

    // Worker-thread state
    std::shared_ptr<const Project> m_project;
    quint64 m_appliedGeneration = 0;
    std::unique_ptr<Mlt::Profile> m_profile;
    std::unique_ptr<engine::TimelineGraphBuilder> m_builder;
    std::unique_ptr<Mlt::Tractor> m_tractor;
    std::unique_ptr<AudioOutput> m_audio;
    QSize m_maxPreview{960, 540};
    QSize m_profileSize;
    FrameRate m_profileRate;
    bool m_needsRebuild = false;
    bool m_needsRender = false;
    bool m_playing = false;
    bool m_audioEnabled = true;
    double m_volume = 1.0;
    FrameCount m_position = 0;
    FrameCount m_duration = 0;
    Clock::time_point m_clockStart;
    FrameCount m_clockStartFrame = 0;

    std::thread m_thread; // last member: starts after everything above is initialised
};

void PlaybackEngine::Worker::openAudio()
{
    if (m_audio) {
        return;
    }
    m_audio = createAudioOutput();
    if (m_audio && !m_audio->open(kAudioFrequency, kAudioChannels)) {
        m_audio.reset();
    }
    m_owner->m_audioAvailable = m_audio != nullptr;
}

bool PlaybackEngine::Worker::sleepUntil(Clock::time_point deadline)
{
    std::unique_lock lock(m_mutex);
    return !m_cv.wait_until(lock, deadline, [this] { return !m_queue.empty(); });
}

void PlaybackEngine::Worker::setPlaying(bool playing)
{
    if (m_playing == playing) {
        return;
    }
    m_playing = playing;
    m_owner->m_playing = playing;
    if (m_audio) {
        m_audio->clear();
    }
    m_clockStart = Clock::now();
    m_clockStartFrame = m_position;
    QMetaObject::invokeMethod(
        m_owner,
        [owner = QPointer<PlaybackEngine>(m_owner)] {
            if (owner) {
                owner->deliverFromWorker();
            }
        },
        Qt::QueuedConnection);
}

void PlaybackEngine::Worker::ensureProfile()
{
    if (!m_project) {
        return;
    }
    const ProjectSettings& settings = m_project->settings();
    const QSize size = engine::fitPreviewSize(QSize(settings.width, settings.height), m_maxPreview);
    if (m_profile && size == m_profileSize && settings.frameRate == m_profileRate) {
        return;
    }
    // Producers are bound to a profile, so a new profile needs a new builder (and cache).
    m_tractor.reset();
    m_builder.reset();
    m_profile = engine::createProfile(size.width(), size.height(), settings.frameRate);
    m_builder = std::make_unique<engine::TimelineGraphBuilder>(*m_profile);
    m_profileSize = size;
    m_profileRate = settings.frameRate;
}

void PlaybackEngine::Worker::rebuild()
{
    m_needsRebuild = false;
    if (!m_project) {
        m_tractor.reset();
        m_duration = 0;
        m_owner->m_duration = 0;
        return;
    }
    ensureProfile();
    Result<engine::GraphBuildResult> built = m_builder->build(*m_project);
    if (!built) {
        m_tractor.reset();
        const QString message = built.error().toString();
        qCWarning(lcPlayback) << "Preview graph failed:" << message;
        QMetaObject::invokeMethod(
            m_owner,
            [owner = QPointer<PlaybackEngine>(m_owner), message] {
                if (owner) {
                    emit owner->errorOccurred(message);
                }
            },
            Qt::QueuedConnection);
        return;
    }
    m_tractor = std::move(built->tractor);
    m_duration = built->duration;
    m_owner->m_duration = m_duration;
    if (m_position >= m_duration) {
        m_position = std::max<FrameCount>(m_duration - 1, 0);
    }
    const QStringList warnings = built->warnings;
    QMetaObject::invokeMethod(
        m_owner,
        [owner = QPointer<PlaybackEngine>(m_owner), warnings] {
            if (owner) {
                emit owner->warningsChanged(warnings);
                owner->deliverFromWorker();
            }
        },
        Qt::QueuedConnection);
    m_needsRender = true;
}

std::unique_ptr<Mlt::Frame> PlaybackEngine::Worker::renderFrame(FrameCount position, QImage* image)
{
    m_tractor->seek(static_cast<int>(position));
    std::unique_ptr<Mlt::Frame> frame(m_tractor->get_frame());
    if (!frame || !frame->is_valid()) {
        return nullptr;
    }
    *image = engine::frameToImage(*frame, m_profileSize);
    ++m_owner->m_renderedFrames;
    return frame;
}

void PlaybackEngine::Worker::publish(const QImage& image, FrameCount position)
{
    bool schedule = false;
    {
        std::lock_guard commandLock(m_mutex);
        if (m_appliedGeneration != m_requestedGeneration) {
            return;
        }
        std::lock_guard lock(m_frameMutex);
        m_frame = image;
        m_framePosition = position;
        m_frameFresh = true;
        m_frameGeneration = m_appliedGeneration;
        schedule = !m_deliveryPending;
        m_deliveryPending = true;
        m_owner->m_position = position;
    }
    if (schedule) {
        QMetaObject::invokeMethod(
            m_owner,
            [owner = QPointer<PlaybackEngine>(m_owner)] {
                if (owner) {
                    owner->deliverFromWorker();
                }
            },
            Qt::QueuedConnection);
    }
}

bool PlaybackEngine::Worker::applyCommands(std::deque<Command>& commands)
{
    for (Command& command : commands) {
        if (command.generation != 0) {
            m_appliedGeneration = command.generation;
        }
        // Commands that depend on the sequence length must see the latest project.
        if (m_needsRebuild &&
            (command.type == Command::Type::Seek || command.type == Command::Type::Step ||
             command.type == Command::Type::Play)) {
            rebuild();
        }
        switch (command.type) {
        case Command::Type::Quit:
            return false;
        case Command::Type::SetProject:
            m_project = std::move(command.project);
            m_needsRebuild = true;
            break;
        case Command::Type::ClearProject:
            setPlaying(false);
            m_project.reset();
            m_position = 0;
            m_needsRebuild = true;
            break;
        case Command::Type::Play:
            if (m_tractor || m_needsRebuild) {
                if (m_position >= m_duration - 1) {
                    m_position = 0;
                }
                setPlaying(true);
            }
            break;
        case Command::Type::Pause: {
            std::lock_guard lock(m_mutex);
            // Playback renders ahead of the displayed frame. Freeze the published
            // position before acknowledging pause; a pending seek/step keeps its target.
            if (!m_needsRender && m_appliedGeneration == m_requestedGeneration) {
                m_position = m_owner->m_position.load();
            }
        }
            setPlaying(false);
            m_needsRender = true;
            break;
        case Command::Type::Seek:
            m_position =
                std::clamp<FrameCount>(command.frame, 0, std::max<FrameCount>(m_duration - 1, 0));
            m_needsRender = true;
            if (m_playing) {
                if (m_audio) {
                    m_audio->clear();
                }
                m_clockStart = Clock::now();
                m_clockStartFrame = m_position;
            }
            break;
        case Command::Type::Step:
            setPlaying(false);
            m_position = std::clamp<FrameCount>(m_position + command.frame, 0,
                                                std::max<FrameCount>(m_duration - 1, 0));
            m_needsRender = true;
            break;
        case Command::Type::PreviewSize:
            if (command.size.isValid() && command.size != m_maxPreview) {
                m_maxPreview = command.size;
                m_needsRebuild = true;
            }
            m_needsRender = true;
            break;
        case Command::Type::Audio:
            m_audioEnabled = command.flag;
            if (m_audio && !m_audioEnabled) {
                m_audio->clear();
            }
            break;
        case Command::Type::Volume:
            m_volume = std::clamp(command.value, 0.0, 2.0);
            break;
        }
    }
    return true;
}

void PlaybackEngine::Worker::run()
{
    openAudio();
    std::deque<Command> commands;
    while (true) {
        {
            std::unique_lock lock(m_mutex);
            m_cv.wait(lock, [this] {
                return !m_queue.empty() || m_playing || m_needsRender || m_needsRebuild;
            });
            commands.swap(m_queue);
        }
        if (!applyCommands(commands)) {
            break;
        }
        commands.clear();
        if (m_needsRebuild) {
            rebuild();
        }
        if (!m_tractor) {
            m_needsRender = false;
            setPlaying(false);
            continue;
        }

        if (!m_playing) {
            if (m_needsRender) {
                m_needsRender = false;
                QImage image;
                if (renderFrame(m_position, &image)) {
                    publish(image, m_position);
                }
            }
            continue;
        }

        if (m_position >= m_duration) {
            m_position = std::max<FrameCount>(m_duration - 1, 0);
            setPlaying(false);
            continue;
        }
        m_needsRender = false;
        QImage image;
        std::unique_ptr<Mlt::Frame> frame = renderFrame(m_position, &image);
        if (!frame) {
            setPlaying(false);
            continue;
        }
        const double fps = m_profileRate.toDouble();
        const bool useAudio = m_audio && m_audioEnabled;
        if (useAudio) {
            engine::AudioBlock audio =
                engine::frameToAudio(*frame, fps, kAudioFrequency, kAudioChannels, m_position);
            if (m_volume != 1.0) {
                for (std::int16_t& s : audio.samples) {
                    s = static_cast<std::int16_t>(std::clamp(s * m_volume, -32768.0, 32767.0));
                }
            }
            // Wait while the device still holds more than the lead time of audio.
            const auto leadFrames = static_cast<std::int64_t>(kAudioLeadSeconds * kAudioFrequency);
            bool interrupted = false;
            while (m_audio->queuedSampleFrames() > leadFrames) {
                if (!sleepUntil(Clock::now() + std::chrono::milliseconds(4))) {
                    interrupted = true;
                    break;
                }
            }
            if (interrupted) {
                continue; // handle the new command first; the frame is rendered again
            }
            // The picture belongs to the audio that starts after what is already queued.
            const std::int64_t queued = m_audio->queuedSampleFrames();
            m_audio->write(audio.samples.data(), audio.sampleFrames());
            const auto displayAt =
                Clock::now() + std::chrono::microseconds(queued * 1'000'000 / kAudioFrequency);
            // Once its audio is queued the frame is always shown; a command that interrupts the
            // wait is handled on the next iteration.
            (void)sleepUntil(displayAt);
        } else {
            const auto offset =
                m_profileRate.framesToMicroseconds(m_position - m_clockStartFrame).value_or(0);
            const auto displayAt = m_clockStart + std::chrono::microseconds(offset);
            if (Clock::now() - displayAt > std::chrono::milliseconds(500)) {
                // Rendering fell far behind (slow machine or debugger): restart the clock.
                m_clockStart = Clock::now();
                m_clockStartFrame = m_position;
            } else if (!sleepUntil(displayAt)) {
                continue;
            }
        }
        publish(image, m_position);
        ++m_position;
    }
    // MLT objects must be released on this thread, before MltRuntime::shutdown().
    m_tractor.reset();
    m_builder.reset();
    m_profile.reset();
    m_project.reset();
    if (m_audio) {
        m_audio->close();
        m_audio.reset();
    }
}

PlaybackEngine::PlaybackEngine(QObject* parent)
    : QObject(parent)
{
    qRegisterMetaType<ft::FrameCount>("ft::FrameCount");
    m_worker = std::make_unique<Worker>(this);
}

PlaybackEngine::~PlaybackEngine()
{
    m_worker.reset();
}

void PlaybackEngine::setProject(const Project& project)
{
    Command c{Command::Type::SetProject};
    c.project = std::make_shared<const Project>(project);
    m_worker->post(std::move(c));
}

void PlaybackEngine::clearProject()
{
    m_worker->post(Command{Command::Type::ClearProject});
}

void PlaybackEngine::play()
{
    m_worker->post(Command{Command::Type::Play});
}

void PlaybackEngine::pause()
{
    m_worker->post(Command{Command::Type::Pause});
}

void PlaybackEngine::togglePlayback()
{
    if (isPlaying()) {
        pause();
    } else {
        play();
    }
}

void PlaybackEngine::seek(FrameCount frame)
{
    Command c{Command::Type::Seek};
    c.frame = frame;
    m_worker->post(std::move(c));
}

void PlaybackEngine::step(int frames)
{
    Command c{Command::Type::Step};
    c.frame = frames;
    m_worker->post(std::move(c));
}

void PlaybackEngine::setMaximumPreviewSize(QSize size)
{
    Command c{Command::Type::PreviewSize};
    c.size = size;
    m_worker->post(std::move(c));
}

void PlaybackEngine::setAudioEnabled(bool enabled)
{
    Command c{Command::Type::Audio};
    c.flag = enabled;
    m_worker->post(std::move(c));
}

void PlaybackEngine::setVolume(double volume)
{
    Command c{Command::Type::Volume};
    c.value = volume;
    m_worker->post(std::move(c));
}

QImage PlaybackEngine::currentFrame() const
{
    return m_displayed;
}

void PlaybackEngine::deliverFromWorker()
{
    FrameCount position = 0;
    QImage image = m_worker->takeFrame(&position);
    if (!image.isNull()) {
        m_displayed = image;
        ++m_deliveredFrames;
        emit frameReady(image, position);
        emit positionChanged(position);
    }
    const FrameCount currentDuration = duration();
    if (currentDuration != m_lastEmittedDuration) {
        m_lastEmittedDuration = currentDuration;
        emit durationChanged(currentDuration);
    }
    const bool playing = isPlaying();
    if (playing != m_lastEmittedPlaying) {
        m_lastEmittedPlaying = playing;
        emit playingChanged(playing);
    }
}

} // namespace ft::playback
