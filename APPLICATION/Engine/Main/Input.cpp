//============== ENERGY ENGINE =================//
//                                              //
// Purpose: Make game do smth on user click key //
//                                              //
//==============================================//

#include "Input.h"
#include <algorithm>
#include <utility>

namespace Engine
{
    std::unordered_map<std::string, KEYCODE> keyLookupMap;
    std::unordered_map<KEYCODE, std::string> stringLookupMapShort;
    std::unordered_map<KEYCODE, std::string> stringLookupMapFull;
    CInput& CInput::Get() {
        static CInput instance;
        return instance;
    }
    void CInput::Initialize(const ini::IniSection& userKeyBinds) {
        m_rawKeys.clear();
        m_actions.clear();
        m_keyToActions.clear();
        m_actionListeners.clear();
        m_globalListeners.clear();
        m_mouseUnlockers.clear();
        m_mouseBoundsOverride = nullptr;
        m_nextListenerId = 1;
        m_haveMousePosition = false;
        m_lastMouseX = 0.0f;
        m_lastMouseY = 0.0f;
        keyLookupMap.reserve(CInput::keynames.size() * 2);
        stringLookupMapShort.reserve(CInput::keynames.size() * 2);
        stringLookupMapFull.reserve(CInput::keynames.size() * 2);
        for (const auto& mapping : CInput::keynames) {
            keyLookupMap[mapping.shortName] = mapping.keyCode;
            keyLookupMap[mapping.fullName] = mapping.keyCode;
            stringLookupMapShort[mapping.keyCode] = mapping.shortName;
            stringLookupMapFull[mapping.keyCode] = mapping.fullName;
        }
        for (const auto& [actionName, field] : userKeyBinds)
            BindKeyToAction(actionName, StringToKey(field.as<std::string>()));
    }
    void CInput::BindKeyToAction(const std::string& actionName, KEYCODE key) {
        if (actionName.empty() || key == KEY_INVALID || key == KEY_NONE)
            return;
        Action& action = CInput::GetAction(actionName);
        if (action.Bindings.insert(key).second) {
            m_keyToActions[key].insert(actionName);
        }
    }
    void CInput::UnBindKeyFromAction(const std::string& actionName, KEYCODE key) {
        const auto actionIt = m_actions.find(actionName);
        if (actionIt == m_actions.end() || actionIt->second.Bindings.erase(key) == 0)
            return;
        const auto keyIt = m_keyToActions.find(key);
        if (keyIt != m_keyToActions.end()) {
            keyIt->second.erase(actionName);
            if (keyIt->second.empty())
                m_keyToActions.erase(keyIt);
        }
        RecomputeActionState(actionName, key);
    }
    void CInput::UnBindKeyFromEverywhere(KEYCODE key) {
        auto keyIt = m_keyToActions.find(key);
        if (keyIt == m_keyToActions.end())
            return;
        const auto affectedActions = keyIt->second;
        for (const std::string& actionName : affectedActions)
            UnBindKeyFromAction(actionName, key);
    }
    CInput::ListenerId CInput::BindToAction(const std::string& actionName, MessageCallback callback, bool ignoreInputAllowed) {
        if (actionName.empty() || !callback)
            return 0;
        const ListenerId id = m_nextListenerId++;
        m_actionListeners[actionName].push_back(Listener{ id, std::move(callback), ignoreInputAllowed });
        return id;
    }
    void CInput::UnbindFromAction(ListenerId listenerId) {
        if (listenerId == 0)
            return;
        for (auto it = m_actionListeners.begin(); it != m_actionListeners.end();) {
            auto& listeners = it->second;
            listeners.erase(std::remove_if(listeners.begin(), listeners.end(),
                [listenerId](const Listener& listener) { return listener.id == listenerId; }), listeners.end());
            if (listeners.empty())
                it = m_actionListeners.erase(it);
            else
                ++it;
        }
    }
    CInput::ListenerId CInput::BindToAnyInput(MessageCallback callback, bool ignoreInputAllowed) {
        if (!callback)
            return 0;
        const ListenerId id = m_nextListenerId++;
        m_globalListeners.push_back(Listener{ id, std::move(callback), ignoreInputAllowed });
        return id;
    }
    void CInput::UnbindFromAnyInput(ListenerId listenerId) {
        m_globalListeners.erase(std::remove_if(m_globalListeners.begin(), m_globalListeners.end(),
            [listenerId](const Listener& listener) { return listener.id == listenerId; }), m_globalListeners.end());
    }
    void CInput::KeyPressed(KEYCODE key, int modifiers) {
        if (!IsKeyBoardKey(key) && !IsMouseKey(key))
            return;
        if (!m_rawKeys.insert(key).second)
            return;
        InputMessage message{};
        message.Type = ButtonBegan;
        message.Key = key;
        message.Modifiers = modifiers;
        message.Pressed = true;
        EmitAnyInput(message);
        RecomputeActionStatesForKey(key);
    }
    void CInput::KeyReleased(KEYCODE key, int modifiers) {
        if (!IsKeyBoardKey(key) && !IsMouseKey(key))
            return;
        if (m_rawKeys.erase(key) == 0)
            return;
        InputMessage message{};
        message.Type = ButtonEnded;
        message.Key = key;
        message.Modifiers = modifiers;
        message.Pressed = false;
        EmitAnyInput(message);
        RecomputeActionStatesForKey(key);
    }
    void CInput::MouseMove(float x, float y, int modifiers) {
        const float dx = m_haveMousePosition ? (x - m_lastMouseX) : 0.0f;
        const float dy = m_haveMousePosition ? (y - m_lastMouseY) : 0.0f;
        m_lastMouseX = x;
        m_lastMouseY = y;
        m_haveMousePosition = true;
        MouseMoveRelative(dx, dy, modifiers);
    }
    void CInput::MouseMoveRelative(float dx, float dy, int modifiers) {
        InputMessage message{};
        message.Type = InputMessageType::MouseMoved;
        message.MouseDX = dx;
        message.MouseDY = dy;
        message.Modifiers = modifiers;
        EmitAnyInput(message);
    }
    void CInput::MouseButtonPressed(int button, int modifiers) {
        const KEYCODE key = MouseButtonToKey(button);
        if (key != KEY_INVALID)
            KeyPressed(key, modifiers);
    }
    void CInput::MouseButtonReleased(int button, int modifiers) {
        const KEYCODE key = MouseButtonToKey(button);
        if (key != KEY_INVALID)
            KeyReleased(key, modifiers);
    }
    void CInput::MouseWheel(float wheelX, float wheelY, int modifiers) {
        InputMessage message{};
        message.Type = InputMessageType::MouseWheelChanged;
        message.Modifiers = modifiers;
        message.WheelX = wheelX;
        message.WheelY = wheelY;
        EmitAnyInput(message);
        auto emitWheelAction = [&](KEYCODE key) { KeyPressed(key, modifiers); KeyReleased(key, modifiers); };
        if (wheelY > 0.0f || wheelX > 0.0f)
            emitWheelAction(MOUSE_WHEEL_UP);
        if (wheelY < 0.0f || wheelX < 0.0f)
            emitWheelAction(MOUSE_WHEEL_DOWN);
    }
    void CInput::TextInput(const std::string& text, int modifiers) {
        if (text.empty())
            return;
        InputMessage message{};
        message.Type = InputMessageType::TextInputReceived;
        message.Text = text;
        message.Modifiers = modifiers;
        EmitAnyInput(message);
    }
    void CInput::FocusChanged(bool focused) {
        InputMessage message{};
        message.Type = InputMessageType::FocusChangedEvent;
        message.Pressed = focused;
        EmitAnyInput(message);
        if (focused)
            return;
        m_haveMousePosition = false;
        const auto pressedKeys = m_rawKeys;
        for (KEYCODE key : pressedKeys)
            KeyReleased(key);
    }
    void CInput::AddMouseUnlock(const std::string& unlockName) {
        if (unlockName.empty())
            return;
        if (std::find(m_mouseUnlockers.begin(), m_mouseUnlockers.end(), unlockName) != m_mouseUnlockers.end())
            return;
        const bool wasAllowed = IsInputAllowed();
        m_mouseUnlockers.push_back(unlockName);
        m_haveMousePosition = false;
        if (wasAllowed)
            RecomputeAllActionStates();
    }
    void CInput::RemoveMouseUnlock(const std::string& unlockName) {
        const auto it = std::find(m_mouseUnlockers.begin(), m_mouseUnlockers.end(), unlockName);
        if (it == m_mouseUnlockers.end())
            return;
        const bool wasBlocked = !IsInputAllowed();
        m_mouseUnlockers.erase(it);
        m_haveMousePosition = false;
        if (wasBlocked && IsInputAllowed())
            RecomputeAllActionStates();
    }
    CInput::Action& CInput::GetAction(const std::string& actionName) {
        Action& action = m_actions[actionName];
        action.Name = actionName;
        return action;
    }
    void CInput::EmitAnyInput(const InputMessage& message) {
        const auto listeners = m_globalListeners;
        for (const Listener& listener : listeners) {
            const bool inputAllowed = IsInputAllowed();
            const bool release = message.Type == ButtonEnded;
            if (listener.callback && (inputAllowed || listener.ignoreInputAllowed || release))
                listener.callback(message, inputAllowed);
        }
    }
    void CInput::EmitActionTransition(const std::string& actionName, KEYCODE key, bool oldActive, bool oldRawActive) {
        const auto actionIt = m_actions.find(actionName);
        if (actionIt == m_actions.end())
            return;
        const bool active = actionIt->second.IsActive;
        const bool rawActive = actionIt->second.IsRawActive;
        const auto globals = m_globalListeners;
        const auto listenerIt = m_actionListeners.find(actionName);
        const auto actions = listenerIt == m_actionListeners.end() ? std::vector<Listener>{} : listenerIt->second;
        auto dispatch = [&](const Listener& listener) {
            const bool before = listener.ignoreInputAllowed ? oldRawActive : oldActive;
            const bool after = listener.ignoreInputAllowed ? rawActive : active;
            if (!listener.callback || before == after)
                return;
            if (after && !listener.ignoreInputAllowed && !IsInputAllowed())
                return;
            InputMessage event{};
            event.Type = after ? ActionBegan : ActionEnded;
            event.Key = key;
            event.Action = actionName;
            event.Pressed = after;
            listener.callback(event, IsInputAllowed());
            };
        for (const Listener& listener : globals)
            dispatch(listener);
        for (const Listener& listener : actions)
            dispatch(listener);
    }
    void CInput::RecomputeActionState(const std::string& actionName, KEYCODE key) {
        const auto actionIt = m_actions.find(actionName);
        if (actionIt == m_actions.end())
            return;
        Action& action = actionIt->second;
        bool rawActive = false;
        for (KEYCODE boundKey : action.Bindings) {
            if (m_rawKeys.contains(boundKey)) {
                rawActive = true;
                break;
            }
        }
        const bool oldActive = action.IsActive;
        const bool oldRawActive = action.IsRawActive;
        action.IsRawActive = rawActive;
        action.IsActive = IsInputAllowed() && rawActive;
        if (oldActive != action.IsActive || oldRawActive != rawActive)
            EmitActionTransition(actionName, key, oldActive, oldRawActive);
    }
    void CInput::RecomputeActionStatesForKey(KEYCODE key) {
        const auto keyIt = m_keyToActions.find(key);
        if (keyIt == m_keyToActions.end())
            return;
        const auto names = keyIt->second;
        for (const std::string& actionName : names)
            RecomputeActionState(actionName, key);
    }
    void CInput::RecomputeAllActionStates() {
        std::vector<std::string> names;
        names.reserve(m_actions.size());
        for (const auto& entry : m_actions)
            names.push_back(entry.first);
        for (const std::string& name : names)
            RecomputeActionState(name);
    }
    KEYCODE CInput::MouseButtonToKey(int button) {
        switch (button)
        {
        case 0: return MOUSE_LEFT;
        case 1: return MOUSE_RIGHT;
        case 2: return MOUSE_MIDDLE;
        case 3: return MOUSE_4;
        case 4: return MOUSE_5;
        default: return KEY_INVALID;
        }
    }
    KEYCODE CInput::StringToKey(std::string string) {
        auto it = keyLookupMap.find(string);
        if (it != keyLookupMap.end())
            return it->second;
        return KEY_INVALID;
    }
    std::string CInput::KeyToString(KEYCODE key, bool useShortName) {
        if (useShortName) {
            auto it = stringLookupMapShort.find(key);
            if (it != stringLookupMapShort.end())
                return it->second;
            return "";
        }
        else {
            auto it = stringLookupMapFull.find(key);
            if (it != stringLookupMapFull.end())
                return it->second;
            return "";
        }
    }
}