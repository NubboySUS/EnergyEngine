//============== ENERGY ENGINE =================//
//                                              //
// Purpose: Tell PC how to launch the game      //
//                                              //
//==============================================//

#pragma once
#include "PlatformWindow.h"
#include <string>

struct GameConfig
{
    std::string WindowName = "Game";
    std::uint32_t WindowStyle = Engine::WindowStyle_Default;
    int WindowPosX = -1;
    int WindowPosY = -1;
    int WindowSizeX = -1;
    int WindowSizeY = -1;
};

extern Engine::Window* MainGameWindow;
extern GameConfig Config;
int RunEngine();