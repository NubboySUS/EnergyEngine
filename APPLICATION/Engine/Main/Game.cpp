//============== ENERGY ENGINE =================//
//                                              //
// Purpose: Launch and update the whole game    //
//                                              //
//==============================================//

#include "Game.h"
#include "Input.h"
#include "Console.h"
#include "Logger.h"
#include "CrashHandler.h"
#include "FileParsers/inicpp.h"
#include "VulkanContext.h"
#include "../ImGui/imgui.h"
#include "../ImGui/imgui_impl_vulkan.h"
#include "../ImGui/imgui_impl_engine.h"
#include "../World/Level.h"
#include "../World/Camera.h"
#include "../APPLICATION/Template/Gameplay/Player.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <format>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <cstdlib>
#include <filesystem>
#include <unordered_map>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <timeapi.h>
#pragma comment(lib, "winmm.lib")
#endif
#if defined(__x86_64__) || defined(_M_X64) || defined(_MSC_VER)
#include <immintrin.h>
#endif

const std::string ENGINE_CONFIG_FILE = "Config/EngineConfig.ini";
const std::string USER_CONFIG_FILE = "Config/UserConfig.ini";
GameConfig Config{};
const std::unordered_map<std::string, std::uint32_t> WinStyles = {
    {"none", Engine::WindowStyle_None},
    {"border", Engine::WindowStyle_Border},
    {"resizable", Engine::WindowStyle_Resizable},
    {"minimize", Engine::WindowStyle_Minimize},
    {"minimizebox", Engine::WindowStyle_Minimize},
    {"maximize", Engine::WindowStyle_Maximize},
    {"maximizebox", Engine::WindowStyle_Maximize},
    {"close", Engine::WindowStyle_Close},
    {"sysmenu", Engine::WindowStyle_Close},
    {"fullscreen", Engine::WindowStyle_Fullscreen},
    {"ws_overlappedwindow", Engine::WindowStyle_Default},
    {"ws_caption", Engine::WindowStyle_Border},
    {"ws_thickframe", Engine::WindowStyle_Resizable},
    {"ws_minimizebox", Engine::WindowStyle_Minimize},
    {"ws_maximizebox", Engine::WindowStyle_Maximize},
    {"ws_sysmenu", Engine::WindowStyle_Close},
    {"ws_popup", Engine::WindowStyle_Fullscreen},
};
Engine::Window* MainGameWindow = nullptr;

int RunEngine() {
    // LOAD CONFIG
    ini::IniFile engineConfig;
    if (engineConfig.load(ENGINE_CONFIG_FILE)) {
        if (engineConfig.contains("window")) {
            if (engineConfig["window"].contains("name"))
                Config.WindowName = engineConfig["window"]["name"].as<std::string>();
            if (engineConfig["window"].contains("style")) {
                std::string styleList = engineConfig["window"]["style"].as<std::string>();
                std::istringstream stream(styleList);
                std::string token;
                while (std::getline(stream, token, '|')) {
                    Trim(token);
                    const std::string normalized = ToLower(token);
                    if (normalized == "none") {
                        Config.WindowStyle = Engine::WindowStyle_None;
                        continue;
                    }
                    const auto it = WinStyles.find(normalized);
                    if (it != WinStyles.end())
                        Config.WindowStyle |= it->second;
                }
            }
            if (engineConfig["window"].contains("x")) {
                std::string value = engineConfig["window"]["x"].as<std::string>();
                if (value != "default")
                    TrimInt(value, Config.WindowPosX);
            }
            if (engineConfig["window"].contains("y")) {
                std::string value = engineConfig["window"]["y"].as<std::string>();
                if (value != "default")
                    TrimInt(value, Config.WindowPosY);
            }
            if (engineConfig["window"].contains("width")) {
                std::string value = engineConfig["window"]["width"].as<std::string>();
                if (value != "default")
                    TrimInt(value, Config.WindowSizeX);
            }
            if (engineConfig["window"].contains("height")) {
                std::string value =
                    engineConfig["window"]["height"].as<std::string>();
                if (value != "default")
                    TrimInt(value, Config.WindowSizeY);
            }
        }
    }
    Engine::WindowConfig windowConfig{};
    windowConfig.title = Config.WindowName;
    windowConfig.style = Config.WindowStyle;
    windowConfig.x = Config.WindowPosX;
    windowConfig.y = Config.WindowPosY;
    windowConfig.width = Config.WindowSizeX;
    windowConfig.height = Config.WindowSizeY;
    ini::IniFile userConfig;
    const bool isUserConfigLoaded = userConfig.load(USER_CONFIG_FILE);

    // CREATE CORE STUFF
    Engine::Input().Initialize((isUserConfigLoaded && userConfig.contains("keybinds")) ? userConfig["keybinds"] : ini::IniSection{});
    Console().Initialize((isUserConfigLoaded && userConfig.contains("convars")) ? userConfig["convars"] : ini::IniSection{});
    auto& logger = Engine::Logger::Get();
    if (!logger.Initialize(std::format("Engine/Logs/Log_{:%Y-%m-%d_%H-%M-%S}.txt", std::chrono::system_clock::now())))
        Console().Print("Failed to initialize logger", ConsoleColor::Error());
    auto& crashHandler = Engine::CrashHandler::Get();
    if (!crashHandler.Initialize())
        LOG_ERROR("Failed to initialize crash handler");
    else {
        LOG_CATEGORY_INFO("CrashHandler", "Crash handler initialized");
        LOG_INFO("Energy Engine starting");
        LOG_CATEGORY_INFO("Engine", "C++ logging system online");
    }

    // CRASH IF YUY IS MISSING
    bool requireYuY = false;
    if (Console().GetConVar("requireyuy"))
        requireYuY = Console().GetConVar("requireyuy")->GetBool();
    std::ifstream is("Engine/Main/YuY.png");
    if (!is.is_open() && requireYuY) {
        Engine::ShowMessageBox("YuY was lost to history, bring him back!", "The application cannot launch because a required component (../Engine/Main/YuY.png) is missing.\nPlease find a replacement.", Engine::MessageBoxType::Error, { Engine::MessageBoxButtons::Ok });
        LOG_CRITICAL("YuY was not found in ../Engine/Main/. Engine hates this");
        return -1;
    }

    // CREATE WINDOW
    MainGameWindow = new Engine::Window();
    if (!MainGameWindow->Create(windowConfig)) {
        LOG_CATEGORY_CRITICAL("Engine", "Window creation FAILED");
        Engine::ShowMessageBox("ENGINE INIT FAIL", "Window creation FAILED.\n\nPlease restart the app.\n\nIf that keeps happening, try to reinstall " + Config.WindowName, Engine::MessageBoxType::Error, { Engine::MessageBoxButtons::Ok });
        delete MainGameWindow;
        MainGameWindow = nullptr;
        return -1;
    }
    MainGameWindow->SetInputSink(&Engine::Input());
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    if (isUserConfigLoaded && userConfig.contains("gui") && userConfig["gui"].contains("style")) {
        std::string style = ToLower(userConfig["gui"]["style"].as<std::string>());
        if (style == "dark")
            ImGui::StyleColorsDark();
        else if (style == "light")
            ImGui::StyleColorsLight();
        else {
            if (Engine::DoesSystemUseDarkMode())
                ImGui::StyleColorsDark();
            else
                ImGui::StyleColorsLight();
        }
    }
    else {
        if (Engine::DoesSystemUseDarkMode())
            ImGui::StyleColorsDark();
        else
            ImGui::StyleColorsLight();
    }

    // INIT VULKAN
    auto& vulkan = Engine::Vulkan::Get();
    Engine::Vulkan::VulkanInitInfo vulkanInitInfo = vulkan.Initialize(MainGameWindow);
    if (!vulkanInitInfo.IsSuccess) {
        LOG_CATEGORY_CRITICAL("Vulkan", "Vulkan initialization FAILED with error {}. Error Code: {}", vulkanInitInfo.ErrorDescription, vulkanInitInfo.ErrorCode);
        Engine::ShowMessageBox("VULKAN INIT FAIL", vulkanInitInfo.ErrorDescription + "\n\nError Code: " + std::to_string(vulkanInitInfo.ErrorCode) + ".\n\nIf that keeps happening, try to reinstall " + Config.WindowName, Engine::MessageBoxType::Error, { Engine::MessageBoxButtons::Ok });
        vulkan.Shutdown();
        MainGameWindow->Destroy();
        delete MainGameWindow;
        MainGameWindow = nullptr;
        return -1;
    }
    if (!ImGui_ImplEngine_Init(MainGameWindow)) {
        LOG_CATEGORY_CRITICAL("ImGui", "ImGui initialization FAILED");
        Engine::ShowMessageBox("IMGUI INIT FAIL", "ImGui initialization FAILED.\n\nPlease restart the app.\n\nIf that keeps happening, try to reinstall " + Config.WindowName, Engine::MessageBoxType::Error, { Engine::MessageBoxButtons::Ok });
        ImGui::DestroyContext();
        MainGameWindow->Destroy();
        delete MainGameWindow;
        MainGameWindow = nullptr;
        return -1;
    }

    // TEST LEVEL
    Engine::LevelManager& levelManager = Engine::LevelManager::GetInstance();
    levelManager.SetPlayerFactory([]() { return std::make_unique<Game::Player>(); });
    auto level = levelManager.LoadLevel("test");
    vulkan.AssignPrimaryLevel(level);
    auto camera = std::make_shared<Engine::Camera>(Engine::Camera::Camera());
    auto player = level->GetCurrentPlayer();
    if (player) {
        player->AssignCamera(camera);
        //player->SetDiedCallback([](Engine::Player&) { Console().Print("[Player] Died."); });
        //player->SetRespawnedCallback([](Engine::Player&) { Console().Print("[Player] Respawned."); });
    }

    // RUNGAME
    bool running = true;
    bool respawnKeyHeld = false;
    bool lastCurVisible = true;
#if defined(_WIN32)
    bool m_IsOldWindows = false;
    HANDLE m_hTimer = CreateWaitableTimerExW(NULL, NULL, 0x00000002, TIMER_ALL_ACCESS);
    if (m_hTimer == NULL) {
        m_IsOldWindows = true;
        m_hTimer = CreateWaitableTimerW(NULL, FALSE, NULL);
        timeBeginPeriod(1);
    }
#endif
    static auto lastTime = std::chrono::high_resolution_clock::now();
    while (running && MainGameWindow && !MainGameWindow->ShouldClose()) {
        // UPDATE
        auto frameStart = std::chrono::high_resolution_clock::now();
        const auto startTime = lastTime;
        lastTime = frameStart;
        const double deltaTime = std::chrono::duration<double>(frameStart - startTime).count();
        //LOG_INFO("FPS: {}", 1.0 / deltaTime);
        MainGameWindow->PollEvents();
        if (MainGameWindow->ShouldClose())
            break;
        bool newcurvisible = !Engine::Input().IsInputAllowed() || !MainGameWindow->IsFocused() || MainGameWindow->IsMinimised();
        if (newcurvisible != lastCurVisible)
            MainGameWindow->SetCursorVisible(newcurvisible);
        lastCurVisible = newcurvisible;
        MainGameWindow->UpdateMouseState(!newcurvisible ? Engine::LockedToCenter : ((Engine::Input().GetMouseBounds() == nullptr || !MainGameWindow->IsFocused() || MainGameWindow->IsMinimised()) ? Engine::Free : Engine::LockedToArea), Engine::Input().GetMouseBounds());
        levelManager.UpdateAllLevels(deltaTime);
        camera->SetViewportSize({ static_cast<double>(MainGameWindow->FramebufferWidth()),static_cast<double>(MainGameWindow->FramebufferHeight()) });

        // DRAW IMGUI
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplEngine_NewFrame();
        ImGui::NewFrame();
        Console().Draw();
        ImGui::Render();

        if (!Console().GetConVar("rnd_renderwhenminimized") || Console().GetConVar("rnd_renderwhenminimized")->GetBool() || !MainGameWindow->IsMinimised())
            if (!vulkan.DrawFrame(deltaTime, *camera, [](VkCommandBuffer commandBuffer) {ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer); }))
                break;
        if (Console().GetConVar("rnd_vsync"))
            vulkan.SetVSync(Console().GetConVar("rnd_vsync")->GetBool());
        else
            vulkan.SetVSync(false);
        unsigned int fpsCap = 0;
        if (!MainGameWindow->IsFocused()) {
            if (Console().GetConVar("rnd_backgroundmaxfps"))
                fpsCap = Console().GetConVar("rnd_backgroundmaxfps")->GetInt();
            else
                fpsCap = 10;
        }
        else if (!vulkan.IsVSync())
            if (Console().GetConVar("rnd_maxfps"))
                fpsCap = Console().GetConVar("rnd_maxfps")->GetInt();
        const auto targetDuration = std::chrono::duration<double, std::milli>(1000.0 / fpsCap);
        auto frameEnd = std::chrono::high_resolution_clock::now();
        auto timeSpent = frameEnd - frameStart;
        if (fpsCap > 0 && (!MainGameWindow->IsFocused() || !vulkan.IsVSync()) && timeSpent < targetDuration) {
            auto timeRemaining = targetDuration - timeSpent;
#if defined(_WIN32)
            auto sleepBuffer = m_IsOldWindows ? std::chrono::microseconds(1500) : std::chrono::microseconds(200);
#else
            auto sleepBuffer = std::chrono::microseconds(250);
#endif
            if (timeRemaining > sleepBuffer) {
#if defined(_WIN32)
                LARGE_INTEGER liDueTime;
                liDueTime.QuadPart = -static_cast<LONGLONG>(std::chrono::duration_cast<std::chrono::nanoseconds>(timeRemaining - sleepBuffer).count() / 100);
                if (SetWaitableTimer(m_hTimer, &liDueTime, 0, NULL, NULL, FALSE)) 
                    WaitForSingleObject(m_hTimer, INFINITE);
#else
                std::this_thread::sleep_for(timeRemaining - sleepBuffer);
#endif
            }
            while (std::chrono::high_resolution_clock::now() - frameStart < targetDuration) {
#if defined(_MSC_VER) || defined(__x86_64__) || defined(_M_X64)
                _mm_pause();
#elif defined(__ARM_ARCH) || defined(__aarch64__)
                asm volatile("yield" ::: "memory");
#else
                std::this_thread::yield();
#endif
            }
        }
    }

    // SHUTDOWN
#if defined(_WIN32)
    if (m_hTimer != NULL)
        CloseHandle(m_hTimer);
    if (m_IsOldWindows)
        timeEndPeriod(1);
#endif
    ImGui_ImplEngine_Shutdown();
    ImGui_ImplVulkan_Shutdown();
    ImGui::DestroyContext();
    vulkan.Shutdown();
    if (MainGameWindow) {
        MainGameWindow->Destroy();
        delete MainGameWindow;
        MainGameWindow = nullptr;
    }
    logger.Shutdown();
    return 0;
}


#if defined(_WIN32)
#include <windows.h>
// ENTRY WHEN COMPILED WITH SUBSYSTEM /SUBSYSTEM:CONSOLE
int main() {
    // SACRIFICE CONSOLE FOR THE SAKE OF ACTUAL GAME
    ShowWindow(GetConsoleWindow(), SW_HIDE);
    FreeConsole();
	return RunEngine();
}
// ENTRY WHEN COMPILED WITH SUBSYSTEM /SUBSYSTEM:WINDOWS
int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ PSTR lpCmdLine, _In_ int nCmdShow) { return RunEngine(); }
#else
// CROSS-PLATFORM ENTRY WHEN COMPILED WITH SUBSYSTEM /SUBSYSTEM:CONSOLE
int main() { return RunEngine(); }
#endif