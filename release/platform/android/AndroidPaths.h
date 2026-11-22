// File names as the game writes them (wide or narrow, with Windows separators) converted for Android's file system.
#pragma once

#include <algorithm>
#include <string>

namespace android {

inline std::string hostPath(std::string path)
{
    std::replace(path.begin(), path.end(), '\\', '/');
    return path;
}

// Encodes the path as UTF-8.
inline std::string hostPath(const std::wstring &path)
{
    std::string result;
    for (const wchar_t wide : path) {
        const auto code = static_cast<char32_t>(wide);
        if (code < 0x80) {
            result += static_cast<char>(code);
        } else if (code < 0x800) {
            result += static_cast<char>(0xC0 | (code >> 6));
            result += static_cast<char>(0x80 | (code & 0x3F));
        } else if (code < 0x10000) {
            result += static_cast<char>(0xE0 | (code >> 12));
            result += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (code & 0x3F));
        } else {
            result += static_cast<char>(0xF0 | (code >> 18));
            result += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
            result += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (code & 0x3F));
        }
    }
    return hostPath(std::move(result));
}

}
