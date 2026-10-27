//============== ENERGY ENGINE =================//
//                                              //
// Purpose: Handle engine Console window        //
//                                              //
//==============================================//

#include "Console.h"
#include "../APPLICATION/Config/ConsoleCommands.h"
#include "../APPLICATION/Config/ConsoleVariables.h"
#include "Input.h"
#include "../ImGui/imgui.h"

ConsoleSystem& ConsoleSystem::Get() {
    static ConsoleSystem instance;
    return instance;
}
ConsoleSystem::ConsoleSystem() {
    // LOAD CONCOMMANDS
    for (const auto& def : g_ConsoleCommands)
        AddCommand(std::make_unique<ConCommand>(def.name, def.help, [def](const std::vector<std::string>& args) { def.callback(args); }));
    // LOAD CONVARS
    for (const auto& def : g_ConsoleVariables)
        AddVariable(std::make_unique<ConVar>(def.name, def.defaultValue, def.description, def.flags));
}
void ConsoleSystem::Initialize(const ini::IniSection& userConVars) {
    m_Initialized = true;
    if (m_History.empty())
        m_HistoryPosition = -1;
    Print("Engine console initialized.", ConsoleColor::Info());
#ifdef _DEBUG
    _CrtDbgReport(_CRT_WARN, nullptr, 0, nullptr, "%s\n", "\n\nPress ` to toggle the console.\n\n");
#endif
    Engine::Input().BindToAction("console", [](const Engine::CInput::InputMessage& event, bool isInputAllowed) {
            if (event.Type == Engine::CInput::ActionBegan)
                Console().Toggle();
        }, true);
    for (const auto& [convarName, field] : userConVars)
        if (m_Commands.find(convarName) != m_Commands.end() && !m_Commands[convarName]->IsCommand())
            static_cast<ConVar*>(m_Commands[convarName])->SetValue(field.as<std::string>());
}
void ConsoleSystem::AddCommand(std::unique_ptr<ConCommand> command) {
    Register(*command);
    m_ConCommands.push_back(std::move(command));
}
void ConsoleSystem::AddVariable(std::unique_ptr<ConVar> variable) {
    Register(*variable);
    m_ConVars.push_back(std::move(variable));
}
bool ConsoleSystem::Register(ConCommandBase& command) {
    const std::string key = ToLower(command.GetName());
    if (key.empty())
        return false;
    if (m_Commands.find(key) != m_Commands.end()) {
        PrintF(ConsoleColor::Error(), "Console: duplicate command/convar '%s'", command.GetName().c_str());
        return false;
    }
    m_Commands.emplace(key, &command);
    return true;
}
ConCommandBase* ConsoleSystem::Find(std::string_view name) const {
    const std::string key = ToLower(std::string(name));
    const auto it = m_Commands.find(key);
    if (it == m_Commands.end())
        return nullptr;
    return it->second;
}
ConVar* ConsoleSystem::GetConVar(std::string_view name) const {
    const std::string key = ToLower(std::string(name));
    const auto it = m_Commands.find(key);
    if (it == m_Commands.end() || it->second->IsCommand())
        return nullptr;
    ConVar* var = static_cast<ConVar*>(it->second);
    return var;
}
ConCommand* ConsoleSystem::GetConCommand(std::string_view name) const {
    const std::string key = ToLower(std::string(name));
    const auto it = m_Commands.find(key);
    if (it == m_Commands.end() || !it->second->IsCommand())
        return nullptr;
    ConCommand* cmd = static_cast<ConCommand*>(it->second);
    return cmd;
}
void ConsoleSystem::Execute(std::string_view commandLine) {
    for (const std::string& commandText : SplitCommands(commandLine)) {
        const std::vector<std::string> args = Tokenize(commandText);
        if (args.empty())
            continue;
        ConCommandBase* command = Find(args[0]);
        if (!command) {
            PrintF(ConsoleColor::Error(), "Unknown command: %s", args[0].c_str());
            continue;
        }
        if (command->IsCommand()) {
            static_cast<ConCommand*>(command)->Dispatch(args);
            continue;
        }
        auto* variable = static_cast<ConVar*>(command);
        if (args.size() == 1) {
            PrintF(ConsoleColor::White(), "%s = \"%s\" (default \"%s\")", variable->GetName().c_str(), variable->GetString().c_str(), variable->GetDefault().c_str());
            continue;
        }
        std::string newValue;
        for (size_t i = 1; i < args.size(); ++i) {
            if (!newValue.empty())
                newValue += ' ';
            newValue += args[i];
        }
        variable->SetValue(newValue);
        PrintF(ConsoleColor::Secondary(), "%s changed to \"%s\"", variable->GetName().c_str(), variable->GetString().c_str());
    }
}
void ConsoleSystem::Print(std::string_view text, ConsoleColor color) {
#ifdef _DEBUG
    _CrtDbgReport(_CRT_WARN, nullptr, 0, nullptr, "%.*s\n", static_cast<int>(text.size()), text.data());
#endif
    m_Output.push_back({ std::string(text), color });
    while (m_Output.size() > m_MaxOutputLines)
        m_Output.pop_front();
    m_ScrollToBottom = true;
}
void ConsoleSystem::PrintF(ConsoleColor color, const char* format, ...) {
    std::array<char, 8192> buffer{};
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer.data(), buffer.size(), format, args);
    va_end(args);
    buffer.back() = '\0';
    Print(buffer.data(), color);
}
void ConsoleSystem::Clear() {
    m_Output.clear();
    m_ScrollToBottom = true;
}
void ConsoleSystem::Toggle() {
    m_Open = !m_Open;
    m_RequestFocus = m_Open;
    if (m_Open) {
        Engine::Input().AddMouseUnlock("ENGINE_CONSOLE");
        m_HistoryPosition = -1;
    }
    else
        Engine::Input().RemoveMouseUnlock("ENGINE_CONSOLE");
}
void ConsoleSystem::SetBounds(int x, int y, int width, int height) {
    m_WindowX = x;
    m_WindowY = y;
    m_WindowWidth = std::max(width, 100);
    m_WindowHeight = std::max(height, 100);
    m_HasBounds = true;
}
void ConsoleSystem::Draw() {
    if (!m_Initialized)
        return;
    if (!m_Open)
        return;
    ImGui::SetNextWindowSizeConstraints(ImVec2(400.0f, 300.0f), ImVec2(FLT_MAX, FLT_MAX));
    ImGui::SetNextWindowSize(ImVec2(600.0f, 400.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(.96f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 6.0f));
    bool open = m_Open;
    if (ImGui::Begin("Console", &open, ImGuiWindowFlags_NoCollapse)) {
        SetIsOpen(open);
        ImGui::BeginChild("##ConsoleOutput", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing()), true, ImGuiWindowFlags_HorizontalScrollbar);
        for (const ConsoleLine& line : m_Output) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(line.color.r, line.color.g, line.color.b, line.color.a));
            ImGui::TextUnformatted(line.text.c_str());
            ImGui::PopStyleColor();
        }
        if (m_ScrollToBottom) {
            ImGui::SetScrollHereY(1.0f);
            m_ScrollToBottom = false;
        }
        ImGui::EndChild();
        if (m_RequestFocus) {
            ImGui::SetKeyboardFocusHere();
            m_RequestFocus = false;
        }
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::InputText("##ConsoleInput", m_InputBuffer.data(), m_InputBuffer.size(), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory | ImGuiInputTextFlags_CallbackAlways, &ConsoleSystem::InputTextCallback, this)) {
            const std::string command = m_InputBuffer.data();
            if (!command.empty()) {
                Print("] " + command, ConsoleColor::FromBytes(235, 235, 235));
                if (m_History.empty() || m_History.back() != command) {
                    m_History.push_back(command);
                    if (m_History.size() > 100)
                        m_History.erase(m_History.begin());
                }
                Execute(command);
            }
            m_InputBuffer[0] = '\0';
            m_HistoryPosition = -1;
            m_RequestFocus = true;
        }
    }
    ImGui::PopStyleVar();
    ImGui::End();
    SetIsOpen(open);
}
int ConsoleSystem::InputTextCallback(ImGuiInputTextCallbackData* data) {
    auto* console = static_cast<ConsoleSystem*>(data->UserData);
    if (!console)
        return 0;
    if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory) {
        if (console->m_History.empty())
            return 0;
        if (data->EventKey == ImGuiKey_UpArrow) {
            if (console->m_HistoryPosition == -1)
                console->m_HistoryPosition = static_cast<int>(console->m_History.size()) - 1;
            else if (console->m_HistoryPosition > 0)
                --console->m_HistoryPosition;
        }
        else if (data->EventKey == ImGuiKey_DownArrow) {
            if (console->m_HistoryPosition == -1)
                return 0;
            if (console->m_HistoryPosition + 1 >= static_cast<int>(console->m_History.size())) {
                console->m_HistoryPosition = -1;
                data->DeleteChars(0, data->BufTextLen);
                return 0;
            }
            ++console->m_HistoryPosition;
        }
        if (console->m_HistoryPosition >= 0 && console->m_HistoryPosition < static_cast<int>(console->m_History.size())) {
            const std::string& historyEntry = console->m_History[console->m_HistoryPosition];
            data->DeleteChars(0, data->BufTextLen);
            data->InsertChars(0, historyEntry.c_str());
        }
    }
    else if (data->EventFlag == ImGuiInputTextFlags_CallbackAlways) {
        // FIX VERY LONG COMMAND BUG
        data->Buf[data->BufTextLen] = '\0';
    }
    return 0;
}
