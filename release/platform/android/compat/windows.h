// The small part of the Win32 API the game code uses, implemented on the C++ standard library for Android.
#pragma once

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

using BYTE = std::uint8_t;
using WORD = std::uint16_t;
using DWORD = std::uint32_t;
using UINT = unsigned int;
using LONG = std::int32_t;
using BOOL = int;
using HANDLE = void *;
using UINT_PTR = std::uintptr_t;

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif
#define __stdcall
#define __cdecl
#define WINAPI
#define CALLBACK

// Virtual-key codes read by the chat, the text fields and the inventory.
#define VK_BACK 0x08
#define VK_TAB 0x09
#define VK_RETURN 0x0D
#define VK_SHIFT 0x10
#define VK_CONTROL 0x11
#define VK_ESCAPE 0x1B
#define VK_SPACE 0x20
#define VK_END 0x23
#define VK_HOME 0x24
#define VK_LEFT 0x25
#define VK_UP 0x26
#define VK_RIGHT 0x27
#define VK_DOWN 0x28
#define VK_DELETE 0x2E
#define VK_F1 0x70
#define VK_F2 0x71
#define VK_F3 0x72
#define VK_F4 0x73
#define VK_F5 0x74
#define VK_F6 0x75

#define THREAD_PRIORITY_BELOW_NORMAL (-1)
#define THREAD_PRIORITY_NORMAL 0

// Critical sections are recursive locks. The standard mutex needs no explicit setup or teardown.
struct CRITICAL_SECTION {
    std::recursive_mutex mutex;
};

inline BOOL InitializeCriticalSectionAndSpinCount(CRITICAL_SECTION *, DWORD)
{
    return TRUE;
}
inline void InitializeCriticalSection(CRITICAL_SECTION *) {}
inline void DeleteCriticalSection(CRITICAL_SECTION *) {}
inline void EnterCriticalSection(CRITICAL_SECTION *section)
{
    section->mutex.lock();
}
inline BOOL TryEnterCriticalSection(CRITICAL_SECTION *section)
{
    return section->mutex.try_lock() ? TRUE : FALSE;
}
inline void LeaveCriticalSection(CRITICAL_SECTION *section)
{
    section->mutex.unlock();
}

// Milliseconds since the first call, wrapping like the Windows multimedia timer.
inline DWORD timeGetTime()
{
    static const auto start = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::steady_clock::now() - start;
    return static_cast<DWORD>(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
}
inline UINT timeBeginPeriod(UINT)
{
    return 0;
}
inline UINT timeEndPeriod(UINT)
{
    return 0;
}

inline void Sleep(DWORD milliseconds)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

// Android schedules the worker threads itself; the game's priority hints are not applied.
inline HANDLE GetCurrentThread()
{
    return nullptr;
}
inline BOOL SetThreadPriority(HANDLE, int)
{
    return TRUE;
}

inline BOOL DeleteFileA(const char *path)
{
    std::string file(path);
    for (char &character : file)
        if (character == '\\')
            character = '/';
    return std::remove(file.c_str()) == 0;
}

// The bounded string copy of the Microsoft C runtime: an empty destination when the source does not fit.
inline int strcpy_s(char *destination, std::size_t size, const char *source)
{
    if (std::strlen(source) >= size) {
        if (size)
            destination[0] = '\0';
        return 1;
    }
    std::strcpy(destination, source);
    return 0;
}
