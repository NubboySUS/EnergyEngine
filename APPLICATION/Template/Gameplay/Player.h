//============== ENERGY ENGINE =================//
//                                              //
// Purpose: Create player controller for user   //
//                                              //
//==============================================//

#pragma once
#include <cstdint>
#include <functional>
#include <algorithm>
#include <cmath>
#include "../APPLICATION/Engine/Main/Input.h"
#include "../APPLICATION/Engine/Main/Utils.h"
#include "../APPLICATION/Engine/World/Level.h"
#include "../APPLICATION/Engine/World/Camera.h"

namespace Game {
    class Player : public Engine::Level::Player {
    public:
        void Respawn(bool forcePosition = false, const Vector3& position = Vector3::Zero()) noexcept override;
        void Kill() noexcept;
        void Damage(float amount) noexcept;
        void Heal(float amount) noexcept;
        void SetHealth(float health) noexcept;
        void SetMaxHealth(float maxHealth) noexcept;
        void SetRespawnTime(float seconds) noexcept;
        void SetMoveSpeed(float unitsPerSecond) noexcept { if (std::isfinite(unitsPerSecond)) MoveSpeed = (std::max)(0.0f, unitsPerSecond); }
        void SetEyeHeight(float height) noexcept { if (std::isfinite(height)) EyeHeight = height; }
        void SetDiedCallback(std::function<void(Player&)> callback) { Died = std::move(callback); }
        void SetRespawnedCallback(std::function<void(Player&)> callback) { Respawned = std::move(callback); }
        float GetHealth() const noexcept { return HP; }
        float GetMaxHealth() const noexcept { return MaxHP; }
        float GetRespawnTime() const noexcept { return RespawnTime; }
        float GetRespawnRemaining() const noexcept { return RespawnTimeRemaining; }
        float GetMoveSpeed() const noexcept { return MoveSpeed; }
        float GetEyeHeight() const noexcept { return EyeHeight; }
        void Update(double deltaTime) noexcept override;
    private:
        void OnSpawned() override;
        void OnRemoved() override;
        Engine::CInput::ListenerId InputListener = 0;
        float HP = 100.0f;
        float MaxHP = 100.0f;
        float RespawnTime = 3.0f;
        float RespawnTimeRemaining = 0.0f;
        Vector3 WishVelocity = Vector3::Zero();
        float MoveSpeed = 5.0f;
        float EyeHeight = 1.65f;
        double PendingMouseDX = 0.0;
        double PendingMouseDY = 0.0;
        std::function<void(Player&)> Died;
        std::function<void(Player&)> Respawned;
    };
}