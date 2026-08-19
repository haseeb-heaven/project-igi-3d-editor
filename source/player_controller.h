// player_controller.h - Runtime player locomotion, physics integration & camera placement
#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cstdint>
#include "player_collision.h"

namespace igi {

struct PlayerInputCmd {
    float forward = 0.0f;     // [-1.0 .. +1.0]
    float strafe = 0.0f;      // [-1.0 .. +1.0]
    float yaw_delta = 0.0f;   // Look yaw delta degrees
    float pitch_delta = 0.0f; // Look pitch delta degrees
    bool jump = false;        // Jump key triggered
    bool crouch = false;      // Crouch key held
    bool fire = false;        // Primary fire held
    bool reload = false;      // Reload key pressed
};

enum class PlayerStanceState {
    Standing,
    Crouching,
    Airborne,
    Dead
};

class PlayerController {
public:
    static constexpr float GRAVITY_PER_TICK = 84.741692f; // IGI 1 gravity constant (~18.6 m/s^2)
    static constexpr float JUMP_VERTICAL_SPEED = 1024.0f; // Jump launch impulse
    static constexpr float JUMP_HORIZONTAL_SPEED = 350.0f;
    static constexpr float AIR_CONTROL_SPEED = 120.0f;
    static constexpr float STANDING_EYE_HEIGHT = 180.0f;
    static constexpr float CROUCHING_EYE_HEIGHT = 90.0f;
    static constexpr float RUN_SPEED = 350.0f;
    static constexpr float WALK_SPEED = 180.0f;
    static constexpr float CROUCH_SPEED = 100.0f;

    PlayerController();

    void Reset(const glm::vec3& spawn_pos, float spawn_yaw = 0.0f);

    // Fixed 30 Hz simulation tick
    void Tick(const PlayerInputCmd& cmd, float (*get_terrain_z)(float x, float y), const glm::vec3& anim_root_delta);

    // Damage & Health
    void ApplyDamage(float amount);
    float GetHealth() const { return health_; }
    float GetArmor() const { return armor_; }
    bool IsAlive() const { return health_ > 0.0f; }

    // Coordinates & Camera
    const glm::vec3& GetPosition() const { return position_; }
    const glm::vec3& GetVelocity() const { return velocity_; }
    glm::vec3 GetEyePosition() const;
    float GetYaw() const { return yaw_; }
    float GetPitch() const { return pitch_; }
    PlayerStanceState GetStance() const { return stance_; }
    bool IsGrounded() const { return is_grounded_; }

    void SetPosition(const glm::vec3& pos) { position_ = pos; }
    void SetRotation(float yaw, float pitch) { yaw_ = yaw; pitch_ = pitch; }

private:
    glm::vec3 position_ = glm::vec3(0.0f);
    glm::vec3 velocity_ = glm::vec3(0.0f);
    float yaw_ = 0.0f;
    float pitch_ = 0.0f;
    float current_eye_height_ = STANDING_EYE_HEIGHT;
    float health_ = 100.0f;
    float armor_ = 100.0f;
    bool is_grounded_ = true;
    PlayerStanceState stance_ = PlayerStanceState::Standing;
    PlayerCollision collision_;
};

} // namespace igi
