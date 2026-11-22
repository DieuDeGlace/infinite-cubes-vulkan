#pragma once
// CUBE_SCRIPTING: whether the build has the interface script engine, abstr::Machine and cube::System
// (GameController's constructor has it check db.dat). The script's values are 32-bit ints; buffers,
// files, adapters and variable references reach it as ids and stack slots, never as pointers, so it
// runs in x64 builds too. /DCUBE_SCRIPTING=0 leaves it out of a build.
#ifndef CUBE_SCRIPTING
#define CUBE_SCRIPTING 1
#endif
