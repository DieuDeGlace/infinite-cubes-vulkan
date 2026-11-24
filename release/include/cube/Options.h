// Options holds the settings stored in options.cfg as name/value lines. The controls and options panels edit copies
// before applying them to Game.
#pragma once

#include <iosfwd>

namespace cube {

struct Options {
    Options();
    // Reads options.cfg when present, using lines of "name value" with option member names as keys.
    bool load();
    // Writes "options.cfg".
    void save();

    int fullscreen;          // Initially 1.
    int resolutionX;         // Initially 0.
    int resolutionY;         // Initially 0.
    int refreshRate;         // Initially 0.
    int antiAliasing;        // Initially 0.
    int renderDistance;      // Initially 50.
    int cpuPerformance;      // Initially 1.
    int soundVolume;         // Initially 100.
    int musicVolume;         // Initially 100.
    int cameraSpeed;         // Initially 50.
    int cameraSmoothness;    // Initially 80.
    int invertY;             // Initially 0.
    int language;            // Initially 0.
    int minTimeStep;         // Initially 0.
    int smallGUI;            // Initially 0.
    int vsync;               // Initially 1.
    int showRarity;          // Initially 1.
    float guiScale;          // Initially 1.0.
    float mapScale;          // Initially 1.0.
    int musicLoop;           // Initially 0.
    int antiAliasingSamples; // Initially 8.
    int waterLilies;         // Initially 1.
    int alphaTerrain;        // Read at startup. Uses its own saves.
    int largerBiomes;        // Read at startup. Selects 1.5x biome spacing.
    int pyroProgression;
    int aquaticEnemies;
    int spawnDensity;   // Read at startup. Wild enemy groups, percent of native (50..400).
    int travelDungeons; // Read at startup. Glider, boat, climbing-spike and reins sites become dungeons.
    int alphaLighting;  // Selects the lighting used when building terrain meshes.
    int sunShadows;     // Directional shadows from the world geometry.
    int rsmGI;          // One bounce of sunlight from nearby surfaces.
    int movingLeaves;   // Tree leaves sway in the wind.
    int moonLight;      // At night the moon lights the world and, with sunShadows, casts the shadows.
    int frameStats;     // The HUD shows the frame time, its CPU stages, the GPU time and what was drawn.
    int temporalAA;     // Scene TAA before the HUD.
    int spatialAA;      // Scene SMAA Ultra.

private:
    void read(std::istream &file);
    void write(std::ostream &file) const;
};

}
