//============== ENERGY ENGINE =================//
//                                              //
// Purpose: Handle OS stuff on MacOS            //
//                                              //
//==============================================//

#if defined(__APPLE__)
#define VK_USE_PLATFORM_METAL_EXT
#include "PlatformWindow.h"
#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>
#include <algorithm>
#include <string>
#include <utility>
@class EnergyEngineView;
@class EnergyEngineWindowDelegate;

namespace Engine
{
struct Window::Impl
{
    NSWindow* window = nil;
    EnergyEngineView* view = nil;
    EnergyEngineWindowDelegate* delegate = nil;
    CAMetalLayer* metalLayer = nil;
    std::string clipboard;
    std::vector<WindowEvent> events;
    bool shouldClose = false;
    bool minimized = false;
    bool focused = true;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    float scale = 1.0f;
    MouseState currentMouseState = MouseState::Free;
    Engine::Input()::MouseBounds currentBounds = { 0, 0, 0, 0 };
    std::uint32_t lastCachedWidth = 0;
    std::uint32_t lastCachedHeight = 0;
    std::function<bool()> closeRequestHandler;
    void HandleCloseRequest() {
        events.push_back({ WindowEventType::CloseRequested });
        if (!closeRequestHandler || closeRequestHandler())
            shouldClose = true;
    }
};
}

namespace
{
    int MacModifiers(NSEventModifierFlags flags)
    {
        int mods = KEYMOD_NONE;
        if (flags & NSEventModifierFlagShift) mods |= KEYMOD_SHIFT;
        if (flags & NSEventModifierFlagControl) mods |= KEYMOD_CTRL;
        if (flags & NSEventModifierFlagOption) mods |= KEYMOD_ALT;
        if (flags & NSEventModifierFlagCommand) mods |= KEYMOD_SUPER;
        return mods;
    }
    KEYCODE MacKeyCode(unsigned short keyCode) {
    switch (keyCode)
    {
        case 0:   return KEY_A;
        case 1:   return KEY_S;
        case 2:   return KEY_D;
        case 3:   return KEY_F;
        case 4:   return KEY_H;
        case 5:   return KEY_G;
        case 6:   return KEY_Z;
        case 7:   return KEY_X;
        case 8:   return KEY_C;
        case 9:   return KEY_V;
        case 11:  return KEY_B;
        case 12:  return KEY_Q;
        case 13:  return KEY_W;
        case 14:  return KEY_E;
        case 15:  return KEY_R;
        case 16:  return KEY_Y;
        case 17:  return KEY_T;
        case 18:  return KEY_1;
        case 19:  return KEY_2;
        case 20:  return KEY_3;
        case 21:  return KEY_4;
        case 22:  return KEY_6;
        case 23:  return KEY_5;
        case 24:  return KEY_EQUAL;
        case 25:  return KEY_9;
        case 26:  return KEY_7;
        case 27:  return KEY_MINUS;
        case 28:  return KEY_8;
        case 29:  return KEY_0;
        case 30:  return KEY_RBRACKET;
        case 31:  return KEY_O;
        case 32:  return KEY_U;
        case 33:  return KEY_LBRACKET;
        case 34:  return KEY_I;
        case 35:  return KEY_P;
        case 36:  return KEY_ENTER;
        case 37:  return KEY_L;
        case 38:  return KEY_J;
        case 39:  return KEY_APOSTROPHE;
        case 40:  return KEY_K;
        case 41:  return KEY_SEMICOLON;
        case 42:  return KEY_BACKSLASH;
        case 43:  return KEY_COMMA;
        case 44:  return KEY_SLASH;
        case 45:  return KEY_N;
        case 46:  return KEY_M;
        case 47:  return KEY_PERIOD;
        case 48:  return KEY_TAB;
        case 49:  return KEY_SPACE;
        case 50:  return KEY_BACKQUOTE;
        case 51:  return KEY_BACKSPACE;
        case 53:  return KEY_ESCAPE;
        case 55:  return KEY_LWIN;
        case 56:  return KEY_LSHIFT;
        case 57:  return KEY_CAPSLOCK;
        case 58:  return KEY_LALT;
        case 59:  return KEY_LCONTROL;
        case 60:  return KEY_RSHIFT;
        case 61:  return KEY_RALT;
        case 62:  return KEY_RCONTROL;
        case 63:  return KEY_RWIN;
        case 64:  return KEY_F17;
        case 65:  return KEY_PAD_DECIMAL;
        case 67:  return KEY_PAD_MULTIPLY;
        case 69:  return KEY_PAD_PLUS;
        case 71:  return KEY_NUMLOCK;
        case 72:  return KEY_PAD_CLEAR;
        case 75:  return KEY_PAD_DIVIDE;
        case 76:  return KEY_PAD_ENTER;
        case 78:  return KEY_PAD_MINUS;
        case 81:  return KEY_PAD_EQUAL;
        case 82:  return KEY_PAD_0;
        case 83:  return KEY_PAD_1;
        case 84:  return KEY_PAD_2;
        case 85:  return KEY_PAD_3;
        case 86:  return KEY_PAD_4;
        case 87:  return KEY_PAD_5;
        case 88:  return KEY_PAD_6;
        case 89:  return KEY_PAD_7;
        case 91:  return KEY_PAD_8;
        case 92:  return KEY_PAD_9;
        case 96:  return KEY_F5;
        case 97:  return KEY_F6;
        case 98:  return KEY_F7;
        case 99:  return KEY_F3;
        case 100: return KEY_F8;
        case 101: return KEY_F9;
        case 103: return KEY_F11;
        case 105: return KEY_F13;
        case 106: return KEY_F16;
        case 107: return KEY_F14;
        case 109: return KEY_F10;
        case 111: return KEY_F12;
        case 113: return KEY_F15;
        case 114: return KEY_INSERT;
        case 115: return KEY_HOME;
        case 116: return KEY_PAGEUP;
        case 117: return KEY_DELETE;
        case 118: return KEY_F4;
        case 119: return KEY_END;
        case 120: return KEY_F2;
        case 121: return KEY_PAGEDOWN;
        case 122: return KEY_F1;
        case 123: return KEY_LEFT;
        case 124: return KEY_RIGHT;
        case 125: return KEY_DOWN;
        case 126: return KEY_UP;
        default:  return KEY_INVALID;
    }
}
}
}
@interface EnergyEngineView : NSView
@end
@implementation EnergyEngineView
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)becomeFirstResponder { return YES; }
@end
@interface EnergyEngineWindowDelegate : NSObject<NSWindowDelegate>
@property(nonatomic, assign) Engine::Window::Impl* impl;
@end
@implementation EnergyEngineWindowDelegate
- (BOOL)windowShouldClose:(NSWindow*)sender {
    (void)sender;
    if (_impl)
        _impl->HandleCloseRequest();
    return NO;
}
- (void)windowDidResize:(NSNotification*)notification {
    NSWindow* window = notification.object;
    if (!_impl || !window)
        return;
    NSRect backing = [window convertRectToBacking:window.contentView.bounds];
    _impl->width = static_cast<std::uint32_t>(std::max<CGFloat>(0.0, backing.size.width));
    _impl->height = static_cast<std::uint32_t>(std::max<CGFloat>(0.0, backing.size.height));
    _impl->scale = static_cast<float>(window.backingScaleFactor);
    _impl->minimized = window.isMiniaturized || _impl->width == 0 || _impl->height == 0;
    if (!_impl->minimized)
        _impl->events.push_back({Engine::WindowEventType::Resized});
}
- (void)windowDidMiniaturize:(NSNotification*)notification {
    (void)notification;
    if (_impl) _impl->minimized = true;
}
- (void)windowDidDeminiaturize:(NSNotification*)notification {
    (void)notification;
    if (_impl) _impl->minimized = false;
}
- (void)windowDidBecomeKey:(NSNotification*)notification {
    (void)notification;
    if (_impl) {
        _impl->focused = true;
        _impl->events.push_back(Engine::WindowEvent{Engine::WindowEventType::FocusChanged, KEY_INVALID, KEYMOD_NONE, false, true});
    }
}
- (void)windowDidResignKey:(NSNotification*)notification {
    (void)notification;
    if (_impl) {
        _impl->focused = false;
        _impl->events.push_back(Engine::WindowEvent{Engine::WindowEventType::FocusChanged, KEY_INVALID, KEYMOD_NONE, false, false});
    }
}
@end
namespace Engine
{
Window::Window() : m_impl(std::make_unique<Impl>()) {}
Window::~Window() { Destroy(); }
bool Window::Create(const WindowConfig& config) {
    @autoreleasepool
    {
        NSApplication* app = [NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        [app finishLaunching];
        const CGFloat width = config.width > 0 ? static_cast<CGFloat>(config.width) : 1280.0;
        const CGFloat height = config.height > 0 ? static_cast<CGFloat>(config.height) : 720.0;
        const CGFloat x = config.x >= 0 ? static_cast<CGFloat>(config.x) : 100.0;
        const CGFloat y = config.y >= 0 ? static_cast<CGFloat>(config.y) : 100.0;
        const NSRect rect = NSMakeRect(x, y, width, height);
        NSWindowStyleMask style = 0;
        if (config.style & WindowStyle_Border) style |= NSWindowStyleMaskTitled;
        if (config.style & WindowStyle_Close) style |= NSWindowStyleMaskClosable;
        if (config.style & WindowStyle_Minimize) style |= NSWindowStyleMaskMiniaturizable;
        if (config.style & WindowStyle_Resizable) style |= NSWindowStyleMaskResizable;
        if (style == 0) style = NSWindowStyleMaskBorderless;
        m_impl->window = [[NSWindow alloc] initWithContentRect:rect styleMask:style backing:NSBackingStoreBuffered defer:NO];
        if (!m_impl->window)
            return false;
        m_impl->window.releasedWhenClosed = NO;
        m_impl->window.title = [NSString stringWithUTF8String:config.title.c_str()];
        m_impl->window.acceptsMouseMovedEvents = YES;
        m_impl->view = [[EnergyEngineView alloc] initWithFrame:m_impl->window.contentView.bounds];
        m_impl->view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        m_impl->view.wantsLayer = YES;
        m_impl->metalLayer = [CAMetalLayer layer];
        m_impl->metalLayer.contentsScale = m_impl->window.backingScaleFactor;
        m_impl->view.layer = m_impl->metalLayer;
        [m_impl->window setContentView:m_impl->view];
        m_impl->delegate = [[EnergyEngineWindowDelegate alloc] init];
        m_impl->delegate.impl = m_impl.get();
        m_impl->window.delegate = m_impl->delegate;
        [m_impl->window makeKeyAndOrderFront:nil];
        [app activateIgnoringOtherApps:YES];
        [m_impl->window makeFirstResponder:m_impl->view];
        const NSRect backing = [m_impl->window convertRectToBacking:m_impl->view.bounds];
        m_impl->width = static_cast<std::uint32_t>(std::max<CGFloat>(0.0, backing.size.width));
        m_impl->height = static_cast<std::uint32_t>(std::max<CGFloat>(0.0, backing.size.height));
        m_impl->scale = static_cast<float>(m_impl->window.backingScaleFactor);
        m_impl->focused = true;
        m_impl->minimized = false;
        if (config.style & WindowStyle_Fullscreen)
            [m_impl->window toggleFullScreen:nil];
        return true;
    }
}
void Window::Destroy() {
    @autoreleasepool
    {
        if (!m_impl)
            return;
        if (m_impl->window) {
            m_impl->window.delegate = nil;
            [m_impl->window orderOut:nil];
            [m_impl->window close];
        }
        m_impl->metalLayer = nil;
        m_impl->view = nil;
        m_impl->delegate = nil;
        m_impl->window = nil;
    }
}
void Window::SetCloseRequestHandler(std::function<bool()> handler) { m_impl->closeRequestHandler = std::move(handler); }
void Window::PollEvents() {
    @autoreleasepool
    {
        NSApplication* app = [NSApplication sharedApplication];
        for (;;) {
            NSEvent* event = [app nextEventMatchingMask:NSEventMaskAny untilDate:[NSDate distantPast] inMode:NSDefaultRunLoopMode dequeue:YES];
            if (!event)
                break;
            const NSEventType type = event.type;
            const int mods = MacModifiers(event.modifierFlags);
            switch (type)
            {
            case NSEventTypeKeyDown:
            case NSEventTypeKeyUp: {
                const bool pressed = type == NSEventTypeKeyDown;
                const KeyCode key = MacKeyCode(event.keyCode);
                if (key != KeyCode::Unknown)
                    m_impl->events.push_back(WindowEvent{WindowEventType::Key, key, mods, pressed});
                if (pressed && event.characters.length > 0) {
                    const std::string utf8 = event.characters.UTF8String ? event.characters.UTF8String : "";
                    if (!utf8.empty())
                        m_impl->events.push_back(WindowEvent{WindowEventType::Text, KeyCode::Unknown, mods, false, false, -1, 0.0f, 0.0f, utf8});
                }
                break;
            }
            case NSEventTypeFlagsChanged: {
                const KeyCode key = MacKeyCode(event.keyCode);
                if (key != KeyCode::Unknown) {
                    bool pressed = false;
                    switch (key)
                    {
                    case KeyCode::LeftShift: case KeyCode::RightShift: pressed = (mods & KEYMOD_SHIFT) != 0; break;
                    case KeyCode::LeftCtrl:  case KeyCode::RightCtrl:  pressed = (mods & KEYMOD_CTRL) != 0; break;
                    case KeyCode::LeftAlt:   case KeyCode::RightAlt:   pressed = (mods & KEYMOD_ALT) != 0; break;
                    case KeyCode::LeftSuper: case KeyCode::RightSuper: pressed = (mods & KEYMOD_SUPER) != 0; break;
                    default: break;
                    }
                    m_impl->events.push_back(WindowEvent{WindowEventType::Key, key, mods, pressed});
                }
                break;
            }
            case NSEventTypeMouseMoved:
            case NSEventTypeLeftMouseDragged:
            case NSEventTypeRightMouseDragged:
            case NSEventTypeOtherMouseDragged: {
                m_impl->events.push_back(WindowEvent{ WindowEventType::MouseMove, KeyCode::Unknown, mods, false, false, -1, static_cast<float>(event.deltaX), static_cast<float>(event.deltaY) });
                break;
            }
            case NSEventTypeLeftMouseDown:
            case NSEventTypeLeftMouseUp:
            case NSEventTypeRightMouseDown:
            case NSEventTypeRightMouseUp:
            case NSEventTypeOtherMouseDown:
            case NSEventTypeOtherMouseUp: {
                int button = 0;
                if (type == NSEventTypeRightMouseDown || type == NSEventTypeRightMouseUp)
                    button = 1;
                else if (type == NSEventTypeOtherMouseDown || type == NSEventTypeOtherMouseUp)
                    button = static_cast<int>(event.buttonNumber);
                const bool pressed = type == NSEventTypeLeftMouseDown || type == NSEventTypeRightMouseDown || type == NSEventTypeOtherMouseDown;
                if (button >= 0 && button <= 4)
                    m_impl->events.push_back(WindowEvent{WindowEventType::MouseButton, KeyCode::Unknown, mods, pressed, false, button});
                break;
            }
            case NSEventTypeScrollWheel:
                m_impl->events.push_back(WindowEvent{WindowEventType::MouseWheel, KeyCode::Unknown, mods, false, false, -1, static_cast<float>(event.scrollingDeltaX), static_cast<float>(event.scrollingDeltaY)});
                break;
            case NSEventTypeMouseExited:
                m_impl->events.push_back({WindowEventType::MouseLeft});
                break;
            default:
                break;
            }
            [app sendEvent:event];
        }
    }
}
void Window::WaitEvents() {
    @autoreleasepool
    {
        NSApplication* app = [NSApplication sharedApplication];
        NSEvent* event = [app nextEventMatchingMask:NSEventMaskAny untilDate:[NSDate distantFuture] inMode:NSDefaultRunLoopMode dequeue:YES];
        if (event)
            [app sendEvent:event];
    }
}
void Window::RequestClose() { m_impl->shouldClose = true; }
bool Window::ShouldClose() const { return m_impl->shouldClose; }
bool Window::IsMinimised() const { return m_impl->minimized; }
bool Window::IsFocused() const { return m_impl->focused; }
std::uint32_t Window::Width() const {
    if (!m_impl->window) return 0;
    return static_cast<std::uint32_t>(std::max<CGFloat>(0.0, m_impl->window.contentView.bounds.size.width));
}
std::uint32_t Window::Height() const {
    if (!m_impl->window) return 0;
    return static_cast<std::uint32_t>(std::max<CGFloat>(0.0, m_impl->window.contentView.bounds.size.height));
}
std::uint32_t Window::FramebufferWidth() const { return m_impl->width; }
std::uint32_t Window::FramebufferHeight() const { return m_impl->height; }
float Window::ContentScale() const { return m_impl->scale; }
const std::vector<WindowEvent>& Window::Events() const { return m_impl->events; }
void Window::ClearEvents() { m_impl->events.clear(); }
void Window::SetCursor(MouseCursorShape shape) {
    @autoreleasepool
    {
        NSCursor* cursor = [NSCursor arrowCursor];
        switch (shape)
        {
        case MouseCursorShape::Text: cursor = [NSCursor IBeamCursor]; break;
        case MouseCursorShape::Hand: cursor = [NSCursor pointingHandCursor]; break;
        case MouseCursorShape::ResizeEW: cursor = [NSCursor resizeLeftRightCursor]; break;
        case MouseCursorShape::ResizeNS: cursor = [NSCursor resizeUpDownCursor]; break;
        case MouseCursorShape::NotAllowed: cursor = [NSCursor operationNotAllowedCursor]; break;
        case MouseCursorShape::Wait: cursor = [NSCursor disappearingItem]; break;
        case MouseCursorShape::Progress: cursor = [NSCursor disappearingItem]; break;
        default: break;
        }
        [cursor set];
    }
}
void Window::SetCursorVisible(bool visible) {
    @autoreleasepool
    {
        if (visible) [NSCursor unhide];
        else [NSCursor hide];
    }
}
void Window::SetClipboardText(const char* text) {
    @autoreleasepool
    {
        m_impl->clipboard = text ? text : "";
        NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
        [pasteboard clearContents];
        NSString* string = [NSString stringWithUTF8String:m_impl->clipboard.c_str()];
        if (string)
            [pasteboard setString:string forType:NSPasteboardTypeString];
    }
}
const char* Window::GetClipboardText() {
    @autoreleasepool
    {
        NSString* string = [[NSPasteboard generalPasteboard] stringForType:NSPasteboardTypeString];
        if (string && string.UTF8String)
            m_impl->clipboard = string.UTF8String;
        return m_impl->clipboard.c_str();
    }
}
std::vector<const char*> Window::GetRequiredVulkanInstanceExtensions() const { return { VK_KHR_SURFACE_EXTENSION_NAME, VK_EXT_METAL_SURFACE_EXTENSION_NAME, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME }; }
VkSurfaceKHR Window::CreateVulkanSurface(VkInstance instance) const {
    auto createMetalSurface = reinterpret_cast<PFN_vkCreateMetalSurfaceEXT>(
        vkGetInstanceProcAddr(instance, "vkCreateMetalSurfaceEXT"));
    if (!createMetalSurface || !m_impl->metalLayer)
        return VK_NULL_HANDLE;
    VkMetalSurfaceCreateInfoEXT createInfo{VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT};
    createInfo.pLayer = m_impl->metalLayer;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (createMetalSurface(instance, &createInfo, nullptr, &surface) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    return surface;
}
MessageBoxButtons ShowMessageBox(std::string title, std::string subtitle, MessageBoxType type, const std::vector<MessageBoxButtons>& buttons) {
    if (buttons.empty()) return Ok;
    @autoreleasepool {
        NSAlert *alert = [[NSAlert alloc] init];
        [alert setMessageText:[NSString stringWithUTF8String:title.c_str()]];
        [alert setInformativeText:[NSString stringWithUTF8String:subtitle.c_str()]];
        switch (type)
        {
            case Warning:  [alert setAlertStyle:NSAlertStyleWarning]; break;
            case Info:     [alert setAlertStyle:NSAlertStyleInformational]; break;
            case Question: [alert setAlertStyle:NSAlertStyleInformational]; break;
            case Error:    [alert setAlertStyle:NSAlertStyleCritical]; break;
        }
        for (const auto& btn : buttons) {
            NSString* label = @"";
            switch(btn)
            {
            case Ok:        label = @"OK";       break;
            case Cancel:    label = @"Cancel";   break;
            case Abort:     label = @"Abort";    break;
            case Retry:     label = @"Retry";    break;
            case Ignore:    label = @"Ignore";   break;
            case Yes:       label = @"Yes";      break;
            case No:        label = @"No";       break;
            case Continue:  label = @"Continue"; break;
            case TryAgain:  label = @"Try Again";break;
            default:        label = @"OK";       break;
            }
            [alert addButtonWithTitle:label];
        }
        NSInteger response = [alert runModal];
        int index = response - NSAlertFirstButtonReturn;
        if (index >= 0 && index < static_cast<int>(buttons.size())) { return buttons[index]; }
        return buttons[0];
    }
}
bool DoesSystemUseDarkMode() {
    NSAppearance* appearance = [NSApp appearance];
    if (appearance == nil)
        appearance = [NSAppearance currentAppearance];
    return [appearance.name containsString:NSAppearanceNameDarkAqua];
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
    dispatch_async(dispatch_get_main_queue(), ^{
        switch (state)
        {
        case LockedToCenter: {
            NSRect frame = [m_impl->window contentRectForFrameRect:[m_impl->window frame]];
            NSPoint center = NSMakePoint(frame.size.width / 2, frame.size.height / 2);
            NSPoint screenPoint = [m_impl->window convertPointToScreen:center];
            CGPoint cgScreenPoint = CGPointMake(screenPoint.x, CGDisplayBounds(CGMainDisplayID()).size.height - screenPoint.y);
            CGWarpMouseCursorPosition(cgScreenPoint);
            CGAssociateMouseAndMouseCursorPosition(false);
            return;
        }
        case LockedToArea: {
            CGAssociateMouseAndMouseCursorPosition(true);
            return;
        }
        case Free: {
            CGAssociateMouseAndMouseCursorPosition(true);
            return;
        }
        }
    });
}
}
#endif