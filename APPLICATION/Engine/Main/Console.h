//============== ENERGY ENGINE =================//
//                                              //
// Purpose: Declare stuff to make Console work  //
//                                              //
//==============================================//

#pragma once

#include <array>
#include <format>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <crtdbg.h>
#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <utility>
#include "Utils.h"
#include "FileParsers/inicpp.h"

struct ImVec4;
struct ImGuiInputTextCallbackData;
enum ConsoleVarFlags : uint32_t
{
    FCONVAR_NONE = 0,
    FCONVAR_ARCHIVE = 1 << 0,
    FCONVAR_CHEAT = 1 << 1,
    FCONVAR_HIDDEN = 1 << 2,
    FCONVAR_REPLICATE = 1 << 3
};
struct ConsoleColor
{
    float r = 0.862745f;
    float g = 0.862745f;
    float b = 0.862745f;
    float a = 1.0f;
    constexpr ConsoleColor() = default;
    constexpr ConsoleColor(float red, float green, float blue, float alpha = 1.0f): r(red), g(green), b(blue), a(alpha){}
    static constexpr ConsoleColor FromBytes(uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha = 255) {
        return ConsoleColor(static_cast<float>(red) / 255.0f,static_cast<float>(green) / 255.0f,static_cast<float>(blue) / 255.0f,static_cast<float>(alpha) / 255.0f);
    }
    static constexpr ConsoleColor White() { return FromBytes(220, 220, 220); }
    static constexpr ConsoleColor Error() { return FromBytes(255, 100, 100); }
    static constexpr ConsoleColor Warning() { return FromBytes(255, 200, 100); }
    static constexpr ConsoleColor Info() { return FromBytes(150, 210, 255); }
    static constexpr ConsoleColor Secondary() { return FromBytes(180, 180, 180); }
};
struct ConsoleLine
{
    std::string text;
    ConsoleColor color = ConsoleColor::White();
};
class ConCommandBase
{
public:
    ConCommandBase(std::string name, std::string help) : m_Name(std::move(name)), m_Help(std::move(help)) {};
    virtual ~ConCommandBase() = default;
    const std::string& GetName() const { return m_Name; }
    const std::string& GetHelp() const { return m_Help; }
    virtual bool IsCommand() const = 0;
protected:
    std::string m_Name;
    std::string m_Help;
};

class ConCommand final : public ConCommandBase
{
public:
    using Callback = std::function<void(const std::vector<std::string>& args)>;
    ConCommand(std::string name, std::string help, Callback callback) : ConCommandBase(std::move(name), std::move(help)), m_Callback(std::move(callback)) {};
    bool IsCommand() const override { return true; }
    void Dispatch(const std::vector<std::string>& args) const { if (m_Callback) m_Callback(args); };
private:
    Callback m_Callback;
};

class ConVar final : public ConCommandBase
{
public:
    using ChangeCallback = std::function<void(ConVar& variable, const std::string& oldValue, const std::string& newValue)>;
    ConVar(std::string name, std::string defaultValue, std::string help, uint32_t flags, ChangeCallback callback = nullptr) :
        ConCommandBase(std::move(name), std::move(help)), m_DefaultValue(std::move(defaultValue)), m_Value(m_DefaultValue), m_Flags(flags), m_Callback(std::move(callback)) {};
    bool IsCommand() const override { return false; }
    void SetValue(const std::string& value) {
        if (m_Value == value)
            return;
        std::string oldValue = m_Value;
        m_Value = value;
        if (m_Callback)
            m_Callback(*this, oldValue, m_Value);
    }
    const std::string& GetString() const { return m_Value; }
    const std::string& GetDefault() const { return m_DefaultValue; }
    int GetInt() const {
        try { return std::stoi(m_Value); }
        catch (...) { return 0; }
    }
    float GetFloat() const {
        try { return std::stof(m_Value); }
        catch (...) { return 0.0f; }
    }
    bool GetBool() const {
        const std::string value = ToLower(m_Value);
        return value == "1" || value == "true" || value == "yes" || value == "on";
    }
    uint32_t GetFlags() const { return m_Flags; }
private:
    std::string m_DefaultValue;
    std::string m_Value;
    uint32_t m_Flags = FCONVAR_NONE;
    ChangeCallback m_Callback;
};

class ConsoleSystem
{
public:
    static ConsoleSystem& Get();
    void Initialize(const ini::IniSection& userConVars);
    void Draw();
    void Toggle();
    void SetIsOpen(bool newIsOpen) { if (m_Open != newIsOpen) Toggle(); }
    bool IsOpen() const { return m_Open; }
    void SetBounds(int x, int y, int width, int height);
    bool Register(ConCommandBase& command);
    ConCommandBase* Find(std::string_view name) const;
    ConVar* GetConVar(std::string_view name) const;
    ConCommand* GetConCommand(std::string_view name) const;
    void Execute(std::string_view commandLine);
    void Print(std::string_view text, ConsoleColor color = ConsoleColor::White());
    template <typename... Args>
    void Print(ConsoleColor color, std::format_string<Args...> fmt, Args&&... args) { Print(std::vformat(fmt.get(), std::make_format_args(args...)), color); }
    void PrintF(ConsoleColor color, const char* format, ...);
    void Clear();
    const std::deque<ConsoleLine>& GetOutput() const { return m_Output; }
    std::unordered_map<std::string, ConCommandBase*> m_Commands;
    std::vector<std::string> m_History;
private:
    ConsoleSystem();
    ~ConsoleSystem() = default;
    ConsoleSystem(const ConsoleSystem&) = delete;
    ConsoleSystem& operator=(const ConsoleSystem&) = delete;
    void AddCommand(std::unique_ptr<ConCommand> command);
    void AddVariable(std::unique_ptr<ConVar> variable);
    static int InputTextCallback(ImGuiInputTextCallbackData* data);
    bool m_Initialized = false;
    bool m_InEditor = false;
    bool m_Open = false;
    bool m_RequestFocus = false;
    bool m_ScrollToBottom = false;
    int m_WindowX = 0;
    int m_WindowY = 0;
    int m_WindowWidth = 800;
    int m_WindowHeight = 560;
    bool m_HasBounds = false;
    std::array<char, 4096> m_InputBuffer{};
    int m_HistoryPosition = -1;
    std::deque<ConsoleLine> m_Output;
    size_t m_MaxOutputLines = 2000;
    std::vector<std::unique_ptr<ConCommand>> m_ConCommands;
    std::vector<std::unique_ptr<ConVar>> m_ConVars;
};

inline ConsoleSystem& Console() { return ConsoleSystem::Get(); }

#define PRINT(fmt, ...) \
    ::Engine::ConsoleSystem::Get().Print(ConsoleColor::White(), fmt, __VA_ARGS__)
#define PRINT_ERROR(fmt, ...) \
    ::Engine::ConsoleSystem::Get().Print(ConsoleColor::Error(), fmt, __VA_ARGS__)
#define PRINT_WARN(fmt, ...) \
    ::Engine::ConsoleSystem::Get().Print(ConsoleColor::Warning(), fmt, __VA_ARGS__)
#define PRINT_INFO(fmt, ...) \
    ::Engine::ConsoleSystem::Get().Print(ConsoleColor::Info(), fmt, __VA_ARGS__)
#define PRINT_SECONDARY(fmt, ...) \
    ::Engine::ConsoleSystem::Get().Print(ConsoleColor::Secondary(), fmt, __VA_ARGS__)