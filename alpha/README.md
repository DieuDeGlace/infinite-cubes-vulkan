# Cube World Alpha for Vulkan

This is the original Infinite Cubes client for Cube World Alpha (2013-07-20), built for Windows x64. The game world and the embedded Plasma interface render through Vulkan 1.3. The dedicated Alpha server also builds for x64.

## Play on Windows

**You must own Cube World Alpha and supply the game files from your own Alpha installation.** Use a separate copy of your Alpha folder for this port. It needs the following files alongside `Cube.exe`.

- `data1.db` through `data4.db` and `db.dat`
- `resource1.dat`, `resource2.dat` and `logo.bmp`
- The original Alpha `.plx` interface files

Launch `Cube.exe` with a Vulkan 1.3 graphics driver. No Visual C++ Redistributable is needed. Sound uses XAudio2 2.7 from the [DirectX End-User Runtime](https://www.microsoft.com/en-us/download/details.aspx?id=8109); without it the game runs silently.

To continue an existing character and world, copy the entire original `Save` folder into this installation. Keep a backup before playing. Village requests save their progress and claimed rewards with the character. Talk to a villager marked with an orange exclamation point, follow the orange question mark to the enemy group, then return to collect the reward. Water uses the Steam port’s wave shading, depth-dependent absorption and refraction.

For an Alpha multiplayer session, launch `Server.exe` from your Alpha data folder. The server listens on TCP port 12345. Enter `q` in its console to shut it down.

## Build from source

Use CMake 3.20 or newer, Python 3, and a Visual Studio 2022 x64 C++ toolchain with the Windows SDK. The first configuration fetches the pinned dependencies and a SPIR-V-capable shader compiler if needed. From the repository root, run

```bat
cmake -S alpha -B build-alpha -G "Visual Studio 17 2022" -A x64
cmake --build build-alpha --config Release --target Cube Server
```

The client and server are built in `build-alpha/Release/`. CMake compiles the HLSL shaders to SPIR-V and embeds them in the client.

For Linux cross-compilation, install Ninja and the x86_64 MinGW-w64 POSIX C and C++ compilers, then run

```sh
cmake -S alpha -B build-alpha -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=../release/cmake/mingw-x86_64.cmake
cmake --build build-alpha --target Cube Server
```
