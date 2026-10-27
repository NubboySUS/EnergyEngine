//============== ENERGY ENGINE =================//
//                                              //
// Purpose: Player controller implementation    //
//                                              //
//==============================================//

#include "Player.h"
#include "../APPLICATION/Engine/Main/Console.h"
#include "../APPLICATION/Engine/Main/Logger.h"

namespace Game {
    void Player::OnSpawned() {
        InputListener = Engine::Input().BindToAnyInput([this](const Engine::CInput::InputMessage& message, bool isInputAllowed) {
            if (!isInputAllowed || message.Type != Engine::CInput::MouseMoved)
                return;
            PendingMouseDX += static_cast<double>(message.MouseDX);
            PendingMouseDY += static_cast<double>(message.MouseDY);
            });
    }
    void Player::OnRemoved() {
        if (InputListener != 0) {
            Engine::Input().UnbindFromAnyInput(InputListener);
            InputListener = 0;
        }
    }
    void Player::Update(double deltaTime) noexcept {
        if (!ParentLevel || !std::isfinite(deltaTime) || deltaTime <= 0.0)
            return;
        if (!IsAlive) {
            if (RespawnTimeRemaining != -1.0f) {
                RespawnTimeRemaining = (std::max)(0.0f, RespawnTimeRemaining - static_cast<float>(deltaTime));
                if (RespawnTimeRemaining == 0.0f)
                    Respawn();
            }
            return;
        }
        // MOUSE INPUT
        if (!Engine::Input().IsInputAllowed()) {
            PendingMouseDX = 0.0;
            PendingMouseDY = 0.0;
        }
        // CALCULATE VELOCITY
        bool noclip = false;
        if (Console().GetConVar("noclip"))
            noclip = Console().GetConVar("noclip")->GetBool();
        float sens = 0.0f;
        if (Console().GetConVar("cfg_sens"))
            sens = Console().GetConVar("cfg_sens")->GetFloat();
        float accel = 0.0f;
        if (Console().GetConVar("plr_accelerate"))
            accel = Console().GetConVar("plr_accelerate")->GetFloat();
        float fric = 0.0f;
        if (Console().GetConVar("plr_friction"))
            fric = Console().GetConVar("plr_friction")->GetFloat();
        float speedmult = 1.0f;
        if (Console().GetConVar("plr_speedmultiplier"))
            speedmult = Console().GetConVar("plr_speedmultiplier")->GetFloat();
        float noclipspeed = 1.0f;
        if (Console().GetConVar("plr_noclipspeed"))
            noclipspeed = Console().GetConVar("plr_noclipspeed")->GetFloat();
        Z = WrapDegrees(Z + PendingMouseDX * sens);
        Y = std::clamp(Y + PendingMouseDY * sens, -90.0, 90.0);
        PendingMouseDX = 0.0;
        PendingMouseDY = 0.0;
        Vector3 forward{ std::sin(DegreesToRadians(Z)) * std::cos(DegreesToRadians(Y)), std::cos(DegreesToRadians(Z)) * std::cos(DegreesToRadians(Y)), -std::sin(DegreesToRadians(Y)) };
        Vector3 right{ std::cos(DegreesToRadians(Z)), -std::sin(DegreesToRadians(Z)), 0.0 };
        Vector3 wishvel = forward * (Engine::Input().IsActionActive("forward") - Engine::Input().IsActionActive("backward")) + right * (Engine::Input().IsActionActive("left") - Engine::Input().IsActionActive("right"));
        if (noclip)
            wishvel.z += Engine::Input().IsActionActive("up") - Engine::Input().IsActionActive("down");
        wishvel = wishvel.Unit();
        double wishspeed = noclip ? noclipspeed : MoveSpeed * speedmult;
        if (wishspeed > 0.0) {
            const double addspeed = wishspeed - WishVelocity.Dot(wishvel);
            if (addspeed > 0.0)
                WishVelocity += wishvel * (std::min)(addspeed, accel * wishspeed * deltaTime);
        }
        const double speed = WishVelocity.Magnitude();
        if (speed < 0.1)
            WishVelocity = Vector3::Zero();
        else
            WishVelocity = WishVelocity * ((std::max)(0.0, speed - (std::max)(speed, MoveSpeed * 0.25) * fric * deltaTime) / speed);
        // APPLY VELOCITY
        if (noclip) {
            Position += WishVelocity * deltaTime;
            Velocity = WishVelocity;
        }
        if (CurrentCamera) {
            CurrentCamera->SetViewPoint(Position + Vector3{ 0.0, 0.0, EyeHeight });
            CurrentCamera->SetPitch(Y);
            CurrentCamera->SetYaw(Z);
            CurrentCamera->SetRoll(0.0);
        }
    }

    void Player::Respawn(bool forcePosition, const Vector3& position) noexcept {
        if (forcePosition || !ParentLevel)
            Position = position;
        else {
            auto spawnPoints = ParentLevel->GetObjectsOfType<Engine::Level::SpawnLocation>();
            if (spawnPoints.empty())
                Position = position;
            else {
                const auto* spawn = spawnPoints[randomInt(0, static_cast<int>(spawnPoints.size()) - 1)];
                const Vector3 mins = spawn->GetMins();
                const Vector3 maxs = spawn->GetMaxs();
                Position = Vector3(randomDouble(mins.x, maxs.x), randomDouble(mins.y, maxs.y), randomDouble(mins.z, maxs.z));
            }
        }
        Velocity = Vector3::Zero();
        WishVelocity = Vector3::Zero();
        HP = MaxHP;
        const bool wasDead = !IsAlive;
        RespawnTimeRemaining = 0.0f;
        IsAlive = true;
        PendingMouseDX = 0.0;
        PendingMouseDY = 0.0;
        if (wasDead && Respawned)
            Respawned(*this);
    }

    void Player::Kill() noexcept {
        if (!IsAlive)
            return;
        HP = 0.0f;
        IsAlive = false;
        Velocity = Vector3::Zero();
        WishVelocity = Vector3::Zero();
        RespawnTimeRemaining = RespawnTime >= 0.0f ? RespawnTime : -1.0f;
        PendingMouseDX = 0.0;
        PendingMouseDY = 0.0;
        if (Died)
            Died(*this);
    }

    void Player::Damage(float amount) noexcept {
        if (!IsAlive || !std::isfinite(amount) || amount <= 0.0f)
            return;
        HP = (std::max)(0.0f, HP - amount);
        if (HP <= 0.0f)
            Kill();
    }

    void Player::Heal(float amount) noexcept {
        if (!IsAlive || !std::isfinite(amount) || amount <= 0.0f)
            return;
        HP = (std::min)(MaxHP, HP + amount);
    }

    void Player::SetHealth(float health) noexcept {
        if (!std::isfinite(health))
            health = 0.0f;
        HP = std::clamp(health, 0.0f, MaxHP);
        if (HP <= 0.0f)
            Kill();
    }

    void Player::SetMaxHealth(float maxHealth) noexcept {
        if (!std::isfinite(maxHealth))
            maxHealth = 1.0f;
        MaxHP = (std::max)(1.0f, maxHealth);
        HP = (std::min)(HP, MaxHP);
    }

    void Player::SetRespawnTime(float seconds) noexcept {
        if (!std::isfinite(seconds))
            seconds = 0.0f;
        RespawnTime = seconds < 0.0f ? -1.0f : seconds;
        if (!IsAlive)
            RespawnTimeRemaining = RespawnTime >= 0.0f ? RespawnTime : -1.0f;
    }
}