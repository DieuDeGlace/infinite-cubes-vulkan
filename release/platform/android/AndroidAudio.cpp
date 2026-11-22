#include "AndroidAudio.h"
#include <aaudio/AAudio.h>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vorbis/vorbisfile.h>
#include "AndroidGameData.h"

namespace cube {
namespace {

constexpr size_t MaximumVoices = 48;
// Decoded music kept ahead of playback, in frames (about three quarters of a second).
constexpr size_t MusicReserve = 32768;

std::uint32_t readUnsigned(const unsigned char *bytes, int count)
{
    std::uint32_t value = 0;
    for (int i = 0; i < count; ++i)
        value |= std::uint32_t(bytes[i]) << (8 * i);
    return value;
}

short clampSample(float value)
{
    return static_cast<short>(std::clamp(value, -32768.0f, 32767.0f));
}

aaudio_data_callback_result_t fillOutput(AAudioStream *, void *engine, void *output, int32_t frames)
{
    static_cast<AndroidAudio *>(engine)->mix(static_cast<short *>(output), frames);
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

}

// One streamed Ogg Vorbis file. The decoder thread keeps `pending` filled; the mixer consumes it.
struct AndroidAudio::Music {
    ~Music() { close(); }

    void close()
    {
        if (open)
            ov_clear(&file);
        open = false;
        playing = false;
        pending.clear();
        consumed = 0;
        position = 0.0;
    }

    bool start(const std::string &fileName, bool repeat)
    {
        close();
        auto handle = android::openGameData(fileName);
        if (!handle || ov_open(handle.get(), &file, nullptr, 0) < 0)
            return false;
        handle.release(); // ov_clear closes the stream after a successful ov_open.
        const vorbis_info *info = ov_info(&file, -1);
        channels = info->channels;
        rate = static_cast<int>(info->rate);
        open = true;
        playing = true;
        ended = false;
        loop = repeat;
        return true;
    }

    // Decodes until the reserve is full or the stream ends.
    void decode()
    {
        if (!open || ended)
            return;
        // Drop what the mixer has already played.
        if (consumed > MusicReserve) {
            pending.erase(pending.begin(), pending.begin() + consumed * channels);
            consumed = 0;
        }
        char buffer[8192];
        int section = 0;
        while (pending.size() / channels - consumed < MusicReserve) {
            const long read = ov_read(&file, buffer, sizeof(buffer), 0, 2, 1, &section);
            if (read > 0) {
                const auto *samples = reinterpret_cast<const short *>(buffer);
                pending.insert(pending.end(), samples, samples + read / 2);
            } else if (read == 0 && loop && ov_pcm_seek(&file, 0) == 0) {
                continue;
            } else if (read != OV_HOLE) {
                ended = true;
                break;
            }
        }
    }

    // Adds this track to the output and advances it.
    void mixInto(short *output, int frames, int outputRate)
    {
        if (!playing)
            return;
        const double step = double(rate) / outputRate;
        const size_t available = pending.size() / channels;
        for (int i = 0; i < frames; ++i) {
            const size_t frame = consumed + size_t(position);
            if (frame >= available) {
                // Out of decoded audio: the end of the track, or the decoder is still catching up.
                playing = !ended;
                break;
            }
            const short *source = &pending[frame * channels];
            const float left = source[0] * volume;
            const float right = source[channels > 1 ? 1 : 0] * volume;
            output[i * 2] = clampSample(output[i * 2] + left);
            output[i * 2 + 1] = clampSample(output[i * 2 + 1] + right);
            position += step;
        }
        consumed += size_t(position);
        position -= size_t(position);
    }

    OggVorbis_File file;
    bool open = false;
    bool playing = false;
    bool ended = false;
    bool loop = false;
    int channels = 2;
    int rate = 44100;
    float volume = 1.0f;
    std::vector<short> pending; // interleaved decoded samples
    size_t consumed = 0;        // frames of `pending` already played
    double position = 0.0;      // fractional frame within the next one
};

AndroidAudio::AndroidAudio()
{
    for (auto &music : m_music)
        music = std::make_unique<Music>();
}

AndroidAudio::~AndroidAudio()
{
    m_running = false;
    if (m_decoder.joinable())
        m_decoder.join();
    if (m_stream) {
        AAudioStream_requestStop(m_stream);
        AAudioStream_close(m_stream);
    }
}

bool AndroidAudio::initialize()
{
    AAudioStreamBuilder *builder = nullptr;
    if (AAudio_createStreamBuilder(&builder) != AAUDIO_OK)
        return false;
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setChannelCount(builder, 2);
    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setDataCallback(builder, fillOutput, this);
    const aaudio_result_t opened = AAudioStreamBuilder_openStream(builder, &m_stream);
    AAudioStreamBuilder_delete(builder);
    if (opened != AAUDIO_OK) {
        m_stream = nullptr;
        return false;
    }
    m_rate = AAudioStream_getSampleRate(m_stream);
    m_running = true;
    m_decoder = std::thread(&AndroidAudio::decodeMusic, this);
    return AAudioStream_requestStart(m_stream) == AAUDIO_OK;
}

void AndroidAudio::setPaused(bool paused)
{
    if (!m_stream)
        return;
    if (paused)
        AAudioStream_requestPause(m_stream);
    else
        AAudioStream_requestStart(m_stream);
}

void AndroidAudio::decodeMusic()
{
    while (m_running) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (auto &music : m_music)
                music->decode();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

void AndroidAudio::mix(short *output, int frames)
{
    std::memset(output, 0, sizeof(short) * 2 * frames);
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto &music : m_music)
        music->mixInto(output, frames, m_rate);

    for (auto &voice : m_voices) {
        const Sound &sound = *voice.sound;
        const size_t length = sound.samples.size() / sound.channels;
        for (int i = 0; i < frames && size_t(voice.position) < length; ++i, voice.position += voice.step) {
            const short *source = &sound.samples[size_t(voice.position) * sound.channels];
            const float left = source[0] * voice.volume;
            const float right = source[sound.channels > 1 ? 1 : 0] * voice.volume;
            output[i * 2] = clampSample(output[i * 2] + left);
            output[i * 2 + 1] = clampSample(output[i * 2 + 1] + right);
        }
    }
    m_voices.erase(std::remove_if(m_voices.begin(), m_voices.end(),
                                  [](const Voice &voice) {
                                      return size_t(voice.position) >=
                                             voice.sound->samples.size() / voice.sound->channels;
                                  }),
                   m_voices.end());
}

bool AndroidAudio::playMusic(const std::string &fileName, const std::string &secondFileName, bool loop)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (secondFileName.empty())
        m_music[1]->close();
    else
        m_music[1]->start(secondFileName, loop);
    return m_music[0]->start(fileName, loop);
}

void AndroidAudio::stopMusic()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto &music : m_music)
        music->close();
}

bool AndroidAudio::isMusicPlaying()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_music[0]->playing;
}

void AndroidAudio::setMusicVolume(float volume, float secondVolume)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_music[0]->volume = volume;
    m_music[1]->volume = secondVolume;
}

// Like XAudio2Engine, pan and flag are not used.
void AndroidAudio::play(int sound, float volume, float, float frequencyRatio, bool)
{
    if (!m_stream || volume <= 0.0f)
        return;
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto found = m_sounds.find(sound);
    if (found == m_sounds.end() || m_voices.size() >= MaximumVoices)
        return;
    const double step = double(found->second->rate) / m_rate * std::max(frequencyRatio, 0.01f);
    m_voices.push_back({found->second.get(), 0.0, step, volume});
}

int AndroidAudio::soundId(const std::string &name)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto found = m_soundIds.find(name);
        if (found != m_soundIds.end() && found->second != 0)
            return found->second;
    }

    // RIFF: "RIFF" size "WAVE", then chunks of a four-character tag, a size and the data, padded to even sizes.
    const auto data = android::readGameData(name);
    if (data.size() < 12 || std::memcmp(data.data(), "RIFF", 4) != 0 || std::memcmp(data.data() + 8, "WAVE", 4) != 0)
        return 0;
    auto sound = std::make_unique<Sound>();
    int bitsPerSample = 0;
    bool hasFormat = false;
    for (size_t offset = 12; offset + 8 <= data.size();) {
        const unsigned char *chunk = data.data() + offset;
        const size_t size = std::min<size_t>(readUnsigned(chunk + 4, 4), data.size() - offset - 8);
        if (std::memcmp(chunk, "fmt ", 4) == 0 && size >= 16) {
            sound->channels = static_cast<int>(readUnsigned(chunk + 10, 2));
            sound->rate = static_cast<int>(readUnsigned(chunk + 12, 4));
            bitsPerSample = static_cast<int>(readUnsigned(chunk + 22, 2));
            hasFormat = readUnsigned(chunk + 8, 2) == 1 && sound->channels >= 1 && sound->rate > 0;
        } else if (std::memcmp(chunk, "data", 4) == 0 && hasFormat) {
            const unsigned char *samples = chunk + 8;
            if (bitsPerSample == 16) {
                sound->samples.resize(size / 2);
                for (size_t i = 0; i < sound->samples.size(); ++i)
                    sound->samples[i] = static_cast<short>(readUnsigned(samples + i * 2, 2));
            } else if (bitsPerSample == 8) {
                sound->samples.resize(size);
                for (size_t i = 0; i < size; ++i)
                    sound->samples[i] = static_cast<short>((samples[i] - 128) << 8);
            }
            break;
        }
        offset += 8 + size + (size & 1);
    }
    if (sound->samples.empty())
        return 0;

    std::lock_guard<std::mutex> lock(m_mutex);
    m_soundIds[name] = ++m_lastSoundId;
    m_sounds[m_lastSoundId] = std::move(sound);
    return m_lastSoundId;
}

}
