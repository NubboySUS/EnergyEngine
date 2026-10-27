#pragma once
#include "../APPLICATION/Engine/Main/Console.h"
#include <string>

struct ConsoleVariableDef
{
	std::string name;
	std::string defaultValue;
	std::string description;
	uint32_t flags;
};

const std::vector<ConsoleVariableDef> g_ConsoleVariables = {
	//template,		some value, "A template"							random flags

	// basic
	{ "devmode", "0", "Enable developer messages.", FCONVAR_ARCHIVE },
	{ "cheats", "0", "Allow cheat-protected commands.", FCONVAR_CHEAT },
	{ "noclip", "1", "Fly through anything.", FCONVAR_CHEAT },
	{ "requireyuy", "0", "Require YuY.png to be present in Engine/Main/ to launch.", FCONVAR_ARCHIVE },

	// player
	{ "plr_accelerate", "10", "How fast does player accelerate.", FCONVAR_CHEAT },
	{ "plr_friction", "4", "Player friction with the ground.", FCONVAR_CHEAT },
	{ "plr_maxspeed", "3500", "Player maximum move speed.", FCONVAR_CHEAT },
	{ "plr_noclipspeed", "10", "Player move speed when noclip is true.", FCONVAR_CHEAT },
	{ "plr_speedmultiplier", "1", "Player speed multiplier.", FCONVAR_CHEAT },

	// user config
	{ "cfg_sens", "1", "User mouse sensitivity.", FCONVAR_ARCHIVE },

	// render
	{ "rnd_maxfps", "0", "Game maximum Framerate. Zero is uncapped", FCONVAR_ARCHIVE },
	{ "rnd_backgroundmaxfps", "10", "Game maximum Framerate when window is Unfocused or Minimized. Zero is uncapped", FCONVAR_ARCHIVE },
	{ "rnd_vsync", "1", "Use Vertical Syncronization (VSync). Ignores rnd_maxfps if enabled", FCONVAR_ARCHIVE },
	{ "rnd_renderwhenminimized", "0", "Render new frames even when the main game window is minimized", FCONVAR_ARCHIVE },
	{ "rnd_visualizebakedlighting", "1", "Visualize the what lightbaking has created", FCONVAR_ARCHIVE },
};