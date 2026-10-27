#include "imgui_impl_engine.h"
#include "../Main/PlatformWindow.h"

#include "../ImGui/imgui.h"

#include <chrono>
#include <cfloat>
#include <string>

namespace
{
    Engine::Window* g_Window = nullptr;
    std::chrono::steady_clock::time_point g_LastFrameTime{};
    ImGuiMouseCursor g_LastCursor = ImGuiMouseCursor_COUNT;
    bool g_LastMouseHidden = false;
    void SetClipboardText(void*, const char* text) { if (g_Window) g_Window->SetClipboardText(text); }
    const char* GetClipboardText(void*) { return g_Window ? g_Window->GetClipboardText() : ""; }
    Engine::MouseCursorShape ToEngineCursor(ImGuiMouseCursor cursor) {
        switch (cursor)
        {
        case ImGuiMouseCursor_TextInput:    return Engine::MouseCursorShape::Text;
        case ImGuiMouseCursor_ResizeAll:    return Engine::MouseCursorShape::ResizeAll;
        case ImGuiMouseCursor_ResizeEW:     return Engine::MouseCursorShape::ResizeEW;
        case ImGuiMouseCursor_ResizeNS:     return Engine::MouseCursorShape::ResizeNS;
        case ImGuiMouseCursor_ResizeNESW:   return Engine::MouseCursorShape::ResizeNESW;
        case ImGuiMouseCursor_ResizeNWSE:   return Engine::MouseCursorShape::ResizeNWSE;
        case ImGuiMouseCursor_Hand:         return Engine::MouseCursorShape::Hand;
        case ImGuiMouseCursor_NotAllowed:   return Engine::MouseCursorShape::NotAllowed;
        default:                            return Engine::MouseCursorShape::Arrow;
        }
    }
    void UpdateModifiers(ImGuiIO& io, int mods) {
        io.AddKeyEvent(ImGuiMod_Shift, (mods & KEYMOD_SHIFT) != 0);
        io.AddKeyEvent(ImGuiMod_Ctrl, (mods & KEYMOD_CTRL) != 0);
        io.AddKeyEvent(ImGuiMod_Alt, (mods & KEYMOD_ALT) != 0);
        io.AddKeyEvent(ImGuiMod_Super, (mods & KEYMOD_SUPER) != 0);
    }
}
bool ImGui_ImplEngine_Init(Engine::Window* window) {
    if (!window)
        return false;
    ImGuiIO& io = ImGui::GetIO();
    g_Window = window;
    g_LastFrameTime = std::chrono::steady_clock::now();
    g_LastCursor = ImGuiMouseCursor_COUNT;
    g_LastMouseHidden = false;
    io.BackendPlatformName = "imgui_impl_engine";
    io.BackendPlatformUserData = window;
    io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
    io.SetClipboardTextFn = SetClipboardText;
    io.GetClipboardTextFn = GetClipboardText;
    io.ClipboardUserData = window;
    return true;
}
void ImGui_ImplEngine_Shutdown() {
    if (!ImGui::GetCurrentContext())
        return;
    ImGuiIO& io = ImGui::GetIO();
    if (g_Window)
        g_Window->SetCursorVisible(true);
    io.BackendPlatformName = nullptr;
    io.BackendPlatformUserData = nullptr;
    io.ClipboardUserData = nullptr;
    io.SetClipboardTextFn = nullptr;
    io.GetClipboardTextFn = nullptr;
    io.BackendFlags &= ~ImGuiBackendFlags_HasMouseCursors;
    g_Window = nullptr;
    g_LastFrameTime = {};
}

void ImGui_ImplEngine_NewFrame() {
    if (!g_Window)
        return;
    ImGuiIO& io = ImGui::GetIO();
    const auto now = std::chrono::steady_clock::now();
    const float delta = std::chrono::duration<float>(now - g_LastFrameTime).count();
    g_LastFrameTime = now;
    io.DeltaTime = delta > 0.0f ? delta : (1.0f / 60.0f);
    io.DisplaySize = ImVec2(static_cast<float>(g_Window->Width()), static_cast<float>(g_Window->Height()));
    const float scale = g_Window->ContentScale();
    io.DisplayFramebufferScale = ImVec2(scale, scale);
    for (const Engine::WindowEvent& event : g_Window->Events()) {
        switch (event.type)
        {
        case Engine::WindowEventType::CloseRequested:
            io.AddFocusEvent(false);
            break;
        case Engine::WindowEventType::Resized:
            break;
        case Engine::WindowEventType::FocusChanged:
            io.AddFocusEvent(event.focused);
            if (!event.focused) {
                io.AddKeyEvent(ImGuiMod_Shift, false);
                io.AddKeyEvent(ImGuiMod_Ctrl, false);
                io.AddKeyEvent(ImGuiMod_Alt, false);
                io.AddKeyEvent(ImGuiMod_Super, false);
            }
            break;
        case Engine::WindowEventType::Key: {
            UpdateModifiers(io, event.modifiers);
            const ImGuiKey key = KeyCodeToImGuiKey(event.key);
            if (key != ImGuiKey_None)
                io.AddKeyEvent(key, event.pressed);
            break;
        }
        case Engine::WindowEventType::Text:
            if (!event.text.empty())
                io.AddInputCharactersUTF8(event.text.c_str());
            break;
        case Engine::WindowEventType::MouseMove:
            io.AddMousePosEvent(event.x, event.y);
            break;
        case Engine::WindowEventType::MouseButton:
            if (event.mouseButton >= 0 && event.mouseButton < 5)
                io.AddMouseButtonEvent(event.mouseButton, event.pressed);
            UpdateModifiers(io, event.modifiers);
            break;
        case Engine::WindowEventType::MouseWheel:
            UpdateModifiers(io, event.modifiers);
            io.AddMouseWheelEvent(event.x, event.y);
            break;
        case Engine::WindowEventType::MouseLeft:
            io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
            break;
        default:
            break;
        }
    }
    g_Window->ClearEvents();
    const bool hideMouse = io.MouseDrawCursor;
    if (hideMouse != g_LastMouseHidden) {
        g_Window->SetCursorVisible(!hideMouse);
        g_LastMouseHidden = hideMouse;
    }
    const ImGuiMouseCursor cursor = ImGui::GetMouseCursor();
    if (!hideMouse && cursor != ImGuiMouseCursor_None && cursor != g_LastCursor) {
        g_Window->SetCursor(ToEngineCursor(cursor));
        g_LastCursor = cursor;
    }
}
