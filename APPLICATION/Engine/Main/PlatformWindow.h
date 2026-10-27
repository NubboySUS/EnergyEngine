//============== ENERGY ENGINE =================//
//                                              //
// Purpose: Unite OS stuff handlers together    //
//                                              //
//==============================================//

#pragma once
#if defined(_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#elif defined(__linux__)
#define VK_USE_PLATFORM_XLIB_KHR
#elif defined(__APPLE__)
#define VK_USE_PLATFORM_METAL_EXT
#endif
#include <vulkan/vulkan.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <functional>
#include "Input.h"

namespace Engine
{
    class CInput;
    enum WindowStyle : std::uint32_t
    {
        WindowStyle_None = 0,
        WindowStyle_Border = 1u << 0,
        WindowStyle_Resizable = 1u << 1,
        WindowStyle_Minimize = 1u << 2,
        WindowStyle_Maximize = 1u << 3,
        WindowStyle_Close = 1u << 4,
        WindowStyle_Default = WindowStyle_Border | WindowStyle_Resizable | WindowStyle_Minimize | WindowStyle_Maximize | WindowStyle_Close,
        WindowStyle_Fullscreen = 1u << 5,
    };
    struct WindowConfig
    {
        std::string title = "Game";
        std::uint32_t style = WindowStyle_Default;
        int x = -1;
        int y = -1;
        int width = -1;
        int height = -1;
    };
    enum MouseCursorShape : int
    {
        None = -1,
        Arrow = 0,
        Text,
        ResizeAll,
        ResizeEW,
        ResizeNS,
        ResizeNESW,
        ResizeNWSE,
        Hand,
        Wait,
        Progress,
        NotAllowed,
        _count,
    };
    enum class WindowEventType : int
    {
        None,
        CloseRequested,
        Resized,
        FocusChanged,
        Key,
        Text,
        MouseMove, // 2D UI
        MouseMoveRelative, // 3D CAMERA
        MouseButton,
        MouseWheel,
        MouseLeft,
    };
    struct WindowEvent
    {
        WindowEventType type = WindowEventType::None;
        KEYCODE key = KEY_INVALID;
        int modifiers = KEYMOD_NONE;
        bool pressed = false;
        bool focused = false;
        int mouseButton = -1;
        float x = 0.0f;
        float y = 0.0f;
        std::string text;
    };
    enum MessageBoxType : int
    {
        Warning,
        Info,
        Question,
        Error,
    };
    enum MessageBoxButtons : int
    {
        Ok,
        Cancel,
        Abort,
        Retry,
        Ignore,
        Yes,
        No,
        Continue,
        TryAgain,
    };
    enum MouseState
    {
        Free,
        LockedToArea,
        LockedToCenter
    };
    MessageBoxButtons ShowMessageBox(std::string title, std::string subtitle, MessageBoxType type, const std::vector<MessageBoxButtons>& buttons);
    bool DoesSystemUseDarkMode();
    class Window
    {
    public:
        struct Impl;
        Window();
        ~Window();
        Window(const Window&) = delete;
        Window& operator=(const Window&) = delete;
        bool Create(const WindowConfig& config);
        void Destroy();
        void PollEvents();
        void WaitEvents();
        void RequestClose();
        bool ShouldClose() const;
        bool IsMinimised() const;
        bool IsFocused() const;
        std::uint32_t Width() const;
        std::uint32_t Height() const;
        std::uint32_t FramebufferWidth() const;
        std::uint32_t FramebufferHeight() const;
        float ContentScale() const;
        const std::vector<WindowEvent>& Events() const;
        void ClearEvents();
        void SetInputSink(CInput* input);
        void SetCursor(MouseCursorShape shape);
        void SetCursorVisible(bool visible);
        void UpdateMouseState(MouseState state, Engine::CInput::MouseBounds* bounds = nullptr);
        void SetClipboardText(const char* text);
        void SetCloseRequestHandler(std::function<bool()> handler);
        const char* GetClipboardText();
        std::vector<const char*> GetRequiredVulkanInstanceExtensions() const;
        VkSurfaceKHR CreateVulkanSurface(VkInstance instance) const;
    private:
        std::unique_ptr<Impl> m_impl;
    };
}