# Cube World

`cubeworld.exe` of Cube World 1.0.0-1, Windows x64. The game renderer and Plasma UI use Vulkan 1.3.

## Requirements

- CMake 3.20 or newer
- Python 3
- One of:
  - Linux: MinGW-w64 x86_64 GCC with posix threads (`gcc-mingw-w64-x86-64-posix`, `g++-mingw-w64-x86-64-posix`) and Ninja
  - Windows: Visual Studio 2022 with the C++ workload and the Windows SDK
- A Vulkan 1.3-capable GPU and driver to run the game
- Internet access at the first configure for FreeImage, Vulkan Headers, volk, Vulkan Memory Allocator, GLM and the SPIR-V shader compiler

## Build

MinGW (Linux):

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-x86_64.cmake
cmake --build build
```

Visual Studio 2022 (developer command prompt):

```bat
cmake -S . -B build-msvc -G "Visual Studio 17 2022" -A x64
cmake --build build-msvc --config Release
```

Options:

| option | default | effect |
| --- | --- | --- |
| `-DSTEAMWORKS_SDK=DIR` | none | link the real Steamworks SDK (put its `steam_api64.dll` next to the executable); without it, a built-in offline stand-in is used and the game is local only |
| `-DDXC_EXECUTABLE=PATH` | detected DXC or pinned download | compile the HLSL to embedded SPIR-V with the specified DXC |

Output: `build/cubeworld.exe` (or `build-msvc/Release/cubeworld.exe`).

## Run

Copy the data from your own installation next to the executable:
- `logo.bmp`, the `.plx` files, `resource1.dat`, `resource2.dat`, the `dict_*.xml` files;
- `sprites/`, `sounds/`, `music/`.

Create an empty `Save/` directory and launch `cubeworld.exe` from that folder. For a separate local copy, use `run-vulkan/` under this directory for the executable, assets and saves.

For the local offline test, create a world named `world` with seed `123`. Those values match the world that the upstream offline build starts, so its terrain and creatures are available during the test.

Fullscreen uses the monitor's current display mode in a borderless window. Windowed mode uses the selected resolution, subject to the monitor's work area.

## CWVulkan experiments

The first experiment places the two stock lily pads on warm, shallow river and lake surfaces. The current world seed fixes the eight-block patches and each pad's model and rotation. Placement is cached with the water mesh and uses its light data. Set `waterLilies 0` in `options.cfg` to disable drawing the pads.

In **Options**, turn on **Alpha Terrain (restart)** and restart, or set `alphaTerrain 1` in `options.cfg` before launching, to use the Alpha terrain profile. Alpha's elevation noise from CWVulkan Steam (seed `1570745101`) shapes the land between Release waterways, roads and landmarks. Release's elevation and biome blend keep the Alpha relief gentler in savannahs and stronger in mountains. The relief samples Alpha's noise with its lattice cells found by floor, so it stays seamless at negative coordinates as well as at Alpha's positive ones. Near roads, places, landmarks and river, lava and poison beds the relief keeps their grade and climbs away from them at a bounded slope, so tall hills ramp down instead of ending in straight cliffs. Trees stay on gentle slopes but are left out where the ground beside them would bury their crowns, or where they stand so close to a neighbouring tree that the crowns would merge. The combined elevation sets the voxel heights. The altered ridges use local stone on steep faces. Outside deserts, biome grass on gentle ground blends into stone as slopes steepen. Release places trees, rocks and structures. The map uses the same relief and surface materials. Alpha characters and world state use the existing `Save/AlphaTerrain-v1/` profile. Co-op players using Alpha terrain need the same terrain version.

In Options, set **Biome Size (restart)** to **1.5x**, apply the change and restart. The equivalent setting in `options.cfg` is `largerBiomes 1`. The CWVulkan-style scale expands each biome grid cell from 8,192 to 12,288 blocks per side and scales the biome borders. The grids for places, roads and block detail keep their original scale.

The 1.5x setting uses `Save/Biomes150-v1/` with Release terrain or `Save/AlphaTerrain-v1-Biomes150/` with Alpha terrain. The default 1.0x setting opens the existing saves.

`pyroProgression 1` enables the CWVulkan Steam progression rules. You start at level 1 and need 54 XP for level 2. Enemies and town quests award XP. Leveling restores health and raises health, attack, armor and critical chance. Enemy levels follow the player's level and biome difficulty in solo play. In co-op the first player to enter sets the region's difficulty until everyone leaves. Gear drops keep a level, and the tooltip shows the level required to equip them. Characters keep their XP and level in the active save profile. Set `pyroProgression 0` in `options.cfg` and restart to use the original Steam progression. [CWVulkan](https://github.com/DieuDeGlace/CWVulkan)

`aquaticEnemies 1` adds the CubeMegaMod aquatic encounters to generated water. Schools of two or three fish can include passive fish or predators. Freshwater can also contain crab, spitter or slime encounters. Deep water can contain larger predators, including sharks, maw fish and piranhas. The spawner checks the generated water depth and clearance and the creatures use the world's normal streaming, combat and respawn. Set `aquaticEnemies 0` and restart to disable the added population. [CubeMegaMod aquatic rules](https://github.com/DieuDeGlace/CubeMegaMod/tree/91be89fcf8e64aa27e6d1c15b9e02084b12adbb4/src/cwsdk-extension/helper)

Steam co-op requires matching builds and the same terrain, biome size, progression and aquatic settings.

CWVulkan-style villager requests are always on. Some villagers give you a request when you talk to them, aimed at an enemy group outside the village. The HUD and map show the targets, progress and where to return, and the map marks the villagers that have a request to offer. Villagers in a shop or beside a shopkeeper give none, since talking there opens the shop. Up to three requests are active at once. After the group is defeated, talk to the giver again for 20 gold per target and a rare piece of gear. The request and its reward state are saved with the character.

The graphics experiments can be configured in `options.cfg`. In the game, open **Options**, change **Shadows** or **Global Illumination**, then choose **Apply** or **OK**. The game applies and saves both settings immediately.

| Setting | Effect |
| --- | --- |
| `alphaLighting 1` | Alpha daylight, biome palettes, terrain corner lighting, haze, and water shading. In the game, toggle **Alpha Lighting (restart)** in **Options**; it applies at the next start. |
| `sunShadows 1` | A 3,072-texel directional depth map with surface-aware filtering for terrain and opaque models. Shadows soften at the edge and fade between 28 and 70 blocks from the camera. |
| `movingLeaves 1` | Tree leaves sway and flutter in the wind, with their shadows. On by default; toggle **Moving Leaves** in **Options**, which applies at once. |
| `rsmGI 1` | Optional diffuse sunlight bounce from nearby solid terrain. Uses a 512 × 512 reflective shadow map and fades out by 50 blocks. Defaults to off. |
| `temporalAA 1` | Eight subpixel camera samples resolve to a fixed pixel grid. Depth checks match the previous camera ray and history accumulates more strongly while the camera is still. |
| `spatialAA 1` | SMAA Ultra edge detection, blend weights, and neighborhood blending using the bundled SMAA reference textures. |

TAA resolves the world before SMAA. The map, HUD, and menus draw after both passes. When either option is on, the game uses a single-sample scene target. With both options off, `antiAliasingSamples` controls MSAA.

The sun shadow range, distance fade and warm direct light follow the Omega-inspired Steam preset. [MoonandShadow](https://github.com/DieuDeGlace/MoonandShadow)

The bounced light gathers from the depth and material colors of sunlit terrain in world space. The sampling pattern stays fixed across frames. [Photon-LIGHT](https://github.com/Bonbox562/Photon-LIGHT) · [Reflective Shadow Maps](https://users.soe.ucsc.edu/~pang/160/s13/proposal/mijallen/proposal/media/p203-dachsbacher.pdf)
