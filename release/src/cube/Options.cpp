#include "cube/Noise.h"
#include "cube/Options.h"
#include <fstream>
#include <string>
#ifdef __ANDROID__
#include <sstream>
#include "AndroidGameData.h"
#endif

namespace cube {

Options::Options()
{
    fullscreen = 1;
    resolutionX = 0;
    resolutionY = 0;
    refreshRate = 0;
    antiAliasing = 0;
    renderDistance = 50;
    cpuPerformance = 1;
    soundVolume = 100;
    musicVolume = 100;
    cameraSpeed = 50;
    cameraSmoothness = 80;
    invertY = 0;
    language = 0;
    minTimeStep = 0;
    smallGUI = 0;
    vsync = 1;
    showRarity = 1;
    guiScale = 1.0f;
    mapScale = 1.0f;
    musicLoop = 0;
    antiAliasingSamples = 8;
    waterLilies = 1;
    alphaTerrain = 0;
    largerBiomes = 0;
    pyroProgression = 1;
    aquaticEnemies = 1;
    travelDungeons = 1;
    spawnDensity = 150;
    alphaLighting = 0;
    sunShadows = 0;
    rsmGI = 0;
    movingLeaves = 1;
    moonLight = 1;
    frameStats = 0;
    temporalAA = 0;
    spatialAA = 0;
}

bool Options::load()
{
    std::ifstream file("options.cfg");
    if (file.is_open())
        read(file);
#ifdef __ANDROID__
    // A partial configuration in Documents can override individual app settings.
    if (android::usingDocumentsOptions()) {
        android::GameDataStream document("options.cfg");
        if (document.is_open()) {
            read(document);
            return true;
        }
    }
#endif
    return file.is_open();
}

void Options::read(std::istream &file)
{
    while (file.good()) {
        std::string key;
        file >> key;
        if (!file.good()) {
            break;
        }
        if (key == "fullscreen") {
            file >> fullscreen;
        }
        if (key == "resolutionX") {
            file >> resolutionX;
        }
        if (key == "resolutionY") {
            file >> resolutionY;
        }
        if (key == "refreshRate") {
            file >> refreshRate;
        }
        if (key == "antiAliasing") {
            file >> antiAliasing;
        }
        if (key == "renderDistance") {
            file >> renderDistance;
        }
        if (key == "cpuPerformance") {
            file >> cpuPerformance;
        }
        if (key == "soundVolume") {
            file >> soundVolume;
        }
        if (key == "musicVolume") {
            file >> musicVolume;
        }
        if (key == "cameraSpeed") {
            file >> cameraSpeed;
        }
        if (key == "cameraSmoothness") {
            file >> cameraSmoothness;
        }
        if (key == "language") {
            file >> language;
        }
        if (key == "invertY") {
            file >> invertY;
        }
        if (key == "minTimeStep") {
            file >> minTimeStep;
        }
        if (key == "smallGUI") {
            file >> smallGUI;
        }
        if (key == "vsync") {
            file >> vsync;
        }
        if (key == "showRarity") {
            file >> showRarity;
        }
        if (key == "musicLoop") {
            file >> musicLoop;
        }
        if (key == "guiScale") {
            file >> guiScale;
        }
        if (key == "mapScale") {
            file >> mapScale;
        }
        if (key == "antiAliasingSamples") {
            file >> antiAliasingSamples;
        }
        if (key == "waterLilies") {
            file >> waterLilies;
        }
        if (key == "alphaTerrain") {
            file >> alphaTerrain;
        }
        if (key == "largerBiomes") {
            file >> largerBiomes;
        }
        if (key == "pyroProgression") {
            file >> pyroProgression;
        }
        if (key == "aquaticEnemies") {
            file >> aquaticEnemies;
        }
        if (key == "spawnDensity") {
            file >> spawnDensity;
        }
        if (key == "travelDungeons") {
            file >> travelDungeons;
        }
        if (key == "alphaLighting") {
            file >> alphaLighting;
        }
        if (key == "sunShadows") {
            file >> sunShadows;
        }
        if (key == "rsmGI") {
            file >> rsmGI;
        }
        if (key == "movingLeaves") {
            file >> movingLeaves;
        }
        if (key == "moonLight") {
            file >> moonLight;
        }
        if (key == "frameStats") {
            file >> frameStats;
        }
        if (key == "temporalAA") {
            file >> temporalAA;
        }
        if (key == "spatialAA") {
            file >> spatialAA;
        }
    }
}

void Options::save()
{
#ifdef __ANDROID__
    if (android::usingDocumentsOptions()) {
        std::ostringstream contents;
        write(contents);
        const std::string text = contents.str();
        {
            std::ofstream file("options.cfg");
            file << text;
        }
        android::writeDocumentsOptions(text);
        return;
    }
#endif
    std::ofstream file("options.cfg");
    write(file);
}

void Options::write(std::ostream &file) const
{
    file << "fullscreen " << fullscreen << std::endl;
    file << "resolutionX " << resolutionX << std::endl;
    file << "resolutionY " << resolutionY << std::endl;
    file << "refreshRate " << refreshRate << std::endl;
    file << "antiAliasing " << antiAliasing << std::endl;
    file << "renderDistance " << renderDistance << std::endl;
    file << "cpuPerformance " << cpuPerformance << std::endl;
    file << "soundVolume " << soundVolume << std::endl;
    file << "musicVolume " << musicVolume << std::endl;
    file << "cameraSpeed " << cameraSpeed << std::endl;
    file << "cameraSmoothness " << cameraSmoothness << std::endl;
    file << "language " << language << std::endl;
    file << "invertY " << invertY << std::endl;
    file << "minTimeStep " << minTimeStep << std::endl;
    file << "smallGUI " << smallGUI << std::endl;
    file << "vsync " << vsync << std::endl;
    file << "showRarity " << showRarity << std::endl;
    file << "musicLoop " << musicLoop << std::endl;
    file << "guiScale " << guiScale << std::endl;
    file << "mapScale " << mapScale << std::endl;
    file << "antiAliasingSamples " << antiAliasingSamples << std::endl;
    file << "waterLilies " << waterLilies << std::endl;
    file << "alphaTerrain " << alphaTerrain << std::endl;
    file << "largerBiomes " << largerBiomes << std::endl;
    file << "pyroProgression " << pyroProgression << std::endl;
    file << "aquaticEnemies " << aquaticEnemies << std::endl;
    file << "travelDungeons " << travelDungeons << std::endl;
    file << "spawnDensity " << spawnDensity << std::endl;
    file << "alphaLighting " << alphaLighting << std::endl;
    file << "sunShadows " << sunShadows << std::endl;
    file << "rsmGI " << rsmGI << std::endl;
    file << "movingLeaves " << movingLeaves << std::endl;
    file << "moonLight " << moonLight << std::endl;
    file << "frameStats " << frameStats << std::endl;
    file << "temporalAA " << temporalAA << std::endl;
    file << "spatialAA " << spatialAA << std::endl;
}

}
