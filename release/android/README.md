# Cube World on Android

The native Android app runs the same game sources as `cubeworld.exe`, with the Vulkan renderer and Plasma UI of the
Windows build. Android supplies the window, touch input, the system keyboard and audio. It is single player: the
build links the offline Steam stand-in, so Steam co-op is not available.

## Requirements

- Java 17 and Gradle 8.11.1
- Android SDK with platform 35, build-tools 35.0.0, CMake 3.22.1 and NDK 30.0.16248370
- Python 3 and Git on the path
- Internet access at the first configure for Vulkan Headers, volk, Vulkan Memory Allocator, GLM, stb_image, the GLU
  tessellator and the SPIR-V shader compiler
- A device running Android 13 or newer (ARM64) with a Vulkan 1.3 driver

## Build

Run `gradle assembleDebug` in this directory. The APK is written to `build/outputs/apk/debug/`.

On Windows, a long checkout path can push the fetched dependencies past the 260-character path limit. Set
`CUBE_NATIVE_BUILD_DIR` to a short directory such as `C:\cw` before building to place the native build there.

## Game data

Keep the Steam Cube World data together in a folder in Documents on the phone. Open the app, tap **Choose folder**,
then select the folder that contains `resource1.dat` and `sprites/`. Tap **Use this folder** and confirm access.
The game reads the files in place and remembers the folder for the next launch. With folder write access, it also
keeps an editable `options.cfg` beside them. In-game options are saved there, and edits made while the app is closed
are loaded on the next launch. An upgrade from read-only access offers the picker once to grant write access.

- `logo.bmp`, the `.plx` files, `resource1.dat`, `resource2.dat`, the `dict_*.xml` files;
- `sprites/`, `sounds/`, `music/`.

An existing setup in `Android/data/org.infinitecubes.vulkan/files/` also works. For USB setup, copy the game data
into that app directory with `adb`.

```sh
adb push run-vulkan/. /sdcard/Android/data/org.infinitecubes.vulkan/files/
```

The app keeps `controls.cfg`, `log_startup.txt`, `Save/` and a fallback copy of `options.cfg` under
`Android/data/org.infinitecubes.vulkan/files/`.

Android hands out a chosen folder's files one at a time, so reading the 3,600 sprite models from it is slow. The
first start keeps a packed copy of them in `cache/sprites.pack` in the same directory, about the size of the
`sprites/` folder, and later starts read the models from that copy. The copy is rebuilt when the files in
`sprites/` change (by name, size or time) or another folder is chosen. Delete the file to rebuild it by hand.
`log_startup.txt` lists how long each startup step took.
The startup dialog shows the current step until the game presents its first frame. Tap **Copy log** on the
startup or error dialog to copy the diagnostic text. Startup steps and errors also appear in the device log
under the tag `CubeWorld`. After a crash, reopen the app once to write `log_crash.txt` in the same directory.
This file contains the app's last unexpected exit and the backtrace or timeout thread dump when Android has one.

The launcher icon uses the [original Steam logo](https://store.steampowered.com/app/1128000/Cube_World/) and the elf
from the [release screenshot](https://www.pcgamer.com/cube-world-release-date/).

## Controls

Each button is labelled in the game's interface font and drawn like its menu buttons. In the world:

- drag on the left side of the screen to move with the stick that appears;
- drag on the right side to turn the camera;
- lower right, largest first: attack, special attack, jump, dodge, use, class skill, ultimate skill, quick item;
- upper right, from the corner: system menu, inventory, crafting, world map, lamp, call pet;
- upper left: zoom in, zoom out.

In menus, touch the interface directly. A second finger holds the right mouse button, which drags the world map.
The menu and zoom buttons stay on screen, and the Back button is Escape. The system keyboard opens while a text
field has the focus.

## Differences from Windows

- The options offer one display mode, the device's screen. A first start without `options.cfg` uses 2x MSAA.
- Enable **Shadows** in **Options** and tap **Apply**. Android uses a 1,536 × 1,536 shadow map with a 56-block range.
- Images are decoded with stb_image instead of FreeImage, and sound is mixed in software for AAudio.
- Leaving the app pauses the game and saves the character. The game continues when the app returns.
- Random numbers, `char` signedness and floating-point contraction follow the Windows build so that a seed
  generates the same world. Standard-library differences can still change details.
