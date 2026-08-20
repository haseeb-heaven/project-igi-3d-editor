// player_controller.h - Fixed-step runtime player locomotion and health.
#pragma once

#include <functional>
#include <vector>

#include <glm/glm.hpp>

#include "player_collision.h"

namespace igi {

struct PlayerInputCmd {
    float forward = 0.0f;
    float strafe = 0.0f;
    float yaw_delta = 0.0f;
    float pitch_delta = 0.0f;
    bool jump = false;
    bool crouch = false;
    bool sprint = false;
    bool fire = false;
    bool zoom = false;
    bool reload = false;
    bool interact = false;
    int switch_weapon = -1;
};

enum class PlayerStanceState {
    Standing,
    Crouching,
    Airborne,
    Dead
};

class PlayerController {
public:
    static constexpr float WORLD_METER = 4096.0f;

    // Verified-reference values from OpenIGI HumanMotion and HumanWallProbe.
    static constexpr float GRAVITY = 84.741692f;
    static constexpr float JUMP_SPEED = 1024.0f;
    static constexpr float STANDING_EYE_HEIGHT = 7372.8f;
    static constexpr float CROUCHING_EYE_HEIGHT = 5324.8f;
    static constexpr float AIR_CONTROL_SPEED = 18.96296f;

    // The current editor branch has no animation root-motion stream yet. These
    // are fixed-step presentation placeholders, kept in units per tick so the
    // simulation remains deterministic until vanilla animation data is wired.
    static constexpr float RUN_SPEED = 8.5f * WORLD_METER / 30.0f;
    static constexpr float WALK_SPEED = 4.5f * WORLD_METER / 30.0f;
    static constexpr float CROUCH_SPEED = 2.5f * WORLD_METER / 30.0f;
    static constexpr float SPRINT_MULTIPLIER = 1.5f;
    static constexpr float BODY_RADIUS = 0.4f * WORLD_METER;
    static constexpr float CROUCH_EASE_UNITS_PER_TICK = 250.0f;

    using SolidGeometryQuery = PlayerCollision::SolidGeometryQuery;
    using CeilingHeightQuery = PlayerCollision::CeilingHeightQuery;

    struct Tuning {
        float maximum_health = 100.0f;
        float maximum_armor = 100.0f;
        float walk_speed_units_per_tick = WALK_SPEED;
        float run_speed_units_per_tick = RUN_SPEED;
        float crouch_speed_units_per_tick = CROUCH_SPEED;
        float sprint_multiplier = SPRINT_MULTIPLIER;
        float jump_speed_units_per_tick = JUMP_SPEED;
        float air_control_speed_units_per_tick = AIR_CONTROL_SPEED;
        float gravity_units_per_tick = GRAVITY;
        float standing_eye_height_units = STANDING_EYE_HEIGHT;
        float crouching_eye_height_units = CROUCHING_EYE_HEIGHT;
    };

    PlayerController();

    void Reset(const glm::vec3& spawn_position, float spawn_yaw = 0.0f);
    void ApplyTuning(float maximum_health, float maximum_armor);
    void ApplyTuning(const Tuning& tuning);

    void SetCollisionQuery(SolidGeometryQuery solid_geometry_query);
    void SetCeilingQuery(CeilingHeightQuery ceiling_height_query);

    // Runs exactly one deterministic 1/30-second simulation step.
    void Tick(
        const PlayerInputCmd& input_command,
        PlayerCollision::TerrainHeightQuery terrain_height_query,
        const std::vector<ObstacleCollider>& obstacles = {},
        bool (*legacy_collision_query)(float x, float y, float z) = nullptr);

    void ApplyDamage(float damage_amount);
    float GetHealth() const { return health_; }
    float GetMaximumHealth() const { return maximum_health_; }
    float GetArmor() const { return armor_; }
    float GetMaximumArmor() const { return maximum_armor_; }
    bool IsAlive() const { return health_ > 0.0f; }

    const glm::vec3& GetPosition() const { return position_; }
    const glm::vec3& GetVelocity() const { return velocity_; }
    glm::vec3 GetEyePosition() const;
    float GetYaw() const { return yaw_; }
    float GetPitch() const { return pitch_; }
    float GetEyeHeight() const { return current_eye_height_; }
    PlayerStanceState GetStance() const { return stance_; }
    bool IsGrounded() const { return is_grounded_; }

    void SetPosition(const glm::vec3& position) { position_ = position; }
    void SetRotation(float yaw, float pitch) { yaw_ = yaw; pitch_ = pitch; }
    void SetOrientation(float yaw, float pitch) { yaw_ = yaw; pitch_ = pitch; }

private:
    glm::vec3 CalculateMovementDirection(const PlayerInputCmd& input_command) const;
    bool ResolveRequestedStance(bool requested_crouch) const;
    void IntegrateGroundMovement(const PlayerInputCmd& input_command, const glm::vec3& movement_direction);
    void IntegrateAirMovement(const PlayerInputCmd& input_command, const glm::vec3& movement_direction, bool took_off);
    void UpdateEyeHeight(bool crouching);

    glm::vec3 position_ = glm::vec3(0.0f);
    glm::vec3 velocity_ = glm::vec3(0.0f);
    float yaw_ = 0.0f;
    float pitch_ = 0.0f;
    float current_eye_height_ = STANDING_EYE_HEIGHT;
    float maximum_health_ = 100.0f;
    float health_ = maximum_health_;
    float maximum_armor_ = 100.0f;
    float armor_ = maximum_armor_;
    float walk_speed_units_per_tick_ = WALK_SPEED;
    float run_speed_units_per_tick_ = RUN_SPEED;
    float crouch_speed_units_per_tick_ = CROUCH_SPEED;
    float sprint_multiplier_ = SPRINT_MULTIPLIER;
    float jump_speed_units_per_tick_ = JUMP_SPEED;
    float air_control_speed_units_per_tick_ = AIR_CONTROL_SPEED;
    float gravity_units_per_tick_ = GRAVITY;
    float standing_eye_height_units_ = STANDING_EYE_HEIGHT;
    float crouching_eye_height_units_ = CROUCHING_EYE_HEIGHT;
    glm::vec3 slope_slide_velocity_ = glm::vec3(0.0f);
    bool is_grounded_ = true;
    PlayerStanceState stance_ = PlayerStanceState::Standing;
    PlayerCollision collision_;
};

} // namespace igi
