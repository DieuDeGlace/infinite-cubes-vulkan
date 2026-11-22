// AndroidAudio implements Audio on AAudio: cached RIFF/WAVE sounds and two streamed Ogg Vorbis music tracks, mixed
// into one stereo output stream. It is the Android counterpart of XAudio2Engine.
#pragma once

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "cube/Audio.h"

struct AAudioStreamStruct;

namespace cube {

class AndroidAudio : public Audio {
public:
    AndroidAudio();
    ~AndroidAudio() override;

    // Opens and starts the output stream; false when the device offers none.
    bool initialize();
    // Stops or resumes the output while the app is in the background.
    void setPaused(bool paused);

    bool playMusic(const std::string &fileName, const std::string &secondFileName, bool loop) override;
    void stopMusic() override;
    bool isMusicPlaying() override;
    void setMusicVolume(float volume, float secondVolume) override;
    void play(int sound, float volume, float pan, float frequencyRatio, bool flag) override;
    int soundId(const std::string &name) override;

    // Fills frames of interleaved 16-bit stereo. Called on AAudio's thread.
    void mix(short *output, int frames);

private:
    struct Sound {
        int channels = 1;
        int rate = 44100;
        std::vector<short> samples;
    };
    struct Voice {
        const Sound *sound;
        double position; // in source frames
        double step;     // source frames per output frame
        float volume;
    };
    struct Music;

    void decodeMusic();

    AAudioStreamStruct *m_stream = nullptr;
    int m_rate = 44100;
    std::mutex m_mutex; // guards the voices, the loaded sounds and both music tracks
    std::vector<Voice> m_voices;
    std::map<int, std::unique_ptr<Sound>> m_sounds;
    std::map<std::string, int> m_soundIds;
    int m_lastSoundId = 0;
    std::unique_ptr<Music> m_music[2];
    std::thread m_decoder;
    std::atomic<bool> m_running {false};
};

}
