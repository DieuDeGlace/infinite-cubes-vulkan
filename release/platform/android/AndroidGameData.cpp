#include "AndroidGameData.h"
#include <android/log.h>
#include <android/native_activity.h>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <unordered_map>
#include "AndroidJni.h"
#include "AndroidPaths.h"

namespace android {
namespace {

ANativeActivity *s_activity;
jmethodID s_openDocument;
jmethodID s_listing;
jmethodID s_saveOptions;
bool s_usingFolder;
bool s_usingDocumentsOptions;

int openGameDataDescriptor(const std::filesystem::path &path)
{
    const std::string name = hostPath(path.string());
    if (!s_usingFolder || (!name.empty() && name.front() == '/'))
        return ::open(name.c_str(), O_RDONLY | O_CLOEXEC);

    const JniFrame jni(s_activity->vm);
    JNIEnv *env = jni.get();
    if (!env)
        return -1;
    const jstring relative = env->NewStringUTF(name.c_str());
    const int descriptor = relative ? env->CallIntMethod(s_activity->clazz, s_openDocument, relative) : -1;
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return -1;
    }
    return descriptor;
}

// The folder's listing of a directory, or an empty string.
std::string folderListing(const std::string &directory)
{
    if (!s_usingFolder || !s_listing)
        return {};
    const JniFrame jni(s_activity->vm);
    JNIEnv *env = jni.get();
    if (!env)
        return {};
    const jstring path = env->NewStringUTF(directory.c_str());
    const auto text = path ? static_cast<jstring>(env->CallObjectMethod(s_activity->clazz, s_listing, path)) : nullptr;
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return {};
    }
    if (!text)
        return {};
    const char *chars = env->GetStringUTFChars(text, nullptr);
    std::string listing = chars ? chars : "";
    if (chars)
        env->ReleaseStringUTFChars(text, chars);
    return listing;
}

uint64_t fingerprint(std::string_view text)
{
    uint64_t hash = 14695981039346656037ull; // FNV-1a
    for (const unsigned char c : text)
        hash = (hash ^ c) * 1099511628211ull;
    return hash;
}

// The pack file: each file's bytes back to back, then for each file its name's length (4 bytes), name, offset and
// size (8 bytes each), then the footer.
struct PackFooter {
    uint64_t index;
    uint64_t count;
    uint64_t fingerprint;
    char magic[8];
};
constexpr char PackMagic[8] = {'C', 'W', 'P', 'A', 'C', 'K', '0', '1'};

class GameDataPack {
public:
    void open(const std::string &directory, const std::filesystem::path &path)
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        closeLocked(false);
        const std::string listing = folderListing(directory);
        if (listing.empty())
            return;
        m_prefix = directory + "/";
        m_path = path;
        m_fingerprint = fingerprint(listing);
        if (map())
            return;
        // Record a new copy.
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        m_recording = std::fopen((path.string() + ".part").c_str(), "wb");
    }

    void close(bool keep)
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        closeLocked(keep);
    }

    // The packed copy of name, or false where the pack does not hold it.
    bool find(const std::string &name, std::vector<unsigned char> &data)
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_mapped || name.compare(0, m_prefix.size(), m_prefix) != 0)
            return false;
        const auto found = m_index.find(name);
        if (found == m_index.end())
            return false;
        const auto *begin = static_cast<const unsigned char *>(m_mapped) + found->second.first;
        data.assign(begin, begin + found->second.second);
        return true;
    }

    void record(const std::string &name, const std::vector<unsigned char> &data)
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_recording || data.empty() || name.compare(0, m_prefix.size(), m_prefix) != 0 ||
            !m_recorded.emplace(name, std::make_pair(m_written, uint64_t(data.size()))).second)
            return;
        if (std::fwrite(data.data(), 1, data.size(), m_recording) != data.size())
            m_failed = true;
        m_written += data.size();
    }

private:
    bool map()
    {
        const int descriptor = ::open(m_path.c_str(), O_RDONLY | O_CLOEXEC);
        if (descriptor < 0)
            return false;
        struct stat info {};
        void *mapped = MAP_FAILED;
        if (fstat(descriptor, &info) == 0 && info.st_size >= off_t(sizeof(PackFooter)))
            mapped = mmap(nullptr, size_t(info.st_size), PROT_READ, MAP_PRIVATE, descriptor, 0);
        ::close(descriptor);
        if (mapped == MAP_FAILED)
            return false;
        m_mapped = mapped;
        m_size = size_t(info.st_size);
        if (readIndex())
            return true;
        munmap(m_mapped, m_size);
        m_mapped = nullptr;
        m_index.clear();
        return false;
    }

    bool readIndex()
    {
        const auto *bytes = static_cast<const unsigned char *>(m_mapped);
        PackFooter footer;
        std::memcpy(&footer, bytes + m_size - sizeof(footer), sizeof(footer));
        if (std::memcmp(footer.magic, PackMagic, sizeof(PackMagic)) != 0 || footer.fingerprint != m_fingerprint ||
            footer.index > m_size - sizeof(footer))
            return false;
        const size_t end = m_size - sizeof(footer);
        size_t at = size_t(footer.index);
        const auto take = [&](void *out, size_t size) {
            if (size > end - at)
                return false;
            std::memcpy(out, bytes + at, size);
            at += size;
            return true;
        };
        for (uint64_t i = 0; i < footer.count; ++i) {
            uint32_t length;
            uint64_t offset, size;
            if (!take(&length, sizeof(length)) || length > end - at)
                return false;
            std::string name(reinterpret_cast<const char *>(bytes + at), length);
            at += length;
            if (!take(&offset, sizeof(offset)) || !take(&size, sizeof(size)) || offset > footer.index ||
                size > footer.index - offset)
                return false;
            m_index.emplace(std::move(name), std::make_pair(offset, size));
        }
        return at == end;
    }

    void closeLocked(bool keep)
    {
        if (m_recording)
            finishRecording(keep);
        if (m_mapped)
            munmap(m_mapped, m_size);
        m_mapped = nullptr;
        m_index.clear();
        m_recorded.clear();
        m_written = 0;
        m_failed = false;
    }

    void finishRecording(bool keep)
    {
        bool kept = keep && !m_failed && !m_recorded.empty();
        if (kept) {
            PackFooter footer {m_written, m_recorded.size(), m_fingerprint, {}};
            std::memcpy(footer.magic, PackMagic, sizeof(PackMagic));
            for (const auto &[name, place] : m_recorded) {
                const auto length = uint32_t(name.size());
                kept = kept && std::fwrite(&length, sizeof(length), 1, m_recording) == 1 &&
                       std::fwrite(name.data(), 1, name.size(), m_recording) == name.size() &&
                       std::fwrite(&place.first, sizeof(place.first), 1, m_recording) == 1 &&
                       std::fwrite(&place.second, sizeof(place.second), 1, m_recording) == 1;
            }
            kept = kept && std::fwrite(&footer, sizeof(footer), 1, m_recording) == 1;
        }
        kept = std::fclose(m_recording) == 0 && kept;
        m_recording = nullptr;
        const std::string part = m_path.string() + ".part";
        if (kept && std::rename(part.c_str(), m_path.c_str()) == 0)
            return;
        std::remove(part.c_str());
        if (keep)
            __android_log_write(ANDROID_LOG_WARN, "CubeWorld", "Could not keep the packed game data.");
    }

    std::mutex m_mutex;
    std::string m_prefix;
    std::filesystem::path m_path;
    uint64_t m_fingerprint = 0;
    void *m_mapped = nullptr;
    size_t m_size = 0;
    std::unordered_map<std::string, std::pair<uint64_t, uint64_t>> m_index;
    std::FILE *m_recording = nullptr;
    std::unordered_map<std::string, std::pair<uint64_t, uint64_t>> m_recorded;
    uint64_t m_written = 0;
    bool m_failed = false;
};

GameDataPack s_pack;

// The file as the folder or the file system holds it.
std::vector<unsigned char> readFolderData(const std::filesystem::path &path)
{
    const auto file = openGameData(path);
    if (!file)
        return {};

    std::vector<unsigned char> data;
    struct stat info {};
    if (fstat(fileno(file.get()), &info) == 0 && info.st_size > 0)
        data.reserve(static_cast<size_t>(info.st_size));
    unsigned char chunk[16384];
    for (size_t count; (count = std::fread(chunk, 1, sizeof(chunk), file.get())) != 0;)
        data.insert(data.end(), chunk, chunk + count);
    if (std::ferror(file.get()))
        data.clear();
    return data;
}

}

void openGameDataPack(const std::string &directory, const std::filesystem::path &pack)
{
    s_pack.open(directory, pack);
}

void closeGameDataPack(bool keep)
{
    s_pack.close(keep);
}

void setGameDataActivity(ANativeActivity *activity)
{
    s_activity = activity;
    s_openDocument = nullptr;
    s_listing = nullptr;
    s_saveOptions = nullptr;
    s_usingFolder = false;
    s_usingDocumentsOptions = false;
    if (!activity)
        return;

    const JniFrame jni(activity->vm);
    JNIEnv *env = jni.get();
    if (!env)
        throw std::runtime_error("Android could not open the game data service.");
    const jclass type = env->GetObjectClass(activity->clazz);
    const jmethodID hasFolder = type ? env->GetMethodID(type, "hasGameDataFolder", "()Z") : nullptr;
    if (hasFolder) {
        s_openDocument = env->GetMethodID(type, "openGameDataFile", "(Ljava/lang/String;)I");
        // Optional: without it every file is read from the folder.
        s_listing = env->GetMethodID(type, "gameDataListing", "(Ljava/lang/String;)Ljava/lang/String;");
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            s_listing = nullptr;
        }
    }
    const jmethodID writable = s_openDocument ? env->GetMethodID(type, "canWriteOptions", "()Z") : nullptr;
    if (writable)
        s_saveOptions = env->GetMethodID(type, "saveOptions", "(Ljava/lang/String;)Z");
    if (s_saveOptions) {
        s_usingFolder = env->CallBooleanMethod(activity->clazz, hasFolder);
        if (s_usingFolder && !env->ExceptionCheck())
            s_usingDocumentsOptions = env->CallBooleanMethod(activity->clazz, writable);
    }
    if (env->ExceptionCheck() || !s_saveOptions) {
        env->ExceptionClear();
        throw std::runtime_error("Android could not open the game data service.");
    }
}

bool usingGameDataFolder()
{
    return s_usingFolder;
}

bool usingDocumentsOptions()
{
    return s_usingDocumentsOptions;
}

void writeDocumentsOptions(const std::string &contents)
{
    if (!usingDocumentsOptions())
        return;
    const JniFrame jni(s_activity->vm);
    JNIEnv *env = jni.get();
    if (!env)
        return;

    const jstring text = env->NewStringUTF(contents.c_str());
    bool saved = text && env->CallBooleanMethod(s_activity->clazz, s_saveOptions, text);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        saved = false;
    }
    if (!saved)
        __android_log_write(ANDROID_LOG_WARN, "CubeWorld", "Could not write options.cfg in Documents.");
}

GameDataFile openGameData(const std::filesystem::path &path)
{
    const int descriptor = openGameDataDescriptor(path);
    FILE *file = descriptor >= 0 ? fdopen(descriptor, "rb") : nullptr;
    if (!file && descriptor >= 0)
        ::close(descriptor);
    return GameDataFile(file, std::fclose);
}

std::vector<unsigned char> readGameData(const std::filesystem::path &path)
{
    const std::string name = hostPath(path.string());
    std::vector<unsigned char> packed;
    if (s_pack.find(name, packed))
        return packed;
    std::vector<unsigned char> data = readFolderData(path);
    s_pack.record(name, data);
    return data;
}

void GameDataStream::open(const std::filesystem::path &path, std::ios::openmode mode)
{
    if (is_open()) {
        setstate(std::ios::failbit);
        return;
    }
    const int descriptor = openGameDataDescriptor(path);
    if (descriptor < 0) {
        setstate(std::ios::failbit);
        return;
    }

    // The Android NDK's libc++ stream can take ownership of the provider's descriptor without reopening its path.
    __open(descriptor, mode & ~std::ios::ate);
    if (!is_open())
        ::close(descriptor);
    else if (mode & std::ios::ate)
        seekg(0, std::ios::end);
}

}
