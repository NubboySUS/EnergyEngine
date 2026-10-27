//============== ENERGY ENGINE =================//
//                                              //
// Purpose: Handle OS stuff on Win and Linux    //
//                                              //
//==============================================//

#if defined(_WIN32)
#include "PlatformWindow.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define VK_USE_PLATFORM_WIN32_KHR
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commctrl.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <utility>

#pragma comment(lib, "comctl32.lib")
#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' \
version='6.0.0.0' \
processorArchitecture='*' \
publicKeyToken='6595b64144ccf1df' \
language='*'\"")


namespace Engine
{
    namespace
    {
        int GetWin32Modifiers() {
            int mods = KEYMOD_NONE;
            if (GetKeyState(VK_SHIFT) & 0x8000) mods |= KEYMOD_SHIFT;
            if (GetKeyState(VK_CONTROL) & 0x8000) mods |= KEYMOD_CTRL;
            if (GetKeyState(VK_MENU) & 0x8000) mods |= KEYMOD_ALT;
            if ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) mods |= KEYMOD_SUPER;
            return mods;
        }
        KEYCODE MapVirtualKeyToEngine(WPARAM key, LPARAM lParam) {
            const bool extended = (lParam & (1ll << 24)) != 0;
            const UINT scancode = (static_cast<UINT>(lParam) >> 16) & 0xFFu;
            switch (key)
            {
            case VK_TAB:        return KEY_TAB;
            case VK_LEFT:       return KEY_LEFT;
            case VK_RIGHT:      return KEY_RIGHT;
            case VK_UP:         return KEY_UP;
            case VK_DOWN:       return KEY_DOWN;
            case VK_PRIOR:      return KEY_PAGEUP;
            case VK_NEXT:       return KEY_PAGEDOWN;
            case VK_HOME:       return KEY_HOME;
            case VK_END:        return KEY_END;
            case VK_INSERT:     return KEY_INSERT;
            case VK_DELETE:     return KEY_DELETE;
            case VK_BACK:       return KEY_BACKSPACE;
            case VK_SPACE:      return KEY_SPACE;
            case VK_RETURN:     return extended ? KEY_PAD_ENTER : KEY_ENTER;
            case VK_ESCAPE:     return KEY_ESCAPE;
            case VK_CAPITAL:    return KEY_CAPSLOCK;
            case VK_SCROLL:     return KEY_SCROLLLOCK;
            case VK_NUMLOCK:    return KEY_NUMLOCK;
            case VK_SNAPSHOT:   return KEY_PRINTSCREEN;
            case VK_PAUSE:      return KEY_BREAK;
            case VK_F1:         return KEY_F1;
            case VK_F2:         return KEY_F2;
            case VK_F3:         return KEY_F3;
            case VK_F4:         return KEY_F4;
            case VK_F5:         return KEY_F5;
            case VK_F6:         return KEY_F6;
            case VK_F7:         return KEY_F7;
            case VK_F8:         return KEY_F8;
            case VK_F9:         return KEY_F9;
            case VK_F10:        return KEY_F10;
            case VK_F11:        return KEY_F11;
            case VK_F12:        return KEY_F12;
            case VK_LSHIFT:     return KEY_LSHIFT;
            case VK_RSHIFT:     return KEY_RSHIFT;
            case VK_LCONTROL:   return KEY_LCONTROL;
            case VK_RCONTROL:   return KEY_RCONTROL;
            case VK_LMENU:      return KEY_LALT;
            case VK_RMENU:      return KEY_RALT;
            case VK_LWIN:       return KEY_LWIN;
            case VK_RWIN:       return KEY_RWIN;
            case VK_APPS:       return KEY_APP;
            case VK_OEM_3:      return KEY_BACKQUOTE;
            case VK_OEM_MINUS:  return KEY_MINUS;
            case VK_OEM_PLUS:   return KEY_EQUAL;
            case VK_OEM_4:      return KEY_LBRACKET;
            case VK_OEM_6:      return KEY_RBRACKET;
            case VK_OEM_5:      return KEY_BACKSLASH;
            case VK_OEM_1:      return KEY_SEMICOLON;
            case VK_OEM_7:      return KEY_APOSTROPHE;
            case VK_OEM_COMMA:  return KEY_COMMA;
            case VK_OEM_PERIOD: return KEY_PERIOD;
            case VK_OEM_2:      return KEY_SLASH;
            case VK_NUMPAD0:    return KEY_PAD_0;
            case VK_NUMPAD1:    return KEY_PAD_1;
            case VK_NUMPAD2:    return KEY_PAD_2;
            case VK_NUMPAD3:    return KEY_PAD_3;
            case VK_NUMPAD4:    return KEY_PAD_4;
            case VK_NUMPAD5:    return KEY_PAD_5;
            case VK_NUMPAD6:    return KEY_PAD_6;
            case VK_NUMPAD7:    return KEY_PAD_7;
            case VK_NUMPAD8:    return KEY_PAD_8;
            case VK_NUMPAD9:    return KEY_PAD_9;
            case VK_DECIMAL:    return KEY_PAD_DECIMAL;
            case VK_DIVIDE:     return KEY_PAD_DIVIDE;
            case VK_MULTIPLY:   return KEY_PAD_MULTIPLY;
            case VK_SUBTRACT:   return KEY_PAD_MINUS;
            case VK_ADD:        return KEY_PAD_PLUS;
            case VK_SEPARATOR:  return KEY_PAD_COMMA;
            default:            break;
            }
            if (key >= 'A' && key <= 'Z')
                return static_cast<KEYCODE>(static_cast<int>(KEY_A) + (key - 'A'));
            if (key >= '0' && key <= '9')
                return static_cast<KEYCODE>(static_cast<int>(KEY_0) + (key - '0'));
            if (key == VK_SHIFT)
                return MapVirtualKeyW(scancode, MAPVK_VSC_TO_VK_EX) == VK_RSHIFT ? KEY_RSHIFT : KEY_LSHIFT;
            if (key == VK_CONTROL)
                return extended ? KEY_RCONTROL : KEY_LCONTROL;
            if (key == VK_MENU)
                return extended ? KEY_RALT : KEY_LALT;
            return KEY_INVALID;
        }
        std::string Utf8FromUtf16(wchar_t codeUnit) {
            if (codeUnit == 0)
                return {};
            wchar_t data[2] = { codeUnit, L'\0' };
            const int size = WideCharToMultiByte(CP_UTF8, 0, data, 1, nullptr, 0, nullptr, nullptr);
            if (size <= 0)
                return {};
            std::string out(static_cast<std::size_t>(size), '\0');
            WideCharToMultiByte(CP_UTF8, 0, data, 1, out.data(), size, nullptr, nullptr);
            return out;
        }
        const char* CursorToWin32(MouseCursorShape shape) {
            switch (shape)
            {
            case MouseCursorShape::Text:        return IDC_IBEAM;
            case MouseCursorShape::ResizeEW:    return IDC_SIZEWE;
            case MouseCursorShape::ResizeNS:    return IDC_SIZENS;
            case MouseCursorShape::ResizeNESW:  return IDC_SIZENESW;
            case MouseCursorShape::ResizeNWSE:  return IDC_SIZENWSE;
            case MouseCursorShape::Hand:        return IDC_HAND;
            case MouseCursorShape::NotAllowed:  return IDC_NO;
            case MouseCursorShape::ResizeAll:   return IDC_SIZEALL;
            case MouseCursorShape::Wait:        return IDC_WAIT;
            case MouseCursorShape::Progress:    return IDC_APPSTARTING;
            default:                            return IDC_ARROW;
            }
        }
    }
    struct Window::Impl {
        HWND hwnd = nullptr;
        HINSTANCE instance = nullptr;
        std::string clipboard;
        std::vector<WindowEvent> events;
        bool shouldClose = false;
        bool minimized = false;
        bool focused = true;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        float scale = 1.0f;
        CInput* input = nullptr;
        wchar_t pendingHighSurrogate = 0;
        MouseState currentMouseState = MouseState::Free;
        Engine::CInput::MouseBounds currentBounds = { 0, 0, 0, 0 };
        RECT lastClipRect{};
        bool haveClipRect = false;
        bool mouseTracked = false;
        unsigned mouseButtonsDown = 0;
        bool haveRawAbsolutePosition = false;
        LONG lastRawAbsoluteX = 0;
        LONG lastRawAbsoluteY = 0;
        HCURSOR cursor = nullptr;
        bool cursorVisible = true;
        void PushEvent(const WindowEvent& event);
        void ReleaseMouseButtons() {
            const unsigned buttons = mouseButtonsDown;
            mouseButtonsDown = 0;
            for (int button = 0; button < 5; ++button)
                if (buttons & (1u << button))
                    PushEvent(WindowEvent{ WindowEventType::MouseButton, KEY_INVALID, KEYMOD_NONE, false, false, button });
        }
        void ReleaseMouseLock() {
            if (currentMouseState != MouseState::Free)
                ClipCursor(nullptr);
            currentMouseState = MouseState::Free;
            haveClipRect = false;
            haveRawAbsolutePosition = false;
        }
        std::function<bool()> closeRequestHandler;
        void HandleCloseRequest() {
            events.push_back({ WindowEventType::CloseRequested });
            if (!closeRequestHandler || closeRequestHandler())
                shouldClose = true;
        }
        static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
            auto* impl = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (msg == WM_NCCREATE) {
                auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
                impl = static_cast<Impl*>(create->lpCreateParams);
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(impl));
                if (impl)
                    impl->hwnd = hwnd;
            }
            if (!impl)
                return DefWindowProcW(hwnd, msg, wParam, lParam);
            auto& state = *impl;
            const int mods = GetWin32Modifiers();
            switch (msg)
            {
            case WM_CLOSE:
                state.HandleCloseRequest();
                return 0;
            case WM_DESTROY:
                state.ReleaseMouseLock();
                if (GetCapture() == hwnd)
                    ReleaseCapture();
                state.ReleaseMouseButtons();
                state.shouldClose = true;
                PostQuitMessage(0);
                return 0;
            case WM_SIZE:
                state.width = LOWORD(lParam);
                state.height = HIWORD(lParam);
                state.minimized = (wParam == SIZE_MINIMIZED) || state.width == 0 || state.height == 0;
                state.haveClipRect = false;
                if (state.minimized) {
                    state.ReleaseMouseLock();
                    if (GetCapture() == hwnd)
                        ReleaseCapture();
                    state.ReleaseMouseButtons();
                }
                state.PushEvent(WindowEvent{ WindowEventType::Resized });
                return 0;
            case WM_MOVE:
                state.haveClipRect = false;
                return 0;
            case WM_DPICHANGED: {
                state.scale = static_cast<float>(HIWORD(wParam)) / 96.0f;
                state.haveClipRect = false;
                const auto* rect = reinterpret_cast<const RECT*>(lParam);
                SetWindowPos(hwnd, nullptr, rect->left, rect->top,
                    rect->right - rect->left, rect->bottom - rect->top,
                    SWP_NOZORDER | SWP_NOACTIVATE);
                return 0;
            }
            case WM_SETFOCUS:
                state.focused = true;
                state.PushEvent(WindowEvent{ WindowEventType::FocusChanged, KEY_INVALID, KEYMOD_NONE, false, true });
                return 0;
            case WM_KILLFOCUS:
                state.focused = false;
                state.ReleaseMouseLock();
                if (GetCapture() == hwnd)
                    ReleaseCapture();
                state.ReleaseMouseButtons();
                state.pendingHighSurrogate = 0;
                state.PushEvent(WindowEvent{ WindowEventType::FocusChanged, KEY_INVALID, KEYMOD_NONE, false, false });
                return 0;
            case WM_SETCURSOR:
                if (LOWORD(lParam) == HTCLIENT) {
                    ::SetCursor(state.cursorVisible ? state.cursor : nullptr);
                    return TRUE;
                }
                break;
            case WM_MOUSEMOVE: {
                if (!state.mouseTracked) {
                    TRACKMOUSEEVENT tracking{ sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd, 0 };
                    state.mouseTracked = TrackMouseEvent(&tracking) != FALSE;
                }
                state.PushEvent(WindowEvent{ WindowEventType::MouseMove, KEY_INVALID, mods, false, false, -1,
                    static_cast<float>(GET_X_LPARAM(lParam)), static_cast<float>(GET_Y_LPARAM(lParam)) });
                return 0;
            }
            case WM_INPUT: {
                UINT size = sizeof(RAWINPUT);
                RAWINPUT raw{};
                if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER)) == sizeof(RAWINPUT)
                    && raw.header.dwType == RIM_TYPEMOUSE && state.focused) {
                    LONG dx = raw.data.mouse.lLastX;
                    LONG dy = raw.data.mouse.lLastY;
                    if (raw.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) {
                        const bool desktop = (raw.data.mouse.usFlags & MOUSE_VIRTUAL_DESKTOP) != 0;
                        const LONG x = (desktop ? GetSystemMetrics(SM_XVIRTUALSCREEN) : 0) + static_cast<LONG>(dx * (GetSystemMetrics(desktop ? SM_CXVIRTUALSCREEN : SM_CXSCREEN) - 1) / 65535.0);
                        const LONG y = (desktop ? GetSystemMetrics(SM_YVIRTUALSCREEN) : 0) + static_cast<LONG>(dy * (GetSystemMetrics(desktop ? SM_CYVIRTUALSCREEN : SM_CYSCREEN) - 1) / 65535.0);
                        dx = state.haveRawAbsolutePosition ? x - state.lastRawAbsoluteX : 0;
                        dy = state.haveRawAbsolutePosition ? y - state.lastRawAbsoluteY : 0;
                        state.lastRawAbsoluteX = x;
                        state.lastRawAbsoluteY = y;
                        state.haveRawAbsolutePosition = true;
                    }
                    else
                        state.haveRawAbsolutePosition = false;
                    if (dx != 0 || dy != 0)
                        state.PushEvent(WindowEvent{ WindowEventType::MouseMoveRelative, KEY_INVALID, mods, false, false, -1,
                            static_cast<float>(dx), static_cast<float>(dy) });
                }
                return DefWindowProcW(hwnd, msg, wParam, lParam);
            }
            case WM_MOUSELEAVE:
                state.mouseTracked = false;
                if (state.mouseButtonsDown == 0)
                    state.PushEvent(WindowEvent{ WindowEventType::MouseLeft });
                return 0;
            case WM_CAPTURECHANGED:
                if (reinterpret_cast<HWND>(lParam) != hwnd)
                    state.ReleaseMouseButtons();
                return 0;
            case WM_CANCELMODE:
                state.ReleaseMouseLock();
                if (GetCapture() == hwnd)
                    ReleaseCapture();
                state.ReleaseMouseButtons();
                return 0;
            case WM_LBUTTONDOWN:
            case WM_MBUTTONDOWN:
            case WM_RBUTTONDOWN:
            case WM_XBUTTONDOWN:
            case WM_LBUTTONUP:
            case WM_MBUTTONUP:
            case WM_RBUTTONUP:
            case WM_XBUTTONUP: {
                const bool pressed = msg == WM_LBUTTONDOWN || msg == WM_MBUTTONDOWN ||
                    msg == WM_RBUTTONDOWN || msg == WM_XBUTTONDOWN;
                int button = -1;
                switch (msg)
                {
                case WM_LBUTTONDOWN: case WM_LBUTTONUP: button = 0; break;
                case WM_RBUTTONDOWN: case WM_RBUTTONUP: button = 1; break;
                case WM_MBUTTONDOWN: case WM_MBUTTONUP: button = 2; break;
                case WM_XBUTTONDOWN: case WM_XBUTTONUP:
                    button = (GET_XBUTTON_WPARAM(wParam) == XBUTTON1) ? 3 : 4;
                    break;
                default: break;
                }
                state.PushEvent(WindowEvent{ WindowEventType::MouseMove, KEY_INVALID, mods, false, false, -1,
                    static_cast<float>(GET_X_LPARAM(lParam)), static_cast<float>(GET_Y_LPARAM(lParam)) });
                if (pressed) {
                    if (state.mouseButtonsDown == 0 && GetCapture() == nullptr)
                        SetCapture(hwnd);
                    state.mouseButtonsDown |= 1u << button;
                }
                else
                    state.mouseButtonsDown &= ~(1u << button);
                state.PushEvent(WindowEvent{ WindowEventType::MouseButton, KEY_INVALID, mods, pressed, false, button });
                if (!pressed && state.mouseButtonsDown == 0 && GetCapture() == hwnd)
                    ReleaseCapture();
                return (msg == WM_XBUTTONDOWN || msg == WM_XBUTTONUP) ? TRUE : 0;
            }
            case WM_MOUSEWHEEL:
            case WM_MOUSEHWHEEL: {
                const float amount = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / static_cast<float>(WHEEL_DELTA);
                state.PushEvent(WindowEvent{ WindowEventType::MouseWheel, KEY_INVALID, mods, false, false, -1, msg == WM_MOUSEHWHEEL ? amount : 0.0f, msg == WM_MOUSEWHEEL ? amount : 0.0f });
                return 0;
            }
            case WM_KEYDOWN:
            case WM_SYSKEYDOWN:
            case WM_KEYUP:
            case WM_SYSKEYUP: {
                const bool pressed = (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN);
                const KEYCODE key = MapVirtualKeyToEngine(wParam, lParam);
                if (key != KEY_INVALID)
                    state.PushEvent(WindowEvent{ WindowEventType::Key, key, mods, pressed });
                if (msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP)
                    return DefWindowProcW(hwnd, msg, wParam, lParam);
                return 0;
            }
            case WM_CHAR: {
                const wchar_t unit = static_cast<wchar_t>(wParam);
                if (unit >= 0xD800 && unit <= 0xDBFF) {
                    state.pendingHighSurrogate = unit;
                    return 0;
                }
                std::string utf8;
                if (unit >= 0xDC00 && unit <= 0xDFFF && state.pendingHighSurrogate) {
                    const std::uint32_t codepoint = 0x10000u + ((static_cast<std::uint32_t>(state.pendingHighSurrogate) - 0xD800u) << 10u) + (static_cast<std::uint32_t>(unit) - 0xDC00u);
                    char bytes[4]{};
                    bytes[0] = static_cast<char>(0xF0u | ((codepoint >> 18u) & 0x07u));
                    bytes[1] = static_cast<char>(0x80u | ((codepoint >> 12u) & 0x3Fu));
                    bytes[2] = static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3Fu));
                    bytes[3] = static_cast<char>(0x80u | (codepoint & 0x3Fu));
                    utf8.assign(bytes, bytes + 4);
                    state.pendingHighSurrogate = 0;
                }
                else {
                    state.pendingHighSurrogate = 0;
                    utf8 = Utf8FromUtf16(unit);
                }
                if (!utf8.empty())
                    state.PushEvent(WindowEvent{ WindowEventType::Text, KEY_INVALID, KEYMOD_NONE, false, false, -1, 0.0f, 0.0f, std::move(utf8) });
                return 0;
            }
            case WM_SYSCOMMAND:
                if ((wParam & 0xFFF0u) == SC_KEYMENU)
                    return 0;
                break;
            default:
                break;
            }
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }
    };
    void Window::SetCloseRequestHandler(std::function<bool()> handler) { m_impl->closeRequestHandler = std::move(handler); }
    void Window::Impl::PushEvent(const WindowEvent& event) {
        events.push_back(event);
        if (!input)
            return;
        switch (event.type)
        {
        case WindowEventType::Key: {
            if (event.key != KEY_INVALID) {
                if (event.pressed) input->KeyPressed(event.key, event.modifiers);
                else input->KeyReleased(event.key, event.modifiers);
            }
            break;
        }
        case WindowEventType::MouseMove:
            break;
        case WindowEventType::MouseMoveRelative:
            input->MouseMoveRelative(event.x, event.y, event.modifiers);
            break;
        case WindowEventType::MouseButton:
            if (event.pressed) input->MouseButtonPressed(event.mouseButton, event.modifiers);
            else input->MouseButtonReleased(event.mouseButton, event.modifiers);
            break;
        case WindowEventType::MouseWheel:
            input->MouseWheel(event.x, event.y, event.modifiers);
            break;
        case WindowEventType::Text:
            input->TextInput(event.text, event.modifiers);
            break;
        case WindowEventType::FocusChanged:
            input->FocusChanged(event.focused);
            break;
        default:
            break;
        }
    }
    Window::Window() : m_impl(std::make_unique<Impl>()) {}
    Window::~Window() { Destroy(); }
    bool Window::Create(const WindowConfig& config) {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        m_impl->instance = GetModuleHandleW(nullptr);
        const wchar_t* className = L"EnergyEngineNativeWindow";
        static bool classRegistered = false;
        if (!classRegistered) {
            WNDCLASSEXW wc{};
            wc.cbSize = sizeof(wc);
            wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
            wc.lpfnWndProc = Window::Impl::WndProc;
            wc.hInstance = m_impl->instance;
#ifdef _DEBUG
            wc.hCursor = LoadCursorA(nullptr, IDC_ARROW);
            wc.hIcon = LoadIconA(nullptr, IDI_APPLICATION);
#else
            wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(IDC_ARROW));
            wc.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(IDI_APPLICATION));
#endif
            wc.lpszClassName = className;
            if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
                return false;
            classRegistered = true;
        }
        DWORD style = 0;
        if (config.style & WindowStyle_Border)
            style |= WS_CAPTION;
        if (config.style & WindowStyle_Resizable)
            style |= WS_THICKFRAME;
        if (config.style & WindowStyle_Minimize)
            style |= WS_MINIMIZEBOX;
        if (config.style & WindowStyle_Maximize)
            style |= WS_MAXIMIZEBOX;
        if (config.style & WindowStyle_Close)
            style |= WS_SYSMENU;
        if (config.style & WindowStyle_Fullscreen)
            style = WS_POPUP;
        if (style == 0)
            style = WS_POPUP;
        const int desiredWidth = config.width > 0 ? config.width : 1280;
        const int desiredHeight = config.height > 0 ? config.height : 720;
        RECT rect{ 0, 0, desiredWidth, desiredHeight };
        AdjustWindowRectEx(&rect, style, FALSE, 0);
        const int x = config.x >= 0 ? config.x : CW_USEDEFAULT;
        const int y = config.y >= 0 ? config.y : CW_USEDEFAULT;
        const int width = rect.right - rect.left;
        const int height = rect.bottom - rect.top;
        std::wstring wideTitle(config.title.begin(), config.title.end());
        HWND hwnd = CreateWindowExW(0, className, wideTitle.c_str(), style, x, y, width, height, nullptr, nullptr, m_impl->instance, m_impl.get());
        if (!hwnd)
            return false;
        m_impl->hwnd = hwnd;
#ifdef _DEBUG
        m_impl->cursor = LoadCursorA(nullptr, IDC_ARROW);
#else
        m_impl->cursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(IDC_ARROW));
#endif
        RECT client{};
        GetClientRect(hwnd, &client);
        m_impl->width = static_cast<std::uint32_t>((std::max)(0L, client.right - client.left));
        m_impl->height = static_cast<std::uint32_t>((std::max)(0L, client.bottom - client.top));
        m_impl->scale = static_cast<float>(GetDpiForWindow(hwnd)) / 96.0f;
        m_impl->minimized = false;
        m_impl->focused = true;
        RAWINPUTDEVICE rid{};
        rid.usUsagePage = 0x01;
        rid.usUsage = 0x02;
        rid.dwFlags = 0;
        rid.hwndTarget = m_impl->hwnd;
        RegisterRawInputDevices(&rid, 1, sizeof(rid));
        if (config.style & WindowStyle_Fullscreen)
            ShowWindow(hwnd, SW_MAXIMIZE);
        else
            ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
        SetForegroundWindow(hwnd);
        SetFocus(hwnd);
        return true;
    }
    void Window::Destroy() {
        if (m_impl && m_impl->hwnd) {
            m_impl->ReleaseMouseLock();
            if (GetCapture() == m_impl->hwnd)
                ReleaseCapture();
            SetCursorVisible(true);
            DestroyWindow(m_impl->hwnd);
            m_impl->hwnd = nullptr;
        }
    }
    void Window::PollEvents() {
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                m_impl->shouldClose = true;
                continue;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (m_impl->hwnd && m_impl->focused && !m_impl->minimized) {
            POINT cursor{};
            if (GetCursorPos(&cursor) && ScreenToClient(m_impl->hwnd, &cursor)) {
                const bool inside = cursor.x >= 0 && cursor.y >= 0
                    && cursor.x < static_cast<LONG>(m_impl->width) && cursor.y < static_cast<LONG>(m_impl->height);
                if (inside || GetCapture() == m_impl->hwnd)
                    m_impl->PushEvent(WindowEvent{ WindowEventType::MouseMove, KEY_INVALID, GetWin32Modifiers(), false, false, -1,
                        static_cast<float>(cursor.x), static_cast<float>(cursor.y) });
                else if (m_impl->mouseButtonsDown == 0)
                    m_impl->PushEvent(WindowEvent{ WindowEventType::MouseLeft });
            }
        }
    }
    void Window::WaitEvents() { WaitMessage(); }
    void Window::RequestClose() { m_impl->shouldClose = true; }
    bool Window::ShouldClose() const { return m_impl->shouldClose; }
    bool Window::IsMinimised() const { return m_impl->minimized; }
    bool Window::IsFocused() const { return m_impl->focused; }
    std::uint32_t Window::Width() const { return m_impl->width; }
    std::uint32_t Window::Height() const { return m_impl->height; }
    std::uint32_t Window::FramebufferWidth() const { return m_impl->width; }
    std::uint32_t Window::FramebufferHeight() const { return m_impl->height; }
    float Window::ContentScale() const { return m_impl->scale; }
    const std::vector<WindowEvent>& Window::Events() const { return m_impl->events; }
    void Window::ClearEvents() { m_impl->events.clear(); }
    void Window::SetInputSink(CInput* input) { m_impl->input = input; }
    void Window::SetCursor(MouseCursorShape shape) {
        if (!m_impl->hwnd)
            return;
#ifdef _DEBUG
        m_impl->cursor = LoadCursorA(nullptr, CursorToWin32(shape));
#else
        m_impl->cursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(CursorToWin32(shape)));
#endif
        if (m_impl->cursorVisible)
            ::SetCursor(m_impl->cursor);
    }
    void Window::SetCursorVisible(bool visible) {
        if (!m_impl->hwnd || m_impl->cursorVisible == visible)
            return;
        m_impl->cursorVisible = visible;
        if (visible)
            while (ShowCursor(TRUE) < 0) {}
        else
            while (ShowCursor(FALSE) >= 0) {}
        ::SetCursor(visible ? m_impl->cursor : nullptr);
    }
    void Window::SetClipboardText(const char* text) {
        m_impl->clipboard = text ? text : "";
        if (!OpenClipboard(m_impl->hwnd))
            return;
        EmptyClipboard();
        const int bytes = MultiByteToWideChar(CP_UTF8, 0, m_impl->clipboard.c_str(), -1, nullptr, 0);
        if (bytes > 0) {
            HGLOBAL data = GlobalAlloc(GMEM_MOVEABLE, static_cast<SIZE_T>(bytes) * sizeof(wchar_t));
            if (data) {
                void* ptr = GlobalLock(data);
                MultiByteToWideChar(CP_UTF8, 0, m_impl->clipboard.c_str(), -1,
                    static_cast<wchar_t*>(ptr), bytes);
                GlobalUnlock(data);
                SetClipboardData(CF_UNICODETEXT, data);
            }
        }
        CloseClipboard();
    }
    const char* Window::GetClipboardText() {
        if (!OpenClipboard(m_impl->hwnd))
            return m_impl->clipboard.c_str();
        HANDLE handle = GetClipboardData(CF_UNICODETEXT);
        if (handle) {
            const wchar_t* text = static_cast<const wchar_t*>(GlobalLock(handle));
            if (text) {
                const int bytes = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
                if (bytes > 0) {
                    m_impl->clipboard.resize(static_cast<std::size_t>(bytes - 1));
                    WideCharToMultiByte(CP_UTF8, 0, text, -1, m_impl->clipboard.data(), bytes, nullptr, nullptr);
                }
                GlobalUnlock(handle);
            }
        }
        CloseClipboard();
        return m_impl->clipboard.c_str();
    }
    std::vector<const char*> Window::GetRequiredVulkanInstanceExtensions() const { return { VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME, }; }
    VkSurfaceKHR Window::CreateVulkanSurface(VkInstance instance) const {
        VkWin32SurfaceCreateInfoKHR createInfo{ VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR };
        createInfo.hinstance = m_impl->instance;
        createInfo.hwnd = m_impl->hwnd;
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        if (vkCreateWin32SurfaceKHR(instance, &createInfo, nullptr, &surface) != VK_SUCCESS)
            return VK_NULL_HANDLE;
        return surface;
    }
    std::wstring Utf8ToWide(const std::string& str) {
        if (str.empty()) return L"";
        int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
        std::wstring wstrTo(size_needed, 0);
        MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
        return wstrTo;
    }
    MessageBoxButtons ShowMessageBox(std::string title, std::string subtitle, MessageBoxType type, const std::vector<MessageBoxButtons>& buttons) {
        if (buttons.empty()) return Ok;
        std::wstring wTitle = Utf8ToWide(title);
        std::wstring wSubtitle = Utf8ToWide(subtitle);
        TASKDIALOGCONFIG config = { 0 };
        config.cbSize = sizeof(TASKDIALOGCONFIG);
        config.hwndParent = NULL;
        config.pszWindowTitle = wTitle.c_str();
        config.pszMainInstruction = wSubtitle.c_str();
        switch (type) {
        case Warning:  config.pszMainIcon = TD_WARNING_ICON; break;
        case Info:     config.pszMainIcon = TD_INFORMATION_ICON; break;
        case Question: config.pszMainIcon = TD_INFORMATION_ICON; break;
        case Error:    config.pszMainIcon = TD_ERROR_ICON; break;
        }
        std::vector<TASKDIALOG_BUTTON> tdButtons(buttons.size());
        for (size_t i = 0; i < buttons.size(); ++i) {
            std::wstring label = L"";
            switch (buttons[i]) {
            case Ok:       label = L"OK";       break;
            case Cancel:   label = L"Cancel";   break;
            case Abort:    label = L"Abort";    break;
            case Retry:    label = L"Retry";    break;
            case Ignore:   label = L"Ignore";   break;
            case Yes:      label = L"Yes";      break;
            case No:       label = L"No";       break;
            case Continue: label = L"Continue"; break;
            case TryAgain: label = L"Try Again"; break;
            default:       label = L"OK";       break;
            }
            tdButtons[i].nButtonID = 1000 + static_cast<int>(i);
            static std::vector<std::wstring> labels;
            labels.push_back(label);
            tdButtons[i].pszButtonText = labels.back().c_str();
        }
        config.pButtons = tdButtons.data();
        config.cButtons = static_cast<UINT>(tdButtons.size());
        config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION;
        int pressedButtonId = 0;
        HRESULT hr = TaskDialogIndirect(&config, &pressedButtonId, NULL, NULL);
        if (FAILED(hr)) {
            return buttons[0];
        }
        int clickedIndex = pressedButtonId - 1000;
        if (clickedIndex >= 0 && clickedIndex < static_cast<int>(buttons.size())) { return buttons[clickedIndex]; }
        return buttons[0];
    }
    bool DoesSystemUseDarkMode() {
        HKEY hKey;
        DWORD value = 1;
        DWORD size = sizeof(value);
        if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            RegQueryValueExA(hKey, "AppsUseLightTheme", NULL, NULL, (LPBYTE)&value, &size);
            RegCloseKey(hKey);
        }
        return value == 0;
    }
    void Window::UpdateMouseState(MouseState state, Engine::CInput::MouseBounds* bounds) {
        if (!m_impl->hwnd)
            return;
        if (!m_impl->focused || m_impl->minimized)
            state = MouseState::Free;
        if (state == MouseState::Free) {
            m_impl->ReleaseMouseLock();
            return;
        }
        RECT client{};
        if (!GetClientRect(m_impl->hwnd, &client))
            return;
        RECT clip{};
        if (state == MouseState::LockedToCenter) {
            clip.left = client.right / 2;
            clip.top = client.bottom / 2;
            clip.right = clip.left + 1;
            clip.bottom = clip.top + 1;
        }
        else if (bounds) {
            clip.left = std::clamp<LONG>(bounds->left, 0, (std::max)(0L, client.right - 1));
            clip.top = std::clamp<LONG>(bounds->top, 0, (std::max)(0L, client.bottom - 1));
            clip.right = std::clamp<LONG>(bounds->right, clip.left + 1, (std::max)(clip.left + 1, client.right));
            clip.bottom = std::clamp<LONG>(bounds->bottom, clip.top + 1, (std::max)(clip.top + 1, client.bottom));
        }
        else
            clip = client;
        POINT topLeft{ clip.left, clip.top };
        POINT bottomRight{ clip.right, clip.bottom };
        ClientToScreen(m_impl->hwnd, &topLeft);
        ClientToScreen(m_impl->hwnd, &bottomRight);
        clip = { topLeft.x, topLeft.y, bottomRight.x, bottomRight.y };
        const RECT& previous = m_impl->lastClipRect;
        const bool sameClip = m_impl->haveClipRect && clip.left == previous.left && clip.top == previous.top
            && clip.right == previous.right && clip.bottom == previous.bottom;
        if (m_impl->currentMouseState == state && sameClip)
            return;
        if (ClipCursor(&clip)) {
            m_impl->currentMouseState = state;
            m_impl->lastClipRect = clip;
            m_impl->haveClipRect = true;
        }
    }
}

#elif defined(__linux__)
#include "PlatformWindow.h"
#define VK_USE_PLATFORM_XLIB_KHR
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>
#include <utility>

namespace Engine
{
    namespace
    {
        int X11Modifiers(const XKeyEvent& event) {
            const unsigned state = event.state;
            int mods = KEYMOD_NONE;
            if (state & ShiftMask) mods |= KEYMOD_SHIFT;
            if (state & ControlMask) mods |= KEYMOD_CTRL;
            if (state & Mod1Mask) mods |= KEYMOD_ALT;
            if (state & Mod4Mask) mods |= KEYMOD_SUPER;
            return mods;
        }
        KEYCODE KeysymToEngine(KeySym sym) {
            if (sym >= XK_A && sym <= XK_Z)
                return static_cast<KEYCODE>(static_cast<int>(KEY_A) + static_cast<int>(sym - XK_A));
            if (sym >= XK_a && sym <= XK_z)
                return static_cast<KEYCODE>(static_cast<int>(KEY_A) + static_cast<int>(sym - XK_a));
            if (sym >= XK_0 && sym <= XK_9)
                return static_cast<KEYCODE>(static_cast<int>(KEY_0) + static_cast<int>(sym - XK_0));
            if (sym >= XK_KP_0 && sym <= XK_KP_9)
                return static_cast<KEYCODE>(static_cast<int>(KEY_PAD_0) + static_cast<int>(sym - XK_KP_0));

            switch (sym)
            {
            case XK_Tab:          return KEY_TAB;
            case XK_Left:         return KEY_LEFT;
            case XK_Right:        return KEY_RIGHT;
            case XK_Up:           return KEY_UP;
            case XK_Down:         return KEY_DOWN;
            case XK_Page_Up:      return KEY_PAGEUP;
            case XK_Page_Down:    return KEY_PAGEDOWN;
            case XK_Home:         return KEY_HOME;
            case XK_End:          return KEY_END;
            case XK_Insert:       return KEY_INSERT;
            case XK_Delete:       return KEY_DELETE;
            case XK_BackSpace:    return KEY_BACKSPACE;
            case XK_space:        return KEY_SPACE;
            case XK_Return:       return KEY_ENTER;
            case XK_KP_Enter:     return KEY_PAD_ENTER;
            case XK_Escape:       return KEY_ESCAPE;
            case XK_Caps_Lock:    return KEY_CAPSLOCK;
            case XK_Scroll_Lock:  return KEY_SCROLLLOCK;
            case XK_Num_Lock:     return KEY_NUMLOCK;
            case XK_Print:        return KEY_PRINTSCREEN;
            case XK_Pause:        return KEY_BREAK;
            case XK_F1:           return KEY_F1;
            case XK_F2:           return KEY_F2;
            case XK_F3:           return KEY_F3;
            case XK_F4:           return KEY_F4;
            case XK_F5:           return KEY_F5;
            case XK_F6:           return KEY_F6;
            case XK_F7:           return KEY_F7;
            case XK_F8:           return KEY_F8;
            case XK_F9:           return KEY_F9;
            case XK_F10:          return KEY_F10;
            case XK_F11:          return KEY_F11;
            case XK_F12:          return KEY_F12;
            case XK_Shift_L:      return KEY_LSHIFT;
            case XK_Shift_R:      return KEY_RSHIFT;
            case XK_Control_L:    return KEY_LCONTROL;
            case XK_Control_R:    return KEY_RCONTROL;
            case XK_Alt_L:        return KEY_LALT;
            case XK_Alt_R:        return KEY_RALT;
            case XK_Super_L:      return KEY_LWIN;
            case XK_Super_R:      return KEY_RWIN;
            case XK_Menu:         return KEY_APP;
            case XK_grave:        return KEY_BACKQUOTE;
            case XK_minus:        return KEY_MINUS;
            case XK_equal:        return KEY_EQUAL;
            case XK_bracketleft:  return KEY_LBRACKET;
            case XK_bracketright: return KEY_RBRACKET;
            case XK_backslash:    return KEY_BACKSLASH;
            case XK_semicolon:    return KEY_SEMICOLON;
            case XK_apostrophe:   return KEY_APOSTROPHE;
            case XK_comma:        return KEY_COMMA;
            case XK_period:       return KEY_PERIOD;
            case XK_slash:        return KEY_SLASH;
            case XK_KP_Decimal:   return KEY_PAD_DECIMAL;
            case XK_KP_Divide:    return KEY_PAD_DIVIDE;
            case XK_KP_Multiply:  return KEY_PAD_MULTIPLY;
            case XK_KP_Subtract:  return KEY_PAD_MINUS;
            case XK_KP_Add:       return KEY_PAD_PLUS;
            case XK_KP_Equal:     return KEY_PAD_EQUAL;
            default:              return KEY_INVALID;
            }
        }
        Cursor XCursorFor(Display* display, MouseCursorShape shape) {
            switch (shape)
            {
            case MouseCursorShape::Text:        return XCreateFontCursor(display, XC_xterm);
            case MouseCursorShape::ResizeEW:    return XCreateFontCursor(display, XC_sb_h_double_arrow);
            case MouseCursorShape::ResizeNS:    return XCreateFontCursor(display, XC_sb_v_double_arrow);
            case MouseCursorShape::ResizeNESW:  return XCreateFontCursor(display, XC_sizing);
            case MouseCursorShape::ResizeNWSE:  return XCreateFontCursor(display, XC_sizing);
            case MouseCursorShape::Hand:        return XCreateFontCursor(display, XC_hand2);
            case MouseCursorShape::NotAllowed:  return XCreateFontCursor(display, XC_circle);
            case MouseCursorShape::ResizeAll:   return XCreateFontCursor(display, XC_fleur);
            case MouseCursorShape::Wait:        return XCreateFontCursor(display, XC_clock);
            case MouseCursorShape::Progress:    return XCreateFontCursor(display, XC_watch);
            default:                      return XCreateFontCursor(display, XC_left_ptr);
            }
        }
    }
    struct Window::Impl {
        Display* display = nullptr;
        ::Window window = 0;
        Atom wmDelete = None;
        Atom clipboardAtom = None;
        Atom utf8Atom = None;
        Atom targetsAtom = None;
        Atom clipboardProperty = None;
        XIM xim = nullptr;
        XIC xic = nullptr;
        Cursor cursor = None;
        std::string clipboard;
        std::vector<WindowEvent> events;
        bool shouldClose = false;
        bool minimized = false;
        bool focused = true;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        float scale = 1.0f;
        CInput* input = nullptr;
        ::Window clipAreaWindow = 0;
        ::Window clipCenterWindow = 0;
        MouseState currentMouseState = MouseState::Free;
        Engine::CInput::MouseBounds currentBounds = { 0, 0, 0, 0 };
        std::uint32_t lastCachedWidth = 0;
        std::uint32_t lastCachedHeight = 0;
        std::function<bool()> closeRequestHandler;
        void HandleCloseRequest() {
            events.push_back({ WindowEventType::CloseRequested });
            if (!closeRequestHandler || closeRequestHandler())
                shouldClose = true;
        }
    };
    void Window::SetCloseRequestHandler(std::function<bool()> handler) { m_impl->closeRequestHandler = std::move(handler); }
    void Window::Impl::PushEvent(const WindowEvent& event) {
        events.push_back(event);
        if (!input) return;
        switch (event.type)
        {
        case WindowEventType::Key: {
            if (event.key != KEY_INVALID) {
                if (event.pressed) input->KeyPressed(event.key, event.modifiers);
                else input->KeyReleased(event.key, event.modifiers);
            }
            break;
        }
        case WindowEventType::MouseMove: input->MouseMove(event.x, event.y, event.modifiers); break;
        case WindowEventType::MouseButton:
            if (event.pressed) input->MouseButtonPressed(event.mouseButton, event.modifiers);
            else input->MouseButtonReleased(event.mouseButton, event.modifiers);
            break;
        case WindowEventType::MouseWheel: input->MouseWheel(event.x, event.y, event.modifiers); break;
        case WindowEventType::Text: input->TextInput(event.text, event.modifiers); break;
        case WindowEventType::FocusChanged: input->FocusChanged(event.focused); break;
        default: break;
        }
    }
    Window::Window() : m_impl(std::make_unique<Impl>()) {}
    Window::~Window() { Destroy(); }
    bool Window::Create(const WindowConfig& config) {
        m_impl->display = XOpenDisplay(nullptr);
        if (!m_impl->display)
            return false;
        const int screen = DefaultScreen(m_impl->display);
        const ::Window root = RootWindow(m_impl->display, screen);
        const int width = config.width > 0 ? config.width : 1280;
        const int height = config.height > 0 ? config.height : 720;
        XSetWindowAttributes attrs{};
        attrs.event_mask = KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | FocusChangeMask | StructureNotifyMask | ExposureMask | LeaveWindowMask | PropertyChangeMask;
        unsigned long eventMask = attrs.event_mask;
        m_impl->window = XCreateWindow(m_impl->display, root, config.x >= 0 ? config.x : 0, config.y >= 0 ? config.y : 0, static_cast<unsigned>(width), static_cast<unsigned>(height), 0, CopyFromParent, InputOutput, CopyFromParent, CWEventMask, &attrs);
        if (!m_impl->window) {
            XCloseDisplay(m_impl->display);
            m_impl->display = nullptr;
            return false;
        }
        (void)eventMask;
        m_impl->wmDelete = XInternAtom(m_impl->display, "WM_DELETE_WINDOW", False);
        m_impl->clipboardAtom = XInternAtom(m_impl->display, "CLIPBOARD", False);
        m_impl->utf8Atom = XInternAtom(m_impl->display, "UTF8_STRING", False);
        m_impl->targetsAtom = XInternAtom(m_impl->display, "TARGETS", False);
        m_impl->clipboardProperty = XInternAtom(m_impl->display, "ENERGY_ENGINE_CLIPBOARD", False);
        XSetWMProtocols(m_impl->display, m_impl->window, &m_impl->wmDelete, 1);
        XStoreName(m_impl->display, m_impl->window, config.title.c_str());
        if (config.style & WindowStyle_Fullscreen) {
            Atom wmState = XInternAtom(m_impl->display, "_NET_WM_STATE", False);
            Atom fullscreen = XInternAtom(m_impl->display, "_NET_WM_STATE_FULLSCREEN", False);
            XEvent event{};
            event.type = ClientMessage;
            event.xclient.window = m_impl->window;
            event.xclient.message_type = wmState;
            event.xclient.format = 32;
            event.xclient.data.l[0] = 1;
            event.xclient.data.l[1] = static_cast<long>(fullscreen);
            XSendEvent(m_impl->display, root, False, SubstructureNotifyMask | SubstructureRedirectMask, &event);
        }
        if (m_impl->display) {
            m_impl->xim = XOpenIM(m_impl->display, nullptr, nullptr, nullptr);
            if (m_impl->xim) {
                m_impl->xic = XCreateIC(m_impl->xim,
                    XNInputStyle, XIMPreeditNothing | XIMStatusNothing,
                    XNClientWindow, m_impl->window,
                    XNFocusWindow, m_impl->window,
                    nullptr);
                if (m_impl->xic)
                    XSetICFocus(m_impl->xic);
            }
        }
        m_impl->width = static_cast<std::uint32_t>(width);
        m_impl->height = static_cast<std::uint32_t>(height);
        m_impl->scale = 1.0f;
        m_impl->focused = true;
        XMapWindow(m_impl->display, m_impl->window);
        m_impl->clipAreaWindow = XCreateSimpleWindow(m_impl->display, m_impl->window, config.x >= 0 ? config.x : 0, config.y >= 0 ? config.y : 0, static_cast<unsigned>(m_impl->width), static_cast<unsigned>(m_impl->height), 0, 0, 0);
        m_impl->clipCenterWindow = XCreateSimpleWindow(m_impl->display, m_impl->window, static_cast<int>(m_impl->width) * .5, static_cast<int>(m_impl->height) * .5, 1, 1, 0, 0, 0);
        XMapWindow(m_impl->display, m_impl->clipCenterWindow);
        XMapWindow(m_impl->display, m_impl->clipAreaWindow);
        XFlush(m_impl->display);
        return true;
    }
    void Window::Destroy() {
        if (!m_impl)
            return;
        if (m_impl->display) {
            if (m_impl->cursor != None)
                XFreeCursor(m_impl->display, m_impl->cursor);
            if (m_impl->xic)
                XDestroyIC(m_impl->xic);
            if (m_impl->xim)
                XCloseIM(m_impl->xim);
            if (m_impl->window)
                XDestroyWindow(m_impl->display, m_impl->window);
            XCloseDisplay(m_impl->display);
        }
        m_impl->display = nullptr;
        m_impl->window = 0;
        m_impl->xic = nullptr;
        m_impl->xim = nullptr;
        m_impl->cursor = None;
    }
    void Window::PollEvents() {
        if (!m_impl->display)
            return;
        while (XPending(m_impl->display) > 0) {
            XEvent event{};
            XNextEvent(m_impl->display, &event);
            switch (event.type)
            {
            case ClientMessage:
                if (static_cast<Atom>(event.xclient.data.l[0]) == m_impl->wmDelete)
                    m_impl->HandleCloseRequest();
                break;
            case ConfigureNotify:
                m_impl->width = static_cast<std::uint32_t>(((std::max))(0, event.xconfigure.width));
                m_impl->height = static_cast<std::uint32_t>((std::max)(0, event.xconfigure.height));
                m_impl->minimized = m_impl->width == 0 || m_impl->height == 0;
                m_impl->PushEvent({ WindowEventType::Resized });
                break;
            case FocusIn:
                m_impl->focused = true;
                if (m_impl->xic) XSetICFocus(m_impl->xic);
                m_impl->PushEvent(WindowEvent{ WindowEventType::FocusChanged, KEY_INVALID, KEYMOD_NONE, false, true });
                break;
            case FocusOut:
                m_impl->focused = false;
                if (m_impl->xic) XUnsetICFocus(m_impl->xic);
                m_impl->PushEvent(WindowEvent{ WindowEventType::FocusChanged, KEY_INVALID, KEYMOD_NONE, false, false });
                break;

            case MotionNotify:
                m_impl->PushEvent(WindowEvent{ WindowEventType::MouseMove, KEY_INVALID, KEYMOD_NONE, false, false, -1, static_cast<float>(event.xmotion.x), static_cast<float>(event.xmotion.y) });
                break;
            case LeaveNotify:
                m_impl->PushEvent({ WindowEventType::MouseLeft });
                break;
            case ButtonPress:
            case ButtonRelease: {
                if (event.xbutton.button == Button4 || event.xbutton.button == Button5 || event.xbutton.button == 6 || event.xbutton.button == 7) {
                    const float amount = (event.xbutton.button == Button4 || event.xbutton.button == 6) ? 1.0f : -1.0f;
                    m_impl->PushEvent(WindowEvent{ WindowEventType::MouseWheel, KEY_INVALID,
                                                         static_cast<int>(((event.xbutton.state & ShiftMask) ? KEYMOD_SHIFT : 0) | ((event.xbutton.state & ControlMask) ? KEYMOD_CTRL : 0) | ((event.xbutton.state & Mod1Mask) ? KEYMOD_ALT : 0)),
                                                         false, false, -1, (event.xbutton.button == 6 || event.xbutton.button == 7) ? amount : 0.0f, (event.xbutton.button == Button4 || event.xbutton.button == Button5) ? amount : 0.0f });
                    break;
                }
                int button = -1;
                switch (event.xbutton.button)
                {
                case Button1: button = 0; break;
                case Button3: button = 1; break;
                case Button2: button = 2; break;
                case 8: button = 3; break;
                case 9: button = 4; break;
                default: break;
                }
                if (button >= 0) {
                    const int mods = static_cast<int>(((event.xbutton.state & ShiftMask) ? KEYMOD_SHIFT : 0) | ((event.xbutton.state & ControlMask) ? KEYMOD_CTRL : 0) | ((event.xbutton.state & Mod1Mask) ? KEYMOD_ALT : 0) | ((event.xbutton.state & Mod4Mask) ? KEYMOD_SUPER : 0));
                    m_impl->PushEvent(WindowEvent{ WindowEventType::MouseButton, KEY_INVALID, mods, event.type == ButtonPress, false, button });
                }
                break;
            }
            case KeyPress:
            case KeyRelease: {
                const bool pressed = event.type == KeyPress;
                KeySym sym = XLookupKeysym(&event.xkey, 0);
                const KeyCode key = KeysymToEngine(sym);
                if (key != KeyCode::Unknown)
                    m_impl->PushEvent(WindowEvent{ WindowEventType::Key, key, X11Modifiers(event.xkey), pressed });
                if (pressed && m_impl->xic)
                {
                    char text[128]{};
                    KeySym composed = NoSymbol;
                    Status status = 0;
                    const int count = Xutf8LookupString(m_impl->xic, &event.xkey, text, sizeof(text) - 1,
                        &composed, &status);
                    if (count > 0 && (status == XLookupChars || status == XLookupBoth)) {
                        text[count] = '\0';
                        m_impl->PushEvent(WindowEvent{ WindowEventType::Text, KeyCode::Unknown, X11Modifiers(event.xkey), false, false, -1, 0.0f, 0.0f, text });
                    }
                }
                break;
            }
            case SelectionRequest: {
                const XSelectionRequestEvent& request = event.xselectionrequest;
                XEvent response{};
                response.xselection.type = SelectionNotify;
                response.xselection.display = request.display;
                response.xselection.requestor = request.requestor;
                response.xselection.selection = request.selection;
                response.xselection.target = request.target;
                response.xselection.property = request.property;
                response.xselection.time = request.time;
                if (request.target == m_impl->targetsAtom) {
                    Atom targets[] = { m_impl->targetsAtom, m_impl->utf8Atom, XA_STRING };
                    XChangeProperty(m_impl->display, request.requestor, request.property, XA_ATOM, 32, PropModeReplace, reinterpret_cast<unsigned char*>(targets), 3);
                }
                else if (request.target == m_impl->utf8Atom || request.target == XA_STRING)
                    XChangeProperty(m_impl->display, request.requestor, request.property, request.target, 8, PropModeReplace, reinterpret_cast<const unsigned char*>(m_impl->clipboard.data()), static_cast<int>(m_impl->clipboard.size()));
                else
                    response.xselection.property = None;
                XSendEvent(m_impl->display, request.requestor, False, 0, &response);
                XFlush(m_impl->display);
                break;
            }
            case SelectionNotify:
            case SelectionClear:
            default:
                break;
            }
        }
    }
    void Window::WaitEvents() {
        if (!m_impl->display)
            return;
        XEvent event{};
        XNextEvent(m_impl->display, &event);
        XPutBackEvent(m_impl->display, &event);
    }
    void Window::RequestClose() { m_impl->shouldClose = true; }
    bool Window::ShouldClose() const { return m_impl->shouldClose; }
    bool Window::IsMinimised() const { return m_impl->minimized; }
    bool Window::IsFocused() const { return m_impl->focused; }
    std::uint32_t Window::Width() const { return m_impl->width; }
    std::uint32_t Window::Height() const { return m_impl->height; }
    std::uint32_t Window::FramebufferWidth() const { return m_impl->width; }
    std::uint32_t Window::FramebufferHeight() const { return m_impl->height; }
    float Window::ContentScale() const { return m_impl->scale; }
    const std::vector<WindowEvent>& Window::Events() const { return m_impl->events; }
    void Window::ClearEvents() { m_impl->events.clear(); }
    void Window::SetInputSink(CInput* input) { m_impl->input = input; }
    void Window::SetCursor(MouseCursorShape shape) {
        if (!m_impl->display || !m_impl->window)
            return;
        if (m_impl->cursor != None)
            XFreeCursor(m_impl->display, m_impl->cursor);
        m_impl->cursor = XCursorFor(m_impl->display, shape);
        XDefineCursor(m_impl->display, m_impl->window, m_impl->cursor);
        XFlush(m_impl->display);
    }
    void Window::SetCursorVisible(bool visible) { (void)visible; }
    void Window::SetClipboardText(const char* text) {
        m_impl->clipboard = text ? text : "";
        XSetSelectionOwner(m_impl->display, m_impl->clipboardAtom, m_impl->window, CurrentTime);
        XFlush(m_impl->display);
    }
    const char* Window::GetClipboardText() {
        if (!m_impl->display || !m_impl->window)
            return "";
        if (XGetSelectionOwner(m_impl->display, m_impl->clipboardAtom) == m_impl->window)
            return m_impl->clipboard.c_str();
        XConvertSelection(m_impl->display, m_impl->clipboardAtom, m_impl->utf8Atom,
            m_impl->clipboardProperty, m_impl->window, CurrentTime);
        XFlush(m_impl->display);
        auto startTicks = std::chrono::steady_clock::now();
        for (;;) {
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - startTicks).count() > 250)
                break;
            if (XPending(m_impl->display) == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                continue;
            }
            XEvent event{};
            XNextEvent(m_impl->display, &event);
            if (event.type == SelectionNotify && event.xselection.selection == m_impl->clipboardAtom) {
                if (event.xselection.property == None)
                    break;
                Atom type = None;
                int format = 0;
                unsigned long itemCount = 0;
                unsigned long bytesAfter = 0;
                unsigned char* data = nullptr;
                if (XGetWindowProperty(m_impl->display, m_impl->window, m_impl->clipboardProperty,
                    0, 1 << 20, False, AnyPropertyType,
                    &type, &format, &itemCount, &bytesAfter, &data) == Success && data) {
                    m_impl->clipboard.assign(reinterpret_cast<char*>(data), reinterpret_cast<char*>(data) + itemCount * (format / 8));
                    XFree(data);
                }
                XDeleteProperty(m_impl->display, m_impl->window, m_impl->clipboardProperty);
                break;
            }
            else if (event.type == SelectionRequest) {
                XPutBackEvent(m_impl->display, &event);
                PollEvents();
            }
            else {
                XPutBackEvent(m_impl->display, &event);
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        }
        return m_impl->clipboard.c_str();
    }
    std::vector<const char*> Window::GetRequiredVulkanInstanceExtensions() const { return { VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_XLIB_SURFACE_EXTENSION_NAME }; }
    VkSurfaceKHR Window::CreateVulkanSurface(VkInstance instance) const {
        VkXlibSurfaceCreateInfoKHR createInfo{ VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR };
        createInfo.dpy = m_impl->display;
        createInfo.window = m_impl->window;
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        if (vkCreateXlibSurfaceKHR(instance, &createInfo, nullptr, &surface) != VK_SUCCESS)
            return VK_NULL_HANDLE;
        return surface;
    }
    MessageBoxButtons ShowMessageBox(std::string title, std::string subtitle, MessageBoxType type, const std::vector<MessageBoxButtons>& buttons) {
        if (buttons.empty()) return Ok;
        std::stringstream ss;
        ss << "xmessage -center -title \"" << title << "\" ";
        ss << "-buttons ";
        for (size_t i = 0; i < buttons.size(); ++i) {
            std::string label = "";
            switch (buttons[i])
            {
            case Ok:        label = "OK";       break;
            case Cancel:    label = "Cancel";   break;
            case Abort:     label = "Abort";    break;
            case Retry:     label = "Retry";    break;
            case Ignore:    label = "Ignore";   break;
            case Yes:       label = "Yes";      break;
            case No:        label = "No";       break;
            case Continue:  label = "Continue"; break;
            case TryAgain:  label = "Try Again"; break;
            default:        label = "OK";       break;
            }
            ss << label << ":" << (100 + i);
            if (i < buttons.size() - 1) ss << ",";
        }
        ss << " \"" << subtitle << "\"";
        int exitCode = WEXITSTATUS(std::system(ss.c_str()));
        if (exitCode >= 100 && exitCode < (100 + static_cast<int>(buttons.size()))) {
            return buttons[exitCode - 100];
        }
        return buttons[0];
    }
    bool Engine::DoesSystemUseDarkMode() {
        gboolean prefer_dark = FALSE;
        GtkSettings* settings = gtk_settings_get_default();
        if (settings)
            g_object_get(settings, "gtk-application-prefer-dark-theme", &prefer_dark, NULL);
        return prefer_dark == TRUE;
    }
    void Window::UpdateMouseState(MouseState state, Engine::CInput::MouseBounds* bounds) {
        Engine::CInput::MouseBounds betterBounds{ 0, 0, 0, 0 };
        if (bounds != nullptr)
            betterBounds = *bounds;
        bool stateChanged = m_impl->currentMouseState != state;
        bool boundsChanged = m_impl->currentBounds.left != betterBounds.left || m_impl->currentBounds.top != betterBounds.top || m_impl->currentBounds.right != betterBounds.right || m_impl->currentBounds.bottom != betterBounds.bottom;
        bool sizeChanged = m_impl->lastCachedWidth != m_impl->width || m_impl->lastCachedHeight != m_impl->height;
        if (!stateChanged && !boundsChanged && !sizeChanged)
            return;
        m_impl->currentMouseState = state;
        m_impl->currentBounds = betterBounds;
        m_impl->lastCachedWidth = m_impl->width;
        m_impl->lastCachedHeight = m_impl->height;
        switch (state)
        {
        case LockedToCenter: {
            int cx = static_cast<int>(m_impl->width) / 2;
            int cy = static_cast<int>(m_impl->height) / 2;
            XWarpPointer(m_impl->display, None, m_impl->window, 0, 0, 0, 0, cx, cy);
            XGrabPointer(m_impl->display, m_impl->window, True, PointerMotionMask | ButtonPressMask | ButtonReleaseMask, GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
            XFlush(m_impl->display);
            return;
        }
        case LockedToArea: {
            int x = betterBounds.left;
            int y = betterBounds.top;
            int w = betterBounds.right - betterBounds.left;
            int h = betterBounds.bottom - betterBounds.top;
            if (w <= 0) w = 1;
            if (h <= 0) h = 1;

            XMoveResizeWindow(m_impl->display, m_impl->clipAreaWindow, x, y, static_cast<unsigned>(w), static_cast<unsigned>(h));
            XGrabPointer(m_impl->display, m_impl->window, True,
                PointerMotionMask | ButtonPressMask | ButtonReleaseMask,
                GrabModeAsync, GrabModeAsync, m_impl->clipAreaWindow, None, CurrentTime);
            XFlush(m_impl->display);
            return;
        }
        case Free: {
            XUngrabPointer(m_impl->display, CurrentTime);
            XFlush(m_impl->display);
            return;
        }
        }
    }
}
#endif
