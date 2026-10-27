#pragma once
#include "../APPLICATION/Engine/Main/Console.h"
#include "../APPLICATION/Engine/Main/Utils.h"
#include "../APPLICATION/Engine/Main/VulkanContext.h"
#include <string>
#include <vector>
#include <functional>

struct ConsoleCommandDef
{
    std::string name;
    std::string help;
    std::function<void(const std::vector<std::string>& args)> callback;
};

const std::vector<ConsoleCommandDef> g_ConsoleCommands = {
	{
		"echo",
		"Prints text to the console.",
		[](const std::vector<std::string>& args) {
			std::string text;
			for (size_t i = 1; i < args.size(); ++i) {
				if (!text.empty())
					text += ' ';
				text += args[i];
			}
			Console().Print(text);
		}
	},
	{
		"clear",
		"Clears the console.",
		[](const std::vector<std::string>&) { Console().Clear(); }
	},
	{
		"help",
		"Shows information about a command or convar.",
		[](const std::vector<std::string>& args) {
			if (args.size() == 1) {
				Console().Print("Available commands:", ConsoleColor::Info());
				std::vector<ConCommandBase*> entries;
				entries.reserve(Console().m_Commands.size());
				for (const auto& pair : Console().m_Commands)
					entries.push_back(pair.second);
				std::sort(entries.begin(),entries.end(), [](const auto* a, const auto* b) { return a->GetName() < b->GetName(); });
				for (const auto* entry : entries) {
					if (!entry->IsCommand())
						continue;
					std::string line = "[cmd] " + entry->GetName() + (entry->GetHelp().empty() ? "" : " - " + entry->GetHelp());
					Console().Print(line, ConsoleColor::White());
				}
				Console().Print("Available variables:", ConsoleColor::Info());
				for (const auto* entry : entries) {
					if (entry->IsCommand())
						continue;
					std::string line = "[var] " + entry->GetName() + (entry->GetHelp().empty() ? "" : " - " + entry->GetHelp());
					Console().Print(line, ConsoleColor::White());
				}
				return;
			}
			ConCommandBase* entry = Console().Find(args[1]);
			if (!entry) {
				Console().PrintF(ConsoleColor::Error(), "help: no command or convar named '%s'", args[1].c_str());
				return;
			}
			if (entry->IsCommand()) {
				Console().PrintF(ConsoleColor::Info(), "%s - command", entry->GetName().c_str());
				Console().Print(entry->GetHelp());
			}
			else {
				auto* variable = static_cast<ConVar*>(entry);
				Console().PrintF(ConsoleColor::Info(), "%s = \"%s\"", variable->GetName().c_str(), variable->GetString().c_str());
				Console().PrintF(ConsoleColor::Secondary(), "default: \"%s\"", variable->GetDefault().c_str());
				Console().Print(variable->GetHelp());
			}
		}
	},
	{
		"find",
		"Searches command and convar names.",
		[](const std::vector<std::string>& args) {
			if (args.size() < 2) {
				Console().Print("Usage: find <text>", ConsoleColor::Warning());
				return;
			}
			const std::string search = ToLower(args[1]);
			for (const auto& pair : Console().m_Commands) {
				if (pair.first.find(search) == std::string::npos)
					continue;
				const auto* entry = pair.second;
				Console().Print(std::string(entry->IsCommand() ? "[cmd] " : "[var] ") + entry->GetName(),ConsoleColor::White());
			}
		}
	},
	{
		"convarlist",
		"Lists all console variables.",
		[](const std::vector<std::string>&) {
			std::vector<ConVar*> variables;
			for (const auto& pair : Console().m_Commands) {
				if (!pair.second->IsCommand())
				variables.push_back(static_cast<ConVar*>(pair.second));
			}
			std::sort(variables.begin(),variables.end(), [](const ConVar* a, const ConVar* b) { return a->GetName() < b->GetName(); });
			for (const auto* variable : variables)
				Console().PrintF(ConsoleColor::White(),"%-24s = \"%s\"",variable->GetName().c_str(),variable->GetString().c_str());
		}
	},
	{
		"toggle",
		"Toggles a boolean convar.",
		[](const std::vector<std::string>& args) {
			if (args.size() < 2) {
				Console().Print("Usage: toggle <convar>", ConsoleColor::Warning());
				return;
			}
			ConCommandBase* base = Console().Find(args[1]);
			if (!base || base->IsCommand()) {
				Console().PrintF(ConsoleColor::Error(), "toggle: '%s' is not a convar", args[1].c_str());
				return;
			}
			auto* variable = static_cast<ConVar*>(base);
			variable->SetValue(variable->GetBool() ? "0" : "1");
		}
	},
	{
		"reset",
		"Resets a convar to its default value.",
		[](const std::vector<std::string>& args) {
			if (args.size() < 2) {
				Console().Print("Usage: reset <convar>", ConsoleColor::Warning());
				return;
			}
			ConCommandBase* base = Console().Find(args[1]);
			if (!base || base->IsCommand()) {
				Console().PrintF(ConsoleColor::Error(), "reset: '%s' is not a convar", args[1].c_str());
				return;
			}
			auto* variable = static_cast<ConVar*>(base);
			variable->SetValue(variable->GetDefault());
		}
	},
	{
		"history",
		"Prints command history.",
		[](const std::vector<std::string>&) {
			for (size_t i = 0; i < Console().m_History.size(); ++i)
				Console().Print(std::to_string(i + 1) + ": " + Console().m_History[i]);
		}
	},
	{
		"crash",
		"Intentionally crashes the process.",
		[](const std::vector<std::string>&) {
			volatile int* invalidPointer = nullptr;
			*invalidPointer = 42;
		}
	},
	{
		"rnd_bakelighting",
		"Bakes lighting and saves it to a .lightmap file",
		[](const std::vector<std::string>& args) {
			if (Engine::Vulkan::Get().BakeLighting())
				Console().Print("Light baking finished successfully", ConsoleColor::Info());
			else
				Console().Print("Light bake failed", ConsoleColor::Error());
		}
	}
};