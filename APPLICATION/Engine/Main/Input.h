//============== ENERGY ENGINE =================//
//                                              //
// Purpose: Declare input stuff                 //
//                                              //
//==============================================//

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "FileParsers/inicpp.h"

enum KEYCODE : int
{
    KEY_INVALID = -1,
    KEY_NONE = 0,
    KEY_0,
    KEY_1,
    KEY_2,
    KEY_3,
    KEY_4,
    KEY_5,
    KEY_6,
    KEY_7,
    KEY_8,
    KEY_9,
    KEY_A,
    KEY_B,
    KEY_C,
    KEY_D,
    KEY_E,
    KEY_F,
    KEY_G,
    KEY_H,
    KEY_I,
    KEY_J,
    KEY_K,
    KEY_L,
    KEY_M,
    KEY_N,
    KEY_O,
    KEY_P,
    KEY_Q,
    KEY_R,
    KEY_S,
    KEY_T,
    KEY_U,
    KEY_V,
    KEY_W,
    KEY_X,
    KEY_Y,
    KEY_Z,
    KEY_PAD_0,
    KEY_PAD_1,
    KEY_PAD_2,
    KEY_PAD_3,
    KEY_PAD_4,
    KEY_PAD_5,
    KEY_PAD_6,
    KEY_PAD_7,
    KEY_PAD_8,
    KEY_PAD_9,
    KEY_PAD_DIVIDE,
    KEY_PAD_MULTIPLY,
    KEY_PAD_MINUS,
    KEY_PAD_PLUS,
    KEY_PAD_ENTER,
    KEY_PAD_DECIMAL,
    KEY_PAD_COMMA,
    KEY_PAD_EQUAL,
    KEY_PAD_CLEAR,
    KEY_LBRACKET,
    KEY_RBRACKET,
    KEY_SEMICOLON,
    KEY_APOSTROPHE,
    KEY_BACKQUOTE,
    KEY_COMMA,
    KEY_PERIOD,
    KEY_SLASH,
    KEY_BACKSLASH,
    KEY_MINUS,
    KEY_EQUAL,
    KEY_ENTER,
    KEY_SPACE,
    KEY_BACKSPACE,
    KEY_TAB,
    KEY_CAPSLOCK,
    KEY_NUMLOCK,
    KEY_ESCAPE,
    KEY_SCROLLLOCK,
    KEY_INSERT,
    KEY_DELETE,
    KEY_HOME,
    KEY_END,
    KEY_PAGEUP,
    KEY_PAGEDOWN,
    KEY_BREAK,
    KEY_PRINTSCREEN,
    KEY_LSHIFT,
    KEY_RSHIFT,
    KEY_LALT,
    KEY_RALT,
    KEY_LCONTROL,
    KEY_RCONTROL,
    KEY_LWIN,
    KEY_RWIN,
    KEY_APP,
    KEY_UP,
    KEY_LEFT,
    KEY_DOWN,
    KEY_RIGHT,
    KEY_APPBACK,
    KEY_APPFORWARD,
    KEY_NONUSBACKSLASH,
    KEY_F1,
    KEY_F2,
    KEY_F3,
    KEY_F4,
    KEY_F5,
    KEY_F6,
    KEY_F7,
    KEY_F8,
    KEY_F9,
    KEY_F10,
    KEY_F11,
    KEY_F12,
    KEY_F13,
    KEY_F14,
    KEY_F15,
    KEY_F16,
    KEY_F17,
    KEY_F18,
    KEY_F19,
    KEY_F20,
    KEY_F21,
    KEY_F22,
    KEY_F23,
    KEY_F24,
    KEY_CAPSLOCKTOGGLE,
    KEY_NUMLOCKTOGGLE,
    KEY_SCROLLLOCKTOGGLE,
    _almostlastkey,
    _lastkey = _almostlastkey - 1,
    _keysamount = _lastkey - KEY_NONE + 1,
    _firstmouse = _lastkey + 1,
    MOUSE_LEFT = _firstmouse,
    MOUSE_RIGHT,
    MOUSE_MIDDLE,
    MOUSE_4,
    MOUSE_5,
    MOUSE_X1,
    MOUSE_X2,
    MOUSE_WHEEL_X,
    MOUSE_WHEEL_Y,
    MOUSE_WHEEL_UP,
    MOUSE_WHEEL_DOWN,
    _almostlastmouse,
    _lastmouse = _almostlastmouse - 1,
    _mouseamount = _lastmouse - _firstmouse + 1,
    _firstgamepad = _lastmouse + 1,
    //                      // XBOX        | SWITCH  | PLAYSTA. | -> ACTION
    GAMEPAD_START = _firstgamepad,// Menu  | +       | Options  |
    GAMEPAD_BACK,           // View        | -       | Share    |
    GAMEPAD_FACELEFT,       // X           | Y       | Square   | Toggle Menu. Hold for Windowing mode (Focus/Move/Resize windows)
    GAMEPAD_FACERIGHT,      // B           | A       | Circle   | Cancel / Close / Exit
    GAMEPAD_FACEUP,         // Y           | X       | Triangle | Open Context Menu
    GAMEPAD_FACEDOWN,       // A           | B       | Cross    | Activate / Open / Toggle. Hold for 0.60f to Activate in Text Input mode (e.g. wired to an on-screen keyboard).
    GAMEPAD_DPADLEFT,       // D-pad Left  | "       | "        | Move / Tweak / Resize Window (in Windowing mode)
    GAMEPAD_DPADRIGHT,      // D-pad Right | "       | "        | Move / Tweak / Resize Window (in Windowing mode)
    GAMEPAD_DPADUP,         // D-pad Up    | "       | "        | Move / Tweak / Resize Window (in Windowing mode)
    GAMEPAD_DPADDOWN,       // D-pad Down  | "       | "        | Move / Tweak / Resize Window (in Windowing mode)
    GAMEPAD_L1,             // L Bumper    | L       | L1       | Tweak Slower / Focus Previous (in Windowing mode)
    GAMEPAD_R1,             // R Bumper    | R       | R1       | Tweak Faster / Focus Next (in Windowing mode)
    GAMEPAD_L2,             // L Trigger   | ZL      | L2       | [Analog]
    GAMEPAD_R2,             // R Trigger   | ZR      | R2       | [Analog]
    GAMEPAD_L3,             // L Stick     | L3      | L3       |
    GAMEPAD_R3,             // R Stick     | R3      | R3       |
    GAMEPAD_LSTICKLEFT,     //             |         |          | [Analog] Move Window (in Windowing mode)
    GAMEPAD_LSTICKRIGHT,    //             |         |          | [Analog] Move Window (in Windowing mode)
    GAMEPAD_LSTICKUP,       //             |         |          | [Analog] Move Window (in Windowing mode)
    GAMEPAD_LSTICKDOWN,     //             |         |          | [Analog] Move Window (in Windowing mode)
    GAMEPAD_RSTICKLEFT,     //             |         |          | [Analog]
    GAMEPAD_RSTICKRIGHT,    //             |         |          | [Analog]
    GAMEPAD_RSTICKUP,       //             |         |          | [Analog]
    GAMEPAD_RSTICKDOWN,     //             |         |          | [Analog]
    _almostlastgamepad,
    _lastgamepad = _almostlastgamepad - 1,
    _gamepadamount = _lastgamepad - _firstgamepad + 1,
    _allknownkeysamount = _keysamount + _mouseamount + _gamepadamount + 4,
    KEYMOD_NONE = 0,
    KEYMOD_CTRL = 1 << 12,
    KEYMOD_SHIFT = 1 << 13,
    KEYMOD_ALT = 1 << 14,
    KEYMOD_SUPER = 1 << 15,
    KEYMOD_MASK = 0xF000,
};
namespace Engine
{
    class CInput
    {
    public:
        struct Action
        {
            std::string Name;
            bool IsActive = false;    // AT LEAST ONE BINDING IS DOWN AND IsInputAllowed() == TRUE
            bool IsRawActive = false; // AT LEAST ONE BINDING IS DOWN REGARDLESS OF IsInputAllowed()
            std::unordered_set<KEYCODE> Bindings;
        };
        enum InputMessageType
        {
            ButtonBegan,
            ButtonEnded,
            ActionBegan,
            ActionEnded,
            MouseMoved,
            MouseWheelChanged,
            TextInputReceived,
            FocusChangedEvent,
        };
        struct InputMessage
        {
            InputMessageType Type = ButtonEnded;
            KEYCODE Key = KEY_INVALID;
            std::string Action;
            float MouseDX = 0.0f;
            float MouseDY = 0.0f;
            float WheelX = 0.0f;
            float WheelY = 0.0f;
            std::string Text;
            int Modifiers = 0;
            bool Pressed = false;
        };
        using MessageCallback = std::function<void(const InputMessage&, bool isInputAllowed)>;
        using ListenerId = std::uint64_t;
        struct KeyNameMapping
        {
            std::string shortName;
            std::string fullName;
            KEYCODE keyCode;
        };
        struct MouseBounds {
            int left;
            int top;
            int right;
            int bottom;
        };
        // SHORT NAME | FULL NAME | BUTTON CODE
        std::vector<KeyNameMapping> keynames = {
            {"invalid", "invalid", KEY_INVALID},
            {"none", "none", KEY_NONE},
            {"0", "zero", KEY_0},
            {"1", "one", KEY_1},
            {"2", "two", KEY_2},
            {"3", "three", KEY_3},
            {"4", "four", KEY_4},
            {"5", "five", KEY_5},
            {"6", "six", KEY_6},
            {"7", "seven", KEY_7},
            {"8", "eight", KEY_8},
            {"9", "nine", KEY_9},
            {"a", "a", KEY_A},
            {"b", "b", KEY_B},
            {"c", "c", KEY_C},
            {"d", "d", KEY_D},
            {"e", "e", KEY_E},
            {"f", "f", KEY_F},
            {"g", "g", KEY_G},
            {"h", "h", KEY_H},
            {"i", "i", KEY_I},
            {"j", "j", KEY_J},
            {"k", "k", KEY_K},
            {"l", "l", KEY_L},
            {"m", "m", KEY_M},
            {"n", "n", KEY_N},
            {"o", "o", KEY_O},
            {"p", "p", KEY_P},
            {"q", "q", KEY_Q},
            {"r", "r", KEY_R},
            {"s", "s", KEY_S},
            {"t", "t", KEY_T},
            {"u", "u", KEY_U},
            {"v", "v", KEY_V},
            {"w", "w", KEY_W},
            {"x", "x", KEY_X},
            {"y", "y", KEY_Y},
            {"z", "z", KEY_Z},
            {"num0", "numpad0", KEY_PAD_0},
            {"num1", "numpad1", KEY_PAD_1},
            {"num2", "numpad2", KEY_PAD_2},
            {"num3", "numpad3", KEY_PAD_3},
            {"num4", "numpad4", KEY_PAD_4},
            {"num5", "numpad5", KEY_PAD_5},
            {"num6", "numpad6", KEY_PAD_6},
            {"num7", "numpad7", KEY_PAD_7},
            {"num8", "numpad8", KEY_PAD_8},
            {"num9", "numpad9", KEY_PAD_9},
            {"num/", "numpad_divide", KEY_PAD_DIVIDE},
            {"num*", "numpad_multiply", KEY_PAD_MULTIPLY},
            {"num-", "numpad_minus", KEY_PAD_MINUS},
            {"num+", "numpad_plus", KEY_PAD_PLUS},
            {"numenter", "numpad_enter", KEY_PAD_ENTER},
            {"num.", "numpad_decimal", KEY_PAD_DECIMAL},
            {"num,", "numpad_comma", KEY_PAD_COMMA},
            {"num=", "numpad_equal", KEY_PAD_EQUAL},
            {"numclr", "numpad_clear", KEY_PAD_CLEAR},
            {"[", "left_bracket", KEY_LBRACKET},
            {"]", "right_bracket", KEY_RBRACKET},
            {";", "semicolon", KEY_SEMICOLON},
            {"'", "apostrophe", KEY_APOSTROPHE},
            {"`", "backquote", KEY_BACKQUOTE},
            {",", "comma", KEY_COMMA},
            {".", "period", KEY_PERIOD},
            {"/", "slash", KEY_SLASH},
            {"\\", "backslash", KEY_BACKSLASH},
            {"-", "minus", KEY_MINUS},
            {"=", "equal", KEY_EQUAL},
            {"ent", "enter", KEY_ENTER},
            {"space", "space", KEY_SPACE},
            {"bksp", "backspace", KEY_BACKSPACE},
            {"tab", "tab", KEY_TAB},
            {"caps", "capslock", KEY_CAPSLOCK},
            {"numlk", "numlock", KEY_NUMLOCK},
            {"esc", "escape", KEY_ESCAPE},
            {"scrlk", "scrolllock", KEY_SCROLLLOCK},
            {"ins", "insert", KEY_INSERT},
            {"del", "delete", KEY_DELETE},
            {"home", "home", KEY_HOME},
            {"end", "end", KEY_END},
            {"pgup", "page_up", KEY_PAGEUP},
            {"pgdown", "page_down", KEY_PAGEDOWN},
            {"break", "break", KEY_BREAK},
            {"print", "printscreen", KEY_PRINTSCREEN},
            {"lshift", "left_shift", KEY_LSHIFT},
            {"rshift", "right_shift", KEY_RSHIFT},
            {"lalt", "left_alt", KEY_LALT},
            {"ralt", "right_alt", KEY_RALT},
            {"lctrl", "left_control", KEY_LCONTROL},
            {"rctrl", "right_control", KEY_RCONTROL},
            {"lwin", "left_windows", KEY_LWIN},
            {"rwin", "right_windows", KEY_RWIN},
            {"app", "application", KEY_APP},
            {"up", "up_arrow", KEY_UP},
            {"left", "left_arrow", KEY_LEFT},
            {"down", "down_arrow", KEY_DOWN},
            {"right", "right_arrow", KEY_RIGHT},
            {"f1", "function1", KEY_F1},
            {"f2", "function2", KEY_F2},
            {"f3", "function3", KEY_F3},
            {"f4", "function4", KEY_F4},
            {"f5", "function5", KEY_F5},
            {"f6", "function6", KEY_F6},
            {"f7", "function7", KEY_F7},
            {"f8", "function8", KEY_F8},
            {"f9", "function9", KEY_F9},
            {"f10", "function10", KEY_F10},
            {"f11", "function11", KEY_F11},
            {"f12", "function12", KEY_F12},
            {"f13", "function13", KEY_F13},
            {"f14", "function14", KEY_F14},
            {"f15", "function15", KEY_F15},
            {"f16", "function16", KEY_F16},
            {"f17", "function17", KEY_F17},
            {"f18", "function18", KEY_F18},
            {"f19", "function19", KEY_F19},
            {"f20", "function20", KEY_F20},
            {"f21", "function21", KEY_F21},
            {"f22", "function22", KEY_F22},
            {"f23", "function23", KEY_F23},
            {"f24", "function24", KEY_F24},
            {"caps_toggle", "capslock_toggle", KEY_CAPSLOCKTOGGLE},
            {"num_toggle", "numlock_toggle", KEY_NUMLOCKTOGGLE},
            {"scroll_toggle", "scrolllock_toggle", KEY_SCROLLLOCKTOGGLE},
            {"mouse1", "mouse_left", MOUSE_LEFT},
            {"mouse2", "mouse_right", MOUSE_RIGHT},
            {"mouse3", "mouse_middle", MOUSE_MIDDLE},
            {"mouse4", "mouse4", MOUSE_4},
            {"mouse5", "mouse5", MOUSE_5},
            {"mwheelup", "mouse_wheel_up", MOUSE_WHEEL_UP},
            {"mwheeldown", "mouse_wheel_down", MOUSE_WHEEL_DOWN}
        };
        static CInput& Get();
        void Initialize(const ini::IniSection& userKeyBinds);
        void KeyPressed(KEYCODE key, int modifiers = 0);
        void KeyReleased(KEYCODE key, int modifiers = 0);
        void MouseMove(float x, float y, int modifiers = 0); // 2D
        void MouseMoveRelative(float dx, float dy, int modifiers = 0); // 3D
        void MouseButtonPressed(int button, int modifiers = 0);
        void MouseButtonReleased(int button, int modifiers = 0);
        void MouseWheel(float wheelX, float wheelY, int modifiers = 0);
        void TextInput(const std::string& text, int modifiers = 0);
        void FocusChanged(bool focused);
        void BindKeyToAction(const std::string& actionName, KEYCODE key);
        void UnBindKeyFromAction(const std::string& actionName, KEYCODE key);
        void UnBindKeyFromEverywhere(KEYCODE key);
        ListenerId BindToAction(const std::string& actionName, MessageCallback callback, bool ignoreInputAllowed = false);
        void UnbindFromAction(ListenerId listenerId);
        ListenerId BindToAnyInput(MessageCallback callback, bool ignoreInputAllowed = false);
        void UnbindFromAnyInput(ListenerId listenerId);
        void AddMouseUnlock(const std::string& unlockName);
        void RemoveMouseUnlock(const std::string& unlockName);
        void SetMouseBounds(MouseBounds* newbounds) { m_mouseBoundsOverride = newbounds; }
        MouseBounds* GetMouseBounds() { return m_mouseBoundsOverride; }
        void RemoveMouseBounds() { m_mouseBoundsOverride = nullptr; }
        bool IsInputAllowed() const { return m_mouseUnlockers.empty(); }
        bool IsKeyDown(KEYCODE key) const { return m_rawKeys.contains(key); }
        bool IsActionActive(const std::string& actionName) const {
            const auto it = m_actions.find(actionName);
            return it != m_actions.end() && it->second.IsActive;
        }
        KEYCODE StringToKey(std::string string);
        std::string KeyToString(KEYCODE key, bool useShortName = false);
        Action& GetAction(const std::string& actionName);
        bool IsLetterKey(KEYCODE code) const { return (code >= KEY_A) && (code <= KEY_Z); }
        bool IsLetterOrNumberKey(KEYCODE code) const { return (code >= KEY_0) && (code <= KEY_Z); }
        bool IsSpaceKey(KEYCODE code) const { return code == KEY_ENTER || code == KEY_TAB || code == KEY_SPACE; }
        bool IsKeypadKey(KEYCODE code) const { return code >= KEY_PAD_0 && code <= KEY_PAD_DECIMAL; }
        bool IsPunctuation(KEYCODE code) const { return code >= KEY_0 && code <= KEY_SPACE && !IsLetterOrNumberKey(code) && !IsSpaceKey(code) && !IsKeypadKey(code); }
        bool IsKeyBoardKey(KEYCODE code) const { return code >= KEY_NONE && code <= _lastkey; }
        bool IsMouseKey(KEYCODE code) const { return code >= _firstmouse && code <= _lastmouse; }
        bool IsGamePadKey(KEYCODE code) const { return code >= _firstgamepad && code <= _lastgamepad; }
        CInput() = default;
    private:
        struct Listener
        {
            ListenerId id = 0;
            MessageCallback callback;
            bool ignoreInputAllowed = false;
        };
        void EmitAnyInput(const InputMessage& message);
        void EmitActionTransition(const std::string& actionName, KEYCODE key, bool oldActive, bool oldRawActive);
        void RecomputeActionState(const std::string& actionName, KEYCODE key = KEY_INVALID);
        void RecomputeActionStatesForKey(KEYCODE key);
        void RecomputeAllActionStates();
        static KEYCODE MouseButtonToKey(int button);
        std::unordered_set<KEYCODE> m_rawKeys;
        std::unordered_map<std::string, Action> m_actions;
        std::unordered_map<KEYCODE, std::unordered_set<std::string>> m_keyToActions;
        std::unordered_map<std::string, std::vector<Listener>> m_actionListeners;
        std::vector<Listener> m_globalListeners;
        std::vector<std::string> m_mouseUnlockers;
        ListenerId m_nextListenerId = 1;
        bool m_haveMousePosition = false;
        float m_lastMouseX = 0.0f;
        float m_lastMouseY = 0.0f;
        MouseBounds* m_mouseBoundsOverride = nullptr;
    };
    inline CInput& Input() { return CInput::Get(); }
}