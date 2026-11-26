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

In **Options**, turn on **Alpha Terrain (restart)** and choose **Apply** or **OK**, or set `alphaTerrain 1` in `options.cfg` before launching, to use the Alpha terrain profile. Applying a new terrain saves your character and starts the game again with it. Characters are shared by both terrains (and both biome sizes), in `Save/characters.db`: a character from the stock terrain loads into the Alpha terrain at the same place, lifted onto the ground if the new relief would bury it. Alpha's elevation noise from CWVulkan Steam (seed `1570745101`) shapes the land between Release waterways, roads and landmarks. Release's elevation and biome blend keep the Alpha relief gentler in savannahs and stronger in mountains. The relief samples Alpha's noise with its lattice cells found by floor, so it stays seamless at negative coordinates as well as at Alpha's positive ones. Near roads, places, landmarks and river, lava and poison beds the relief keeps their grade and climbs away from them at a bounded slope, so tall hills ramp down instead of ending in straight cliffs. Trees stay on gentle slopes but are left out where the ground beside them would bury their crowns, or where they stand so close to a neighbouring tree that the crowns would merge. The combined elevation sets the voxel heights. The altered ridges use local stone on steep faces. Outside deserts, biome grass on gentle ground blends into stone as slopes steepen. Release places trees, rocks and structures. The map uses the same relief and surface materials. The Alpha terrain's world state (time of day and changed zones) stays in `Save/AlphaTerrain-v1/`. Characters made there by earlier builds are added to the shared list at the first start, and the old file is kept as `characters.db.merged`. Co-op players using Alpha terrain need the same terrain version.

In Options, set **Biome Size (restart)** to **1.5x** and apply the change; like the terrain, it saves your character and starts the game again with it, and your characters come along. The CWVulkan-style scale expands each biome grid cell from 8,192 to 12,288 blocks per side and scales the biome borders, and the places and the roads between them stretch along: their cells grow from 1,024 to 1,536 blocks and every place sits at 1.5 times its 1.0x position, so towns keep their biomes and roads still run between them. Towns and buildings keep their size, and block detail keeps its scale. The equivalent setting in `options.cfg` is `largerBiomes 2`. **1.5x Classic** (`largerBiomes 1`), the first 1.5x layout, stretches only the biomes over the 1.0x places and roads; its worlds keep that layout.

The 1.5x setting keeps its world state in `Save/Biomes150-Places-v1/` with Release terrain or `Save/AlphaTerrain-v1-Biomes150-Places/` with Alpha terrain, and 1.5x Classic in `Save/Biomes150-v1/` or `Save/AlphaTerrain-v1-Biomes150/`; the default 1.0x setting uses `Save/`. The characters are the same for all of them.

`pyroProgression 1` enables the CWVulkan Steam progression rules. You start at level 1 and need 54 XP for level 2. Enemies and town quests award XP. Leveling restores health and raises health, attack, armor and critical chance. Enemy levels follow the player's level and biome difficulty in solo play. In co-op the first player to enter sets the region's difficulty until everyone leaves. Gear drops keep a level, and the tooltip shows the level required to equip them. Characters keep their XP and level. Set `pyroProgression 0` in `options.cfg` and restart to use the original Steam progression. [CWVulkan](https://github.com/DieuDeGlace/CWVulkan)

`spawnDensity 150` (the default) gives newly generated land half again as many wild enemy groups as the original game: the percentage scales the one ground column in 128 that gets a group (50 to 400). It draws the same random numbers, so terrain and everything else generate as before. Areas already generated keep their groups.

`aquaticEnemies 1` adds the CubeMegaMod aquatic encounters to generated water. Schools of two or three fish can include passive fish or predators. Freshwater can also contain crab, spitter or slime encounters. Deep water can contain larger predators, including sharks, maw fish and piranhas. The spawner checks the generated water depth and clearance and the creatures use the world's normal streaming, combat and respawn. Set `aquaticEnemies 0` and restart to disable the added population. [CubeMegaMod aquatic rules](https://github.com/DieuDeGlace/CubeMegaMod/tree/91be89fcf8e64aa27e6d1c15b9e02084b12adbb4/src/cwsdk-extension/helper)

Steam co-op requires matching builds and the same terrain, biome size, progression and aquatic settings.

CWVulkan-style villager requests are always on. Some villagers give you a request when you talk to them, aimed at an enemy group outside the village. The HUD and map show the targets, progress and where to return, and the map marks the villagers that have a request to offer. Villagers in a shop or beside a shopkeeper give none, since talking there opens the shop. Up to three requests are active at once. After the group is defeated, talk to the giver again for 20 gold per target and a rare piece of gear. The request and its reward state are saved with the character.

Overworld encounters, adapted from [OverworldEvents](https://github.com/DieuDeGlace/OverworldEvents) (MIT), appear 160 to 400 blocks away now and then (every two minutes at most, and at least 450 blocks from any other encounter), at most two unfinished at once, marked with an orange question mark on the map. A **battle** sets two adventurers against four to six of the region's own enemies; win, then talk to an adventurer. A **rescue** holds a captive in a cage on flat ground beside a named guardian; defeat it, then open the cage. While caged the captive is held like a cauldron's occupant: the guardian and the region's enemies leave it alone and cannot hurt it, and once the cage opens it can be attacked like any ally. A **bounty** names a guardian, with an adventurer waiting 24 blocks off; defeat it and return to them. A **chest** is locked: its native Rusty Key lies in a sack about 56 blocks away, or with a guardian who drops it. Guardians are a star above their kind with three times its HP. Their HP scale travels with the creature's synchronised data, so the nameplate's maximum matches its health on every client. About one friendly traveler in two (marked with an exclamation mark) points you to an encounter nearby, or finds a new one 110 to 250 blocks away; an accepted encounter shows a grey question mark and a tracker line, and the character keeps it, with its progress, between sessions (two at a time). Rewards are gold and a blue weapon or armour piece for your class. About one villager in three (not shopkeepers) asks for help with a battle or rescue 160 to 400 blocks out; once the site is done the adventurers or captive send you back, the map pin moves to the village, and the villager who asked pays the reward. Encounters nobody asked you to finish, and finished ones, are forgotten 900 blocks behind you; actors appear within 200 blocks and leave beyond 320. Natural places also hold landmark groups as you come within 160 blocks (two unfinished at a time): a hill, mountain, forest, beach or similar place a named boss with three to five of the region's enemies; a camp, bandit camp or old hut a leader with four or five followers; an altar, sanctuary, menhir, circle of power or graveyard a guardian with two or three minions. The group stands beside a centerpiece such as a mana tree's trunk. Once all of it falls, an unlocked reward chest appears at the centre, and the place stays done for the session. Places where the region keeps a utility item (a glider, boat, reins and the like) keep it and hold no group. Guardians and landmark leaders are named bosses (Vorgath, Grimthorn and the like): the name shows on the nameplate and in the tracker. Encounter enemies get the mod's small difficulty premium from the higher of your level and the region's: `2% * sqrt(level - 1)` more HP and `1.25% * sqrt(level - 1)` more attack and spell power, capped at +40% and +25%.

Swimming, climbing and riding have levels of their own, 0 to 100, earned by time spent at them while moving (level n after 6n² seconds: level 10 after ten minutes, 100 after about 17 hours). Riding a pet counts in full; walking with your pet out counts at half, so riding can be trained before you can ride. Each level adds 0.05% to that speed, so artifacts stay the larger bonus; climbing artifacts now raise the real climbing speed too, as the character panel shows. The character panel shows each level with Alpha's skill icon (without the skill tree's level badge), and the chat announces a new level. While you train one, a bar at the middle left of the screen, in the style of the level bar, shows its icon, level and progress to the next level; it fades out about a minute after you stop. With `pyroProgression 1` the level bar sits on the health and mana bars, centred between them, and the Use button and the stamina and cast bars move up above it. The character keeps them (save version 18).

`travelDungeons 1` (the default) turns the places where a region's missions put a hang glider, boat, climbing spikes or reins into dungeons, as OverworldEvents does: a Castle or Ancient Ruins on a large place, Ancient Ruins, a Crypt or a Tower on a small one, chosen by the world seed. The other places and their missions stay where they were. The converted sites, and the ocean shores, no longer hand out travel equipment; shops and items already owned are unaffected. General stores then sell a hang glider, boat, climbing spikes and reins for ten silver each (one of each while you have none); they work in every region. The hang glider needs climbing level 5, the boat swimming level 5 and the reins riding level 5; the climbing spikes need nothing, so climbing can be trained anywhere. Set `travelDungeons 0` and restart for the original sites and region-bound gear. Zones you have already visited keep their saved items.

The graphics experiments can be configured in `options.cfg`. In the game, open **Options**, change **Shadows** or **Global Illumination**, then choose **Apply** or **OK**. The game applies and saves both settings immediately.

| Setting | Effect |
| --- | --- |
| `alphaLighting 1` | Alpha daylight, biome palettes, terrain corner lighting, haze, and water shading. In the game, toggle **Alpha Lighting (restart)** in **Options**; it applies at the next start. |
| `sunShadows 1` | A 3,072-texel directional depth map with surface-aware filtering for terrain and opaque models. Shadows soften at the edge and fade between 28 and 70 blocks from the camera. |
| `moonLight 1` | At night the moon, opposite the sun, lights the world with a faint blue light, and with `sunShadows 1` it casts shadows at 60% of the sun's strength. As the moon climbs, the light turns across the sky towards it, so nothing jumps at moonrise or moonset. On by default. |
| `frameStats 1` | The HUD shows, at the top centre, the frame rate and frame time (with the slowest frame), the CPU time of the game update and of the render with its stages, the CPU's wait for the GPU, the GPU's own time, and the draws, triangles, world sprites and shadow casters, averaged over half a second. Also in the Options menu as Frame Stats. Off by default. |
| `movingLeaves 1` | Tree leaves sway and flutter in the wind, with their shadows. On by default; toggle **Moving Leaves** in **Options**, which applies at once. |
| `rsmGI 1` | Optional diffuse sunlight bounce from nearby solid terrain. Uses a 512 × 512 reflective shadow map and fades out by 50 blocks. Defaults to off. |
| `temporalAA 1` | Eight subpixel camera samples resolve to a fixed pixel grid. Depth checks match the previous camera ray and history accumulates more strongly while the camera is still. |
| `spatialAA 1` | SMAA Ultra edge detection, blend weights, and neighborhood blending using the bundled SMAA reference textures. |

TAA resolves the world before SMAA. The map, HUD, and menus draw after both passes. When either option is on, the game uses a single-sample scene target. With both options off, `antiAliasingSamples` controls MSAA.

The sun shadow range, distance fade and warm direct light follow the Omega-inspired Steam preset, and the moon's light and shadows follow its MoonLight rules. [MoonandShadow](https://github.com/DieuDeGlace/MoonandShadow)

Grass, flowers and other plants of one model draw together: one instanced draw per model and zone in the world pass, and per model in the sun shadow pass. A savannah no longer costs thousands of separate draws a frame.

The bounced light gathers from the depth and material colors of sunlit terrain in world space. The sampling pattern stays fixed across frames. [Photon-LIGHT](https://github.com/Bonbox562/Photon-LIGHT) · [Reflective Shadow Maps](https://users.soe.ucsc.edu/~pang/160/s13/proposal/mijallen/proposal/media/p203-dachsbacher.pdf)
