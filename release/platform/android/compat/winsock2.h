// The game includes Winsock for Steam networking only; the Android build has no socket code of its own.
#pragma once
#include <unistd.h>
#include <windows.h>

// The game closes its socket member even when it never opened one. Zero is "no socket" on Windows but the standard
// input on Android, so it is left alone.
inline int closesocket(UINT_PTR socket)
{
    return socket ? close(static_cast<int>(socket)) : -1;
}
