// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "playback/audio_output.h"

#include "core/logging.h"

#ifdef FT_HAVE_SDL2
#include <SDL.h>
#include <mutex>
#endif

namespace ft::playback {

#ifdef FT_HAVE_SDL2

namespace {

std::mutex g_sdlMutex;
int g_sdlUsers = 0;

bool acquireSdlAudio()
{
    std::lock_guard lock(g_sdlMutex);
    if (g_sdlUsers == 0) {
        SDL_SetMainReady();
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
            qCWarning(lcPlayback) << "SDL audio initialisation failed:" << SDL_GetError();
            return false;
        }
    }
    ++g_sdlUsers;
    return true;
}

void releaseSdlAudio()
{
    std::lock_guard lock(g_sdlMutex);
    if (g_sdlUsers <= 0) {
        return;
    }
    --g_sdlUsers;
    if (g_sdlUsers == 0) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
}

class SdlAudioOutput final : public AudioOutput
{
public:
    ~SdlAudioOutput() override { close(); }

    bool open(int frequency, int channels) override
    {
        close();
        if (!acquireSdlAudio()) {
            return false;
        }
        m_acquired = true;
        SDL_AudioSpec want{};
        want.freq = frequency;
        want.format = AUDIO_S16SYS;
        want.channels = static_cast<Uint8>(channels);
        want.samples = 1024;
        want.callback = nullptr; // push model via SDL_QueueAudio
        SDL_AudioSpec have{};
        m_device = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
        if (m_device == 0) {
            qCWarning(lcPlayback) << "No audio device available:" << SDL_GetError();
            close();
            return false;
        }
        m_channels = channels;
        m_name = QString::fromUtf8(SDL_GetCurrentAudioDriver());
        SDL_PauseAudioDevice(m_device, 0);
        qCInfo(lcPlayback) << "Audio output opened:" << m_name << frequency << "Hz" << channels
                           << "ch";
        return true;
    }

    void close() override
    {
        if (m_device != 0) {
            SDL_CloseAudioDevice(m_device);
            m_device = 0;
        }
        if (m_acquired) {
            releaseSdlAudio();
            m_acquired = false;
        }
    }

    [[nodiscard]] bool isOpen() const override { return m_device != 0; }

    void write(const std::int16_t* samples, int sampleFrames) override
    {
        if (m_device == 0 || sampleFrames <= 0) {
            return;
        }
        const auto bytes =
            static_cast<Uint32>(sampleFrames * m_channels * static_cast<int>(sizeof(std::int16_t)));
        if (SDL_QueueAudio(m_device, samples, bytes) != 0) {
            qCWarning(lcPlayback) << "Audio queue error:" << SDL_GetError();
        }
    }

    [[nodiscard]] std::int64_t queuedSampleFrames() const override
    {
        if (m_device == 0 || m_channels == 0) {
            return 0;
        }
        return SDL_GetQueuedAudioSize(m_device) /
               (static_cast<Uint32>(m_channels) * sizeof(std::int16_t));
    }

    void clear() override
    {
        if (m_device != 0) {
            SDL_ClearQueuedAudio(m_device);
        }
    }

    [[nodiscard]] QString deviceName() const override { return m_name; }

private:
    SDL_AudioDeviceID m_device = 0;
    int m_channels = 0;
    bool m_acquired = false;
    QString m_name;
};

} // namespace

std::unique_ptr<AudioOutput> createAudioOutput()
{
    return std::make_unique<SdlAudioOutput>();
}

QString audioBackendName()
{
    SDL_version version;
    SDL_GetVersion(&version);
    return QStringLiteral("SDL %1.%2.%3").arg(version.major).arg(version.minor).arg(version.patch);
}

#else

std::unique_ptr<AudioOutput> createAudioOutput()
{
    return nullptr;
}

QString audioBackendName()
{
    return {};
}

#endif

} // namespace ft::playback
