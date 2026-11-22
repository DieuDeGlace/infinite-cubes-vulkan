# infinite-cubes-vulkan

A Vulkan 1.3 port of the Steam release of Cube World for Windows x64 and Android ARM64, based on [coremaze/Infinite-Cubes](https://github.com/coremaze/Infinite-Cubes). Both builds use the same game and renderer sources. You need your own copy of the Steam game for its data files.

## Play

Download the EXE or APK from the [latest release](https://github.com/DieuDeGlace/infinite-cubes-vulkan/releases/latest).

| Platform | Setup |
| --- | --- |
| Windows x64 | Back up your saves and the original `cubeworld.exe`. Put the downloaded EXE in Cube World's Steam folder, name it `cubeworld.exe`, and launch Cube World from Steam. Keep the installed `steam_api64.dll` beside it. Co-op players should use the same build and world settings. |
| Android ARM64 | Install `infinite-cubes-vulkan.apk` on Android 13 or newer with Vulkan 1.3. Keep your Steam game data together in Documents. At first launch, choose the folder containing `resource1.dat` and `sprites/`. Android plays locally with touch controls. |

The port includes optional Alpha-style relief and larger biomes, moving clouds and leaves, shadows, global illumination, dual-tone hair, quests, and travel skills. See the [Windows build and game settings](release/README.md) and the [Android build, required files and controls](release/android/README.md).

## Original Infinite Cubes project

Everything you need to make Cube World your own.

| directory | builds |
| --- | --- |
| [plasma/](plasma/README.md) | plasma.exe, the Plasma vector graphics editor (x86) |
| [alpha/](alpha/README.md) | Cube.exe and Server.exe of Cube World Alpha (x86) |
| [release/](release/README.md) | cubeworld.exe, Cube World 1.0.0-1 (x64) |

You MUST already own a copy of the respective software to use it, and you must
use your own copy to provide assets for the software to work. You can purchase
Cube World's Steam release [here](https://store.steampowered.com/app/1128000/Cube_World/).

The code provided is roughly 95% validated, and the remaining 5% may contain
bugs not found in the original software. Open issues for these to be fixed;
pull requests will not be accepted. Once fixed, you can pull the changes into
your own fork.

Bugs originally found in the software still exist in this source code, and
fixing them is out of scope for this project.
