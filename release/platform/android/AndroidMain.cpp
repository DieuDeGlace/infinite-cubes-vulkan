// The Android program: the counterpart of main.cpp's WinMain. It creates the same game objects on a native activity
// window, feeds the game from the touch screen and the system keyboard, and keeps the game alive while Android takes
// the window away and returns it.
#include <android/log.h>
#include <android_native_app_glue.h>
#include <jni.h>
#include <unistd.h>
#include <windows.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <vector>

#include "AndroidAudio.h"
#include "AndroidGameData.h"
#include "AndroidJni.h"
#include "GameTouch.h"
#include "cube/AlphaTerrain.h"
#include "cube/Game.h"
#include "cube/Options.h"
#include "cube/SpriteManager.h"
#include "cube/World.h"
#include "cube/WorldMap.h"
#include "cube/widgets/ChatWidget.h"
#include "gfx/VulkanContext.h"
#include "gfx/VulkanGraphics.h"
#include "gfx/VulkanRenderer.h"
#include "plasma/TextIds.h"
#include "gfx/VulkanWorldData.h"
#include "plasma/FontEngine.h"
#include "plasma/Node.h"
#include "plasma/VulkanEngine.h"
#include "steam/steam_api.h"

namespace {

constexpr char LogTag[] = "CubeWorld";
// Windows mouse buttons as Game::handleButtonDown numbers them.
constexpr int LeftButton = 0;
constexpr int RightButton = 1;

constexpr const char *RequiredGameFiles[] = {"resource1.dat", "icons.plx", "gui.plx", "cursor.plx",
                                             "sprites/cubeworld.cub"};

constexpr uint32_t CircleSegments = 24;
constexpr uint32_t CircleVertices = CircleSegments + 1;
constexpr uint32_t CircleIndices = CircleSegments * 3;
using Color = plasma::Vector<4, unsigned char>;

void appendCircle(std::vector<gfx::TexturedVertex> &vertices, std::vector<uint32_t> &indices, glm::vec2 center,
                  float radius, glm::vec2 size, const Color &color)
{
    constexpr float Turn = 6.28318530718f;
    const auto offset = static_cast<uint32_t>(vertices.size());
    const auto vertex = [&](glm::vec2 position) {
        return gfx::TexturedVertex {{2 * position.x / size.x - 1, 1 - 2 * position.y / size.y, 0, 1}, color, {0, 0}};
    };
    vertices.push_back(vertex(center));
    for (uint32_t i = 0; i < CircleSegments; ++i) {
        const float angle = float(i) / CircleSegments * Turn;
        vertices.push_back(vertex(center + radius * glm::vec2(std::cos(angle), std::sin(angle))));
        indices.insert(indices.end(), {offset, offset + 1 + i, offset + 1 + (i + 1) % CircleSegments});
    }
}

// A band between two radii around center, for the rims of the on-screen buttons.
void appendRing(std::vector<gfx::TexturedVertex> &vertices, std::vector<uint32_t> &indices, glm::vec2 center,
                float inner, float outer, glm::vec2 size, const Color &color)
{
    constexpr float Turn = 6.28318530718f;
    const auto offset = static_cast<uint32_t>(vertices.size());
    for (uint32_t i = 0; i < CircleSegments; ++i) {
        const float angle = float(i) / CircleSegments * Turn;
        const glm::vec2 direction(std::cos(angle), std::sin(angle));
        for (const float radius : {inner, outer}) {
            const glm::vec2 position = center + radius * direction;
            vertices.push_back({{2 * position.x / size.x - 1, 1 - 2 * position.y / size.y, 0, 1}, color, {0, 0}});
        }
        const uint32_t a = offset + 2 * i, b = offset + 2 * ((i + 1) % CircleSegments);
        indices.insert(indices.end(), {a, a + 1, b + 1, a, b + 1, b});
    }
}

VkResult createAndroidSurface(ANativeWindow *window, VkInstance instance, VkSurfaceKHR *surface)
{
    VkAndroidSurfaceCreateInfoKHR info {VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR};
    info.window = window;
    return vkCreateAndroidSurfaceKHR(instance, &info, nullptr, surface);
}

class GameDataError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

void checkGameFiles()
{
    for (const char *path : RequiredGameFiles) {
        android::GameDataStream file(path, std::ios::binary);
        if (!file || file.tellg() == std::streampos(-1) || file.peek() == std::char_traits<char>::eof()) {
            throw GameDataError(std::string("Cannot read ") + path +
                                ". Choose the folder in Documents that contains resource1.dat and sprites.");
        }
    }
}

bool takeGameDataFolderChange(ANativeActivity *activity)
{
    const android::JniFrame jni(activity->vm);
    JNIEnv *env = jni.get();
    if (!env)
        return false;

    const jclass type = env->GetObjectClass(activity->clazz);
    const jmethodID take = type ? env->GetMethodID(type, "takeGameDataFolderChange", "()Z") : nullptr;
    const bool changed = take && env->CallBooleanMethod(activity->clazz, take);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }
    return changed;
}

bool showStartupMessage(ANativeActivity *activity, const char *message, bool failed = false, bool chooseFolder = false)
{
    const android::JniFrame jni(activity->vm);
    JNIEnv *env = jni.get();
    if (!env)
        return false;

    const jclass type = env->GetObjectClass(activity->clazz);
    const jmethodID show = type ? env->GetMethodID(type, "showStartupMessage", "(Ljava/lang/String;ZZ)V") : nullptr;
    const jstring text = show && message ? env->NewStringUTF(message) : nullptr;
    const bool canShow = show && (!message || text);
    if (canShow)
        env->CallVoidMethod(activity->clazz, show, text, static_cast<jboolean>(failed),
                            static_cast<jboolean>(chooseFolder));
    const bool jniFailed = env->ExceptionCheck();
    if (jniFailed)
        env->ExceptionClear();
    return canShow && !jniFailed;
}

class StartupLog {
public:
    explicit StartupLog(ANativeActivity *activity)
        : m_activity(activity)
    {}

    void open()
    {
        if (m_stream.is_open())
            m_stream.close();
        m_stream.open("log_startup.txt");
        m_start = m_stageStart = std::chrono::steady_clock::now();
    }

    // Each stage's line is followed by the time it took, once the next stage begins.
    void stage(const char *message)
    {
        finishStage();
        m_stream << message << std::endl;
        __android_log_write(ANDROID_LOG_INFO, LogTag, message);
        showStartupMessage(m_activity, message);
    }

    void finishStage()
    {
        const auto now = std::chrono::steady_clock::now();
        const auto milliseconds = [](auto duration) {
            return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
        };
        if (now != m_stageStart)
            m_stream << "  " << milliseconds(now - m_stageStart) << " ms (" << milliseconds(now - m_start)
                     << " ms since start)" << std::endl;
        m_stageStart = now;
    }

    std::ostream &stream() { return m_stream; }

private:
    ANativeActivity *m_activity;
    std::ofstream m_stream;
    std::chrono::steady_clock::time_point m_start, m_stageStart;
};

// Shows or hides the system keyboard through the activity's InputMethodManager.
void showKeyboard(android_app *app, bool show)
{
    const android::JniFrame jni(app->activity->vm);
    JNIEnv *env = jni.get();
    if (!env)
        return;
    const jobject activity = app->activity->clazz;
    const jclass activityClass = env->GetObjectClass(activity);
    const jobject service = env->CallObjectMethod(
        activity, env->GetMethodID(activityClass, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;"),
        env->NewStringUTF("input_method"));
    const jobject window =
        env->CallObjectMethod(activity, env->GetMethodID(activityClass, "getWindow", "()Landroid/view/Window;"));
    const jobject view = env->CallObjectMethod(
        window, env->GetMethodID(env->GetObjectClass(window), "getDecorView", "()Landroid/view/View;"));
    const jclass serviceClass = env->GetObjectClass(service);
    if (show) {
        env->CallBooleanMethod(service, env->GetMethodID(serviceClass, "showSoftInput", "(Landroid/view/View;I)Z"),
                               view, 0);
    } else {
        const jobject token = env->CallObjectMethod(
            view, env->GetMethodID(env->GetObjectClass(view), "getWindowToken", "()Landroid/os/IBinder;"));
        env->CallBooleanMethod(
            service, env->GetMethodID(serviceClass, "hideSoftInputFromWindow", "(Landroid/os/IBinder;I)Z"), token, 0);
    }
    if (env->ExceptionCheck())
        env->ExceptionClear();
}

// The Windows virtual-key code of an Android key, or 0 for a key the game does not read.
int virtualKey(int keyCode)
{
    if (keyCode >= AKEYCODE_A && keyCode <= AKEYCODE_Z)
        return 'A' + keyCode - AKEYCODE_A;
    if (keyCode >= AKEYCODE_0 && keyCode <= AKEYCODE_9)
        return '0' + keyCode - AKEYCODE_0;
    switch (keyCode) {
    case AKEYCODE_BACK:
    case AKEYCODE_ESCAPE:
        return VK_ESCAPE;
    case AKEYCODE_ENTER:
    case AKEYCODE_NUMPAD_ENTER:
        return VK_RETURN;
    case AKEYCODE_DEL:
        return VK_BACK;
    case AKEYCODE_FORWARD_DEL:
        return VK_DELETE;
    case AKEYCODE_SPACE:
        return VK_SPACE;
    case AKEYCODE_TAB:
        return VK_TAB;
    case AKEYCODE_SHIFT_LEFT:
    case AKEYCODE_SHIFT_RIGHT:
        return VK_SHIFT;
    case AKEYCODE_CTRL_LEFT:
    case AKEYCODE_CTRL_RIGHT:
        return VK_CONTROL;
    case AKEYCODE_DPAD_LEFT:
        return VK_LEFT;
    case AKEYCODE_DPAD_RIGHT:
        return VK_RIGHT;
    case AKEYCODE_DPAD_UP:
        return VK_UP;
    case AKEYCODE_DPAD_DOWN:
        return VK_DOWN;
    case AKEYCODE_MOVE_HOME:
        return VK_HOME;
    case AKEYCODE_MOVE_END:
        return VK_END;
    }
    return 0;
}

// The character a key types on a US layout, or 0.
char typedCharacter(int keyCode, bool shift)
{
    if (keyCode >= AKEYCODE_A && keyCode <= AKEYCODE_Z)
        return static_cast<char>((shift ? 'A' : 'a') + keyCode - AKEYCODE_A);
    if (keyCode >= AKEYCODE_0 && keyCode <= AKEYCODE_9)
        return shift ? ")!@#$%^&*("[keyCode - AKEYCODE_0] : static_cast<char>('0' + keyCode - AKEYCODE_0);
    switch (keyCode) {
    case AKEYCODE_SPACE:
        return ' ';
    case AKEYCODE_PERIOD:
        return shift ? '>' : '.';
    case AKEYCODE_COMMA:
        return shift ? '<' : ',';
    case AKEYCODE_MINUS:
        return shift ? '_' : '-';
    case AKEYCODE_EQUALS:
        return shift ? '+' : '=';
    case AKEYCODE_SLASH:
        return shift ? '?' : '/';
    case AKEYCODE_SEMICOLON:
        return shift ? ':' : ';';
    case AKEYCODE_APOSTROPHE:
        return shift ? '"' : '\'';
    case AKEYCODE_AT:
        return '@';
    case AKEYCODE_POUND:
        return '#';
    case AKEYCODE_STAR:
        return '*';
    case AKEYCODE_PLUS:
        return '+';
    }
    return 0;
}

// Everything WinMain creates for one run of the game, destroyed in the reverse order.
struct Session {
    Session(ANativeWindow *window, StartupLog &log)
    {
        const int width = ANativeWindow_getWidth(window);
        const int height = ANativeWindow_getHeight(window);
        // A phone has no options.cfg at first: start from settings its GPU can hold at full resolution.
        if (!options.load())
            options.antiAliasingSamples = 2;
        options.fullscreen = 1;
        options.resolutionX = width;
        options.resolutionY = height;
        options.save();

        // The Android build links the offline Steam stand-in: single player only.
        log.stream() << "SteamAPI_Init" << std::endl;
        if (!SteamAPI_Init())
            throw std::runtime_error("Steam could not be initialized.");
        log.stage("Starting Vulkan");
        context.initialize(
            VK_KHR_ANDROID_SURFACE_EXTENSION_NAME,
            [window](VkInstance instance, VkSurfaceKHR *surface) {
                return createAndroidSurface(window, instance, surface);
            },
            log.stream());
        engine = std::make_unique<plasma::VulkanEngine>(context);
        engine->getFontEngine()->addFontPath(L"/system/fonts");
        log.stage("Starting audio");
        if (!audio.initialize())
            log.stream() << "Audio initialization failed" << std::endl;
        audio.setMusicVolume(options.musicVolume * 0.01f, 0.0f);

        log.stage("Creating graphics");
        graphics = std::make_unique<gfx::VulkanGraphics>(context);
        log.stage("Reading sprite models");
        // A selected folder serves files one at a time; later starts read the models from one packed copy.
        android::openGameDataPack("sprites", "cache/sprites.pack");
        try {
            spriteManager = std::make_unique<cube::SpriteManager>(graphics.get());
        } catch (...) {
            android::closeGameDataPack(false);
            throw;
        }
        android::closeGameDataPack();
        log.stage("Creating world");
        std::shared_ptr<cube::AlphaTerrain> alphaTerrain;
        if (options.alphaTerrain)
            alphaTerrain = std::make_shared<cube::AlphaTerrain>();
        world = std::make_unique<cube::World>(spriteManager.get(), alphaTerrain, options.largerBiomes != 0,
                                              options.pyroProgression != 0, options.aquaticEnemies != 0,
                                              options.largerBiomes == cube::Options::BiomesWithPlaces);
        world->m_travelDungeons = options.travelDungeons != 0;
        cube::Item::s_travelGearAnywhere = world->m_travelDungeons;
        world->m_spawnDensity = std::clamp(options.spawnDensity, 50, 400);
        std::filesystem::create_directories(world->saveDirectory());
        log.stage("Creating world map");
        worldMap = std::make_unique<cube::WorldMap>(world.get(), engine.get());
        log.stage("Creating renderer");
        renderer = std::make_unique<gfx::VulkanRenderer>(world.get(), worldMap.get(), engine.get(), context,
                                                         options.alphaLighting != 0, options.sunShadows != 0,
                                                         options.rsmGI != 0);
        log.stage("Loading menus, sounds and saves");
        // The display offers the window's size only.
        resolutions = {{{width, height}, {60}}};
        game = std::make_unique<cube::Game>(world.get(), worldMap.get(), renderer.get(), engine.get(), &audio, &options,
                                            &resolutions);
        // The on-screen buttons' names, in the interface font with its black outline like the HUD's labels.
        for (const android::TouchButton &button : android::TouchButtons) {
            const bool large = button.radius >= 0.08f;
            plasma::Node *label = game->m_interface.createText(button.label, L"touch-label", engine->root(),
                                                               (1 << plasma::TEXT_CENTER) | (1 << plasma::TEXT_MIDDLE),
                                                               large ? 16 : 12, 3);
            label->setIgnoresPointer(true);
            label->setVisible(false);
            touchLabels.push_back(label);
        }
    }

    ~Session()
    {
        // A lost device can report an error here too. Let the original error reach the activity.
        try {
            context.waitIdle();
        } catch (const std::exception &error) {
            __android_log_write(ANDROID_LOG_ERROR, LogTag, error.what());
        }
        game.reset();
        SteamAPI_Shutdown();
    }

    cube::Options options;
    gfx::VulkanContext context;
    std::unique_ptr<plasma::VulkanEngine> engine;
    cube::AndroidAudio audio;
    std::unique_ptr<gfx::VulkanGraphics> graphics;
    std::unique_ptr<cube::SpriteManager> spriteManager;
    std::unique_ptr<cube::World> world;
    std::unique_ptr<cube::WorldMap> worldMap;
    std::unique_ptr<gfx::VulkanRenderer> renderer;
    std::vector<plasma::Node *> touchLabels; // owned by the engine's root
    std::vector<cube::ResolutionInfo> resolutions;
    std::unique_ptr<cube::Game> game;
};

class AndroidGame {
public:
    explicit AndroidGame(android_app *app)
        : m_app(app),
          m_log(app->activity)
    {}

    // Once started, a new window replaces just the Vulkan surface.
    void initWindow(ANativeWindow *window)
    {
        if (m_session) {
            m_session->context.replaceSurface([window](VkInstance instance, VkSurfaceKHR *surface) {
                return createAndroidSurface(window, instance, surface);
            });
        }
    }

    bool canStart() const
    {
        return !m_failed && !m_session && !m_paused && m_app->window && ANativeWindow_getWidth(m_app->window) > 0 &&
               ANativeWindow_getHeight(m_app->window) > 0;
    }

    void start()
    {
        // Options and saves use the app directory. Read-only assets can come from the selected folder.
        const char *directory = m_app->activity->externalDataPath;
        if (!directory || chdir(directory) != 0)
            throw std::runtime_error("The app's storage directory is not available.");
        m_log.open();
        android::setGameDataActivity(m_app->activity);
        m_log.stage(android::usingGameDataFolder() ? "Checking the selected game folder" : "Checking game files");
        checkGameFiles();
        // The C runtime on Windows reads the time zone at start-up; the renderer uses its daylight-saving flag.
        tzset();
        m_session = std::make_unique<Session>(m_app->window, m_log);
        m_lastFrame = Clock::now();
        m_log.stage("Waiting for the first Vulkan frame");
    }

    void termWindow()
    {
        releaseTouches();
        if (m_session)
            m_session->context.releaseSurface();
    }

    void setPaused(bool paused)
    {
        m_paused = paused;
        if (!paused && takeGameDataFolderChange(m_app->activity))
            m_failed = false;
        releaseTouches();
        if (!m_session)
            return;
        m_session->audio.setPaused(paused);
        // Android may end a paused app without notice.
        if (paused)
            m_session->game->saveCharacter();
        else
            m_lastFrame = Clock::now();
    }

    void close()
    {
        releaseTouches();
        m_session.reset();
    }

    void fail(const std::exception &error)
    {
        if (m_failed)
            return;
        m_failed = true;

        const char *message = error.what();
        if (!*message)
            message = "The game could not load. Tap Copy log for the startup details.";
        m_log.stream() << "Error: " << message << std::endl;
        __android_log_write(ANDROID_LOG_ERROR, LogTag, message);
        const bool chooseFolder =
            dynamic_cast<const GameDataError *>(&error) != nullptr || (!m_session && android::usingGameDataFolder());
        try {
            close();
        } catch (const std::exception &closing) {
            __android_log_write(ANDROID_LOG_ERROR, LogTag, closing.what());
        }
        if (!showStartupMessage(m_app->activity, message, true, chooseFolder))
            ANativeActivity_finish(m_app->activity);
    }

    bool running() const { return m_session && !m_paused && m_app->window; }
    bool finished() const { return m_session && m_session->game->m_shutdown; }
    // A new terrain or biome size (Game::applyOptions): the next session creates the world with it.
    bool restarting() const { return finished() && m_session->game->m_restart; }

    int32_t input(const AInputEvent *event)
    {
        if (!running())
            return 0;
        switch (AInputEvent_getType(event)) {
        case AINPUT_EVENT_TYPE_MOTION:
            return (AInputEvent_getSource(event) & AINPUT_SOURCE_TOUCHSCREEN) == AINPUT_SOURCE_TOUCHSCREEN
                       ? touch(event)
                       : 0;
        case AINPUT_EVENT_TYPE_KEY:
            return key(event);
        }
        return 0;
    }

    // One frame, as main.cpp's draw: the controls, the game's update (which renders) and the presentation.
    void frame()
    {
        Session &session = *m_session;
        cube::Game &game = *session.game;
        const int windowWidth = ANativeWindow_getWidth(m_app->window);
        const int windowHeight = ANativeWindow_getHeight(m_app->window);
        if (windowWidth <= 0 || windowHeight <= 0)
            return;
        const cube::Options &options = game.m_options;
        // Phone GPUs can drop the multisampled depth resolve the water uses to see the lakebed, which leaves the water
        // clear. Single-sample depth is copied instead; a phone's pixel density makes MSAA hard to see anyway.
        constexpr unsigned samples = 1;
        const VkExtent2D extent =
            session.context.beginFrame({static_cast<uint32_t>(windowWidth), static_cast<uint32_t>(windowHeight)},
                                       options.vsync != 0, samples, options.temporalAA != 0, options.spatialAA != 0);
        if (!extent.width || !extent.height) {
            Sleep(16);
            return;
        }
        const int width = static_cast<int>(extent.width);
        const int height = static_cast<int>(extent.height);
        if (session.engine->viewportWidth() != width || session.engine->viewportHeight() != height) {
            session.engine->setViewport(0, 0, width, height, true);
            game.resize(width, height);
        }
        m_touch.resize(glm::vec2(width, height));
        applyControls(game);
        placeLabels();

        const auto now = Clock::now();
        const std::chrono::duration<double, std::milli> elapsed = now - m_lastFrame;
        m_lastFrame = now;
        game.update(elapsed.count());
        drawControls(session.context, glm::vec2(width, height));
        if (session.context.present() && !m_loggedFirstFrame) {
            m_log.finishStage();
            m_log.stream() << "First Vulkan frame presented" << std::endl;
            showStartupMessage(m_app->activity, nullptr);
            m_loggedFirstFrame = true;
        }

        const bool wantsKeyboard =
            session.engine->getFocusWidget() != nullptr || game.m_interface.m_inGame.m_chatWidget->m_typeboxActive;
        if (wantsKeyboard != m_keyboardShown) {
            m_keyboardShown = wantsKeyboard;
            showKeyboard(m_app, wantsKeyboard);
        }
    }

private:
    using Clock = std::chrono::steady_clock;

    bool inMenu() const { return m_session->game->m_interface.isInMenu(); }
    bool inCharacterCreation() const { return m_session->game->m_interface.m_characterCreationNode->isVisible(); }

    // Passes the on-screen controls to the game the way WinMain passes the keyboard, mouse and gamepad.
    void applyControls(cube::Game &game)
    {
        const bool menu = inMenu();
        if (menu != m_wasInMenu) {
            // The touches of the other mode would be read as something else.
            releaseTouches();
            m_wasInMenu = menu;
        }
        for (int control = 0; control < cube::Controls::ButtonCount; ++control)
            game.setControl(control, false);
        for (size_t i = 0; i < android::TouchButtons.size(); ++i) {
            if (m_touch.pressed(i))
                game.setControl(android::TouchButtons[i].control, true);
        }
        if (menu) {
            game.setStickMove(0.0f, 0.0f);
            advanceTap(game);
            return;
        }
        const glm::vec2 stick = m_touch.stick();
        game.setStickMove(stick.x, -stick.y);
        // The game expects mouse counts; a drag across a 1080-pixel-high screen turns like one across a desktop.
        const glm::vec2 look = m_touch.takeLook() * (1080.0f / std::max(1, session().engine->viewportHeight()));
        game.handleMouseMove(look.x, look.y);
    }

    Session &session() { return *m_session; }

    // A mouse hovers over a button before it clicks, and the interface finds the pointed widget once per frame. A
    // finger arrives and presses at once, so the press follows the pointer by a frame and the release by another.
    void advanceTap(cube::Game &game)
    {
        if (m_tap == Tap::None)
            return;
        game.handleMouseMove(m_pointer.x, m_pointer.y);
        if (m_tap == Tap::Hover) {
            m_tap = Tap::Armed;
        } else if (m_tap == Tap::Armed) {
            game.handleButtonDown(LeftButton);
            m_tap = Tap::Down;
        } else if (m_pointerId < 0) {
            m_tap = Tap::None;
            game.handleButtonUp(LeftButton);
        }
    }

    // The labels are interface text, drawn with the game's frame, so they are placed before it.
    void placeLabels()
    {
        auto &labels = session().touchLabels;
        const bool creation = inCharacterCreation();
        const bool menu = inMenu();
        for (size_t i = 0; i < labels.size(); ++i) {
            const android::TouchButton &button = android::TouchButtons[i];
            const bool shown = !creation && (!menu || button.inMenus);
            labels[i]->setVisible(shown);
            if (shown) {
                const glm::vec2 center = m_touch.center(button);
                labels[i]->setPosition(center.x, center.y, 0.0f, 0.0f);
            }
        }
    }

    void drawControls(gfx::VulkanContext &context, glm::vec2 size)
    {
        if (inCharacterCreation())
            return;
        const bool menu = inMenu();
        std::vector<gfx::TexturedVertex> vertices;
        std::vector<uint32_t> indices;
        vertices.reserve((android::TouchButtons.size() * 3 + 2) * CircleVertices);
        indices.reserve((android::TouchButtons.size() * 3 + 2) * CircleIndices);
        if (!menu) {
            appendCircle(vertices, indices, m_touch.stickCenter(), m_touch.stickRadius(), size, {12, 25, 32, 110});
            appendCircle(vertices, indices, m_touch.stickThumb(), m_touch.stickRadius() * 0.38f, size,
                         {228, 237, 233, 165});
        }
        for (size_t i = 0; i < android::TouchButtons.size(); ++i) {
            const android::TouchButton &button = android::TouchButtons[i];
            if (menu && !button.inMenus)
                continue;
            // The menus' button look: a dark translucent face with a white rim, light while held. The face stays thin so
            // the label (drawn first, with the game) shows through it.
            const bool held = m_touch.pressed(i);
            const float radius = m_touch.radius(button);
            const float rim = std::max(2.0f, radius * 0.07f);
            appendCircle(vertices, indices, m_touch.center(button), radius - rim, size,
                         held ? Color(228, 237, 233, 120) : Color(12, 25, 32, 70));
            appendRing(vertices, indices, m_touch.center(button), radius - rim, radius, size,
                       held ? Color(255, 255, 255, 235) : Color(255, 255, 255, 170));
        }
        if (indices.empty())
            return;
        gfx::VulkanWorldUniforms uniforms;
        uniforms.projection.identity();
        uniforms.view.identity();
        uniforms.world.identity();
        const plasma::Vector<4, float> zero(0, 0, 0, 0);
        uniforms.lightPositions.fill(zero);
        uniforms.lightColors.fill(zero);
        uniforms.skyColors.fill(zero);
        uniforms.shadowTransform.fill(zero);
        context.setDrawState(
            {gfx::VulkanProgram::Textured, gfx::RasterizerTwoSided, gfx::DepthDisabled, gfx::BlendAlpha}, &uniforms,
            sizeof(uniforms));
        context.drawTransient(vertices.data(), vertices.size() * sizeof(gfx::TexturedVertex), indices.data(),
                              static_cast<uint32_t>(indices.size()));
    }

    // In a menu the first finger is the mouse pointer with its left button and a second finger holds the right
    // button (which drags the map). In the world, touches go to the on-screen controls.
    int32_t touch(const AInputEvent *event)
    {
        cube::Game &game = *m_session->game;
        const int action = AMotionEvent_getAction(event);
        const size_t index =
            (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
        const size_t count = AMotionEvent_getPointerCount(event);
        const auto position = [&](size_t i) {
            return glm::vec2(AMotionEvent_getX(event, i), AMotionEvent_getY(event, i));
        };
        switch (action & AMOTION_EVENT_ACTION_MASK) {
        case AMOTION_EVENT_ACTION_DOWN:
        case AMOTION_EVENT_ACTION_POINTER_DOWN: {
            if (index >= count)
                break;
            const int id = AMotionEvent_getPointerId(event, index);
            if (!inCharacterCreation() && m_touch.press(id, position(index), inMenu()))
                break;
            if (m_pointerId < 0 && m_tap == Tap::None) {
                m_pointerId = id;
                m_pointer = position(index);
                m_tap = Tap::Hover;
            } else if (m_secondPointerId < 0) {
                m_secondPointerId = id;
                game.handleButtonDown(RightButton);
            }
            break;
        }
        case AMOTION_EVENT_ACTION_MOVE:
            for (size_t i = 0; i < count; ++i) {
                const int id = AMotionEvent_getPointerId(event, i);
                m_touch.move(id, position(i));
                if (id == m_pointerId)
                    m_pointer = position(i);
            }
            break;
        case AMOTION_EVENT_ACTION_UP:
        case AMOTION_EVENT_ACTION_POINTER_UP:
            if (index < count)
                releaseTouch(AMotionEvent_getPointerId(event, index));
            break;
        case AMOTION_EVENT_ACTION_CANCEL:
            releaseTouches();
            break;
        }
        return 1;
    }

    void releaseTouch(int id)
    {
        if (id < 0)
            return;
        m_touch.release(id);
        // advanceTap completes the tap of a lifted pointer finger.
        if (id == m_pointerId)
            m_pointerId = -1;
        if (id == m_secondPointerId) {
            m_secondPointerId = -1;
            if (m_session)
                m_session->game->handleButtonUp(RightButton);
        }
    }

    // Lets go of everything at once, when the mode changes or the app leaves the screen.
    void releaseTouches()
    {
        m_touch.cancel();
        releaseTouch(m_secondPointerId);
        m_pointerId = -1;
        const bool pressPending = m_tap == Tap::Down;
        m_tap = Tap::None;
        if (pressPending && m_session)
            m_session->game->handleButtonUp(LeftButton);
    }

    // Keys of the system keyboard, a physical keyboard and the Back button, delivered as WinMain's window procedure
    // delivers WM_KEYDOWN, WM_KEYUP and WM_CHAR.
    int32_t key(const AInputEvent *event)
    {
        const int keyCode = AKeyEvent_getKeyCode(event);
        const int virtualCode = virtualKey(keyCode);
        const bool shift = (AKeyEvent_getMetaState(event) & AMETA_SHIFT_ON) != 0;
        const char character = typedCharacter(keyCode, shift);
        if (!virtualCode && !character)
            return 0;
        Session &session = *m_session;
        if (AKeyEvent_getAction(event) == AKEY_EVENT_ACTION_DOWN) {
            if (virtualCode) {
                session.engine->keyPressed(static_cast<unsigned short>(virtualCode));
                session.game->handleKeyDown(static_cast<unsigned char>(virtualCode));
            }
            if (character) {
                session.engine->characterEntered(static_cast<wchar_t>(character));
                session.game->onKeyTyped(static_cast<unsigned char>(character));
            }
        } else if (AKeyEvent_getAction(event) == AKEY_EVENT_ACTION_UP && virtualCode) {
            session.engine->keyReleased(static_cast<unsigned short>(virtualCode));
            session.game->handleKeyUp(static_cast<unsigned char>(virtualCode));
        }
        return 1;
    }

    android_app *m_app;
    StartupLog m_log;
    std::unique_ptr<Session> m_session;
    android::GameTouch m_touch;
    // The left mouse button a finger stands for in menus: over the widget, about to press, pressed.
    enum class Tap {
        None,
        Hover,
        Armed,
        Down
    };

    int m_pointerId = -1;       // the finger acting as the menu pointer
    int m_secondPointerId = -1; // the finger holding the right mouse button
    Tap m_tap = Tap::None;
    glm::vec2 m_pointer {0.0f};
    bool m_wasInMenu = false;
    bool m_paused = true;
    bool m_keyboardShown = false;
    bool m_failed = false;
    bool m_loggedFirstFrame = false;
    Clock::time_point m_lastFrame = Clock::now();
};

void onCommand(android_app *app, int32_t command)
{
    auto &game = *static_cast<AndroidGame *>(app->userData);
    try {
        switch (command) {
        case APP_CMD_INIT_WINDOW:
            if (app->window)
                game.initWindow(app->window);
            break;
        case APP_CMD_TERM_WINDOW:
            game.termWindow();
            break;
        case APP_CMD_RESUME:
            game.setPaused(false);
            break;
        case APP_CMD_PAUSE:
            game.setPaused(true);
            break;
        case APP_CMD_DESTROY:
            game.close();
            break;
        }
    } catch (const std::exception &error) {
        game.fail(error);
    }
}

int32_t onInput(android_app *app, AInputEvent *event)
{
    auto &game = *static_cast<AndroidGame *>(app->userData);
    try {
        return game.input(event);
    } catch (const std::exception &error) {
        game.fail(error);
        return 0;
    }
}

}

void android_main(android_app *app)
{
    const android::JniFrame gameThread(app->activity->vm);
    AndroidGame game(app);
    app->userData = &game;
    app->onAppCmd = onCommand;
    app->onInputEvent = onInput;
    while (!app->destroyRequested) {
        // Start or draw after pending lifecycle events have been processed.
        const int timeout = game.running() || game.canStart() ? 0 : -1;
        void *data = nullptr;
        if (ALooper_pollOnce(timeout, nullptr, nullptr, &data) == ALOOPER_POLL_ERROR)
            break;
        if (auto *source = static_cast<android_poll_source *>(data)) {
            source->process(app, source);
            continue;
        }
        if (app->destroyRequested)
            continue;
        try {
            if (game.canStart())
                game.start();
            if (!game.running())
                continue;
            game.frame();
            // A new terrain or biome size starts a new session; "Exit Game" in the system menu ends the activity.
            if (game.restarting())
                game.close();
            else if (game.finished())
                ANativeActivity_finish(app->activity);
        } catch (const std::exception &error) {
            game.fail(error);
        }
    }
    game.close();
    android::setGameDataActivity(nullptr);
}
