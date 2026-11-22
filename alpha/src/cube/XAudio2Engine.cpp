// XAudio2Engine plays sounds and music. The RIFF chunk helpers read either files or database blobs.
#include <windows.h>
#include "XAudio2_7.h"
#include "cube/XAudio2Engine.h"
#include "cube/BytesIO.h"
#include "cube/Database.h"
#include <algorithm>
#include <fstream>
#include <iterator>
#include <memory>

namespace cube {

// RIFF chunk ids as the little-endian DWORDs read from a file ("RIFF", "data", "fmt ", "WAVE").
constexpr DWORD fourcc(char a, char b, char c, char d)
{
    return DWORD(BYTE(a)) | DWORD(BYTE(b)) << 8 | DWORD(BYTE(c)) << 16 | DWORD(BYTE(d)) << 24;
}
constexpr DWORD fourccRIFF = fourcc('R', 'I', 'F', 'F');
constexpr DWORD fourccDATA = fourcc('d', 'a', 't', 'a');
constexpr DWORD fourccFMT = fourcc('f', 'm', 't', ' ');
constexpr DWORD fourccWAVE = fourcc('W', 'A', 'V', 'E');
static_assert(fourccRIFF == 0x46464952 && fourccWAVE == 0x45564157, "RIFF ids are little-endian");

Music::Music(IXAudio2 *xaudio2)
    : m_xaudio2(xaudio2),
      m_voice(0),
      m_playing(false),
      m_loop(false),
      m_endOfStream(false),
      m_stopPending(false),
      m_bufferIndex(0),
      m_volume(1.0f)
{}

XAudio2Engine::XAudio2Engine()
    : m_xaudio2(0),
      m_masteringVoice(0),
      m_music(0),
      m_secondMusic(0),
      m_database(0),
      m_lastSoundId(0)
{
    m_database = new Database;
    m_database->open("data2.db");
}

XAudio2Engine::~XAudio2Engine()
{
    // Every voice goes before the sounds and music buffers it may still be reading.
    for (IXAudio2SourceVoice *voice : m_voices)
        voice->DestroyVoice();
    m_voices.clear();
    for (std::map<int, Sound *>::iterator it = m_sounds.begin(); it != m_sounds.end(); ++it)
        delete it->second;
    delete m_music;
    delete m_secondMusic;
    if (m_masteringVoice)
        m_masteringVoice->DestroyVoice();
    if (m_xaudio2)
        m_xaudio2->Release();
    delete m_database;
}

namespace {
// Reads a RIFF/WAVE sound's format and samples, walking its chunks as the original loader did (without
// padding odd chunks). False for anything else, or for a chunk that runs past the data.
bool parseWave(const std::vector<char> &bytes, XAudio2Engine::Sound &sound)
{
    const size_t size = bytes.size();
    const auto dword = [&bytes](size_t at) {
        DWORD value;
        memcpy(&value, &bytes[at], sizeof(value));
        return value;
    };
    if (size < 12 || dword(0) != fourccRIFF || dword(8) != fourccWAVE)
        return false;
    bool format = false, samples = false;
    for (size_t at = 12; at + 8 <= size;) {
        const DWORD id = dword(at), length = dword(at + 4);
        const size_t body = at + 8;
        if (length > size - body)
            return false;
        if (id == fourccFMT && length >= 16) { // a PCM format chunk may leave out cbSize
            memset(&sound.format, 0, sizeof(sound.format));
            memcpy(&sound.format, &bytes[body], std::min<size_t>(length, sizeof(sound.format)));
            format = true;
        } else if (id == fourccDATA) {
            sound.data.assign(bytes.begin() + body, bytes.begin() + body + length);
            samples = true;
        }
        at = body + length;
    }
    if (!format || !samples)
        return false;
    memset(&sound.buffer, 0, sizeof(sound.buffer));
    sound.buffer.AudioBytes = UINT32(sound.data.size());
    sound.buffer.pAudioData = sound.data.data();
    sound.buffer.Flags = XAUDIO2_END_OF_STREAM;
    return true;
}
}

void XAudio2Engine::cleanUp()
{
    std::list<std::list<IXAudio2SourceVoice *>::iterator> finished;
    for (std::list<IXAudio2SourceVoice *>::iterator it = m_voices.begin(); it != m_voices.end(); ++it) {
        XAUDIO2_VOICE_STATE state;
        (*it)->GetState(&state);
        if (state.BuffersQueued == 0) {
            (*it)->DestroyVoice();
            finished.push_back(it);
        }
    }
    for (std::list<std::list<IXAudio2SourceVoice *>::iterator>::iterator it = finished.begin(); it != finished.end();
         ++it)
        m_voices.erase(*it);
}

bool XAudio2Engine::initialize()
{
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(XAudio2Create(&m_xaudio2)))
        return false;
    if (FAILED(m_xaudio2->CreateMasteringVoice(&m_masteringVoice)))
        return false;
    m_music = new Music(m_xaudio2);
    m_secondMusic = new Music(m_xaudio2);
    return true;
}

bool XAudio2Engine::isMusicPlaying()
{
    return m_music->isPlaying();
}

void XAudio2Engine::play(int sound, float volume, float pan, float frequencyRatio, bool flag)
{
    if (volume <= 0.0f)
        return;
    Sound *cachedSound = m_sounds[sound];
    if (cachedSound) {
        IXAudio2SourceVoice *voice = 0;
        // A sound whose format the engine refuses (damaged or unreadable data) stays silent.
        if (FAILED(m_xaudio2->CreateSourceVoice(&voice, &cachedSound->format.Format)) || !voice)
            return;
        voice->SetVolume(volume);
        voice->SubmitSourceBuffer(&cachedSound->buffer);
        voice->Start(0);
        voice->SetVolume(volume);
        voice->SetFrequencyRatio(frequencyRatio);
        m_voices.push_back(voice);
    }
}

bool Music::playFile(const std::string &fileName, bool loop)
{
    if (m_voice) {
        stop();
        m_voice->DestroyVoice();
        m_voice = 0; // a file that fails below leaves no destroyed voice for stop() or update()
    }

    FILE *file = fopen(fileName.c_str(), "rb");
    if (!file)
        return false;

    if (ov_open(file, &m_vorbisFile, NULL, 0) < 0) {
        fclose(file); // the stream takes the file only when it opens
        return false;
    }

    vorbis_info *info = ov_info(&m_vorbisFile, -1);

    WAVEFORMATEX format;
    memset(&format, 0, sizeof(format));
    format.cbSize = sizeof(format);
    format.nChannels = info->channels;
    format.wBitsPerSample = 16;
    format.nSamplesPerSec = info->rate;
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nChannels * 2;
    format.nBlockAlign = 2 * format.nChannels;
    format.wFormatTag = WAVE_FORMAT_PCM;

    DWORD position = 0;
    int section = 0;
    int read = 1;

    memset(m_buffers[m_bufferIndex], 0, BufferSize);

    while (read && position < BufferSize) {
        read = ov_read(&m_vorbisFile, (char *)m_buffers[m_bufferIndex] + position, BufferSize - position, 0, 2, 1,
                       &section);
        position += read;
    }

    if (FAILED(m_xaudio2->CreateSourceVoice(&m_voice, &format))) {
        m_voice = 0;
        return false;
    }

    XAUDIO2_BUFFER buffer = {0};
    buffer.pAudioData = m_buffers[m_bufferIndex];
    buffer.AudioBytes = BufferSize;

    if (FAILED(m_voice->SubmitSourceBuffer(&buffer)))
        return false;

    m_bufferIndex++;
    m_bufferIndex %= BufferCount;

    m_voice->Start(0);
    m_voice->SetVolume(m_volume);

    XAUDIO2_VOICE_STATE state;
    m_voice->GetState(&state);
    m_playing = state.BuffersQueued > 0;
    m_loop = loop;
    m_endOfStream = false;
    m_stopPending = false;
    return true;
}

bool XAudio2Engine::playMusic(const std::string &fileName, const std::string &secondFileName, bool loop)
{
    if (secondFileName.empty())
        m_secondMusic->stop();
    else
        m_secondMusic->playFile(secondFileName, loop);
    return m_music->playFile(fileName, loop);
}

void XAudio2Engine::setMusicVolume(float volume, float secondVolume)
{
    m_music->setVolume(volume);
    m_secondMusic->setVolume(secondVolume);
}

int XAudio2Engine::soundId(const std::string &name)
{
    std::map<std::string, int>::iterator found = m_soundIds.find(name);
    if (found != m_soundIds.end() && found->second != 0)
        return found->second;

    // From data2.db (scrambled), or else from a file of that name.
    std::vector<char> bytes;
    if (m_database) {
        BytesIO stream;
        if (!m_database->read(name, stream) || stream.m_data.empty())
            return 0;
        stream.descramble();
        bytes = std::move(stream.m_data);
    } else {
        std::ifstream file(name, std::ios::binary);
        bytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }
    auto sound = std::make_unique<Sound>();
    if (!parseWave(bytes, *sound))
        return 0;
    m_lastSoundId++;
    m_soundIds[name] = m_lastSoundId;
    m_sounds[m_lastSoundId] = sound.release();
    return m_lastSoundId;
}

void XAudio2Engine::stopMusic()
{
    m_music->stop();
    m_secondMusic->stop();
}

void Music::update()
{
    if (m_voice == 0)
        return;

    if (!m_playing)
        return;

    XAUDIO2_VOICE_STATE state;
    m_voice->GetState(&state);
    if (state.BuffersQueued < BufferCount - 1) {
        if (m_stopPending && !m_loop)
            m_voice->Stop(0);

        if (m_endOfStream && !m_loop)
            m_stopPending = true;

        memset(m_buffers[m_bufferIndex], 0, BufferSize);

        DWORD position = 0;
        int section = 0;
        int read = 1;

        while (read && position < BufferSize) {
            read = ov_read(&m_vorbisFile, (char *)m_buffers[m_bufferIndex] + position, BufferSize - position, 0, 2, 1,
                           &section);
            position += read;
        }

        if (!read && m_loop) {
            read = 1;
            ov_pcm_seek(&m_vorbisFile, 0);
            while (read && position < BufferSize) {
                read = ov_read(&m_vorbisFile, (char *)m_buffers[m_bufferIndex] + position, BufferSize - position, 0, 2,
                               1, &section);
                position += read;
            }
        } else if (!read && !m_loop) {
            m_endOfStream = true;
        }

        XAUDIO2_BUFFER buffer = {0};
        buffer.pAudioData = m_buffers[m_bufferIndex];
        if (m_endOfStream)
            buffer.Flags = XAUDIO2_END_OF_STREAM;
        buffer.AudioBytes = BufferSize;

        if (m_voice->SubmitSourceBuffer(&buffer) != S_OK)
            return;

        m_bufferIndex++;
        m_bufferIndex %= BufferCount;
    }
}

void XAudio2Engine::update()
{
    m_music->update();
    m_secondMusic->update();
}

}
