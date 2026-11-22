#pragma once
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

struct ANativeActivity;

namespace android {

// Set before starting the game workers and clear after they have stopped.
void setGameDataActivity(ANativeActivity *activity);
bool usingGameDataFolder();
bool usingDocumentsOptions();
void writeDocumentsOptions(const std::string &contents);

// A selected folder hands out files one at a time through the document provider, which costs a round trip each. While
// a pack is open, readGameData serves the files under directory from one mapped copy kept at pack. Without a current
// copy, it records each file it reads there instead, and closing the pack keeps that copy for the next start. The
// copy is current while the folder's listing of directory (names, sizes and times) is unchanged. Close with keep
// false when the reading stopped early, so an incomplete copy is not kept.
void openGameDataPack(const std::string &directory, const std::filesystem::path &pack);
void closeGameDataPack(bool keep = true);

using GameDataFile = std::unique_ptr<std::FILE, decltype(&std::fclose)>;
GameDataFile openGameData(const std::filesystem::path &path);
std::vector<unsigned char> readGameData(const std::filesystem::path &path);

class GameDataStream : public std::ifstream {
public:
    GameDataStream() = default;
    explicit GameDataStream(const std::filesystem::path &path, std::ios::openmode mode = std::ios::in)
    {
        open(path, mode);
    }

    void open(const std::filesystem::path &path, std::ios::openmode mode = std::ios::in);
};

}
