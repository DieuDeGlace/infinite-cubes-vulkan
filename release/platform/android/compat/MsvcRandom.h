// The Microsoft C runtime's rand and srand. The game seeds rand to generate zones, missions and shop stock, so the
// Android build needs the same sequence to produce the worlds the Windows build produces. Included before every C++
// source of the game.
#pragma once

#include <cstdlib>
#include <stdlib.h>

#undef RAND_MAX
#define RAND_MAX 0x7fff

// Like the Microsoft runtime, every thread has its own sequence, seeded with 1.
inline unsigned &msvcRandomSeed()
{
    static thread_local unsigned seed = 1;
    return seed;
}

inline void msvcSeedRandom(unsigned seed)
{
    msvcRandomSeed() = seed;
}

inline int msvcRandom()
{
    unsigned &seed = msvcRandomSeed();
    seed = seed * 214013u + 2531011u;
    return static_cast<int>((seed >> 16) & RAND_MAX);
}

// Callers that write std::rand reach the same functions.
namespace std {
using ::msvcRandom;
using ::msvcSeedRandom;
}

#define rand msvcRandom
#define srand msvcSeedRandom
