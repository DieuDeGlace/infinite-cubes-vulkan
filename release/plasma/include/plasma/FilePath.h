// The file name a standard stream opens for a path that the engine keeps as a wide string.
#pragma once
#include <algorithm>
#include <filesystem>
#include <string>

namespace plasma {

inline std::filesystem::path FilePath(const std::wstring &path)
{
#ifdef _WIN32
    return path;
#else
    // Windows accepts both separators; other systems only the forward slash.
    std::wstring portable(path);
    std::replace(portable.begin(), portable.end(), L'\\', L'/');
    return portable;
#endif
}

}
