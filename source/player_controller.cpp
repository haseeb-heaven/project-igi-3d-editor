// player_controller.cpp - Fixed-step runtime player locomotion and health.
#include "player_controller.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace igi {

namespace {

constexpr float kMinimumInputMagnitude = 0.0001f;

glm::vec3 NormalizeMovementDirection(const glm::vec3& movement_direction) {
    const float magnitude_squared = glm::dot(movement_direction, movement_direction);
    if (magnitude_squared <= kMinimumInputMagnitude * kMinimumInputMagnitude) {
        return glm::vec3(0.0f);
    }
    if (magnitude_squared <= 1.0f) {
        return movement_direction;
    }
    return movement_direction / std::sqrt(magnitude_squared);
}

} // namespace

PlayerController::PlayerController() {
    Reset(glm::vec3(0.0f));
}

void PlayerController::Reset(const glm::vec3& spawn_position, float spawn_yaw) {
    position_ = spawn_position;
    velocity_ = glm::vec3(0.0f);
    yaw_ = spawn_yaw;
    pitch_ = 0.0f;
    current_eye_height_ = STANDING_EYE_HEIGHT;
    health_ = maximum_health_;
    armor_ = maximum_armor_;
    is_grounded_ = false;
    stance_ = PlayerStanceState::Standing;
}

void PlayerController::ApplyTuning(float maximum_health, float maximum_armor) {
    maximum_health_ = std::max(0.0f, maximum_health);
    maximum_armor_ = std::max(0.0f, maximum_armor);
    health_ = maximum_health_;
    armor_ = maximum_armor_;
    stance_ = PlayerStanceState::Standing;
}

void PlayerController::SetCollisionQuery(SolidGeometryQuery solid_geometry_query) {
    collision_.SetSolidQuery(std::move(solid_geometry_query));
}

void PlayerController::SetCeilingQuery(CeilingHeightQuery ceiling_height_query) {
    collision_.SetCeilingQuery(std::move(ceiling_height_query));
}

void PlayerController::ApplyDamage(float damage_amount) {
    if (damage_amount <= 0.0f || !IsAlive()) {
        return;
    }

    // The editor runtime currently uses the existing 60% armor absorption
    // contract. Damage values will be replaced by the vanilla hit tables once
    // the retail weapon evidence is connected to WeaponSystem.
    float health_damage = damage_amount;
    if (armor_ > 0.0f) {
        const float absorbed_damage = std::min(armor_, damage_amount * 0.60f);
        armor_ -= absorbed_damage;
        health_damage -= absorbed_damage;
    }

    health_ = std::max(0.0f, health_ - health_damage);
    if (health_ <= 0.0f) {
        stance_ = PlayerStanceState::Dead;
        velocity_ = glm::vec3(0.0f);
        is_grounded_ = false;
    }
}

glm::vec3 PlayerController::GetEyePosition() const {
    return glm::vec3(
        position_.x,
        position_.y,
        position_.z + current_eye_height_);
}

glm::vec3 PlayerController::CalculateMovementDirection(const PlayerInputCmd& input_command) const {
    const float yaw_radians = glm::radians(yaw_);
    const float cosine_yaw = std::cos(yaw_radians);
    const float sine_yaw = std::sin(yaw_radians);
    const glm::vec3 forward_direction(-sine_yaw, cosine_yaw, 0.0f);
    const glm::vec3 right_direction(cosine_yaw, sine_yaw, 0.0f);

    const float forward_input = std::clamp(input_command.forward, -1.0f, 1.0f);
    const float strafe_input = std::clamp(input_command.strafe, -1.0f, 1.0f);
    return NormalizeMovementDirection(
        forward_direction * forward_input + right_direction * strafe_input);
}

bool PlayerController::ResolveRequestedStance(bool requested_crouch) const {
    if (requested_crouch) {
        return true;
    }
    if (stance_ != PlayerStanceState::Crouching) {
        return false;
    }
    return !collision_.CanStandUp(position_, STANDING_EYE_HEIGHT, nullptr);
}

void PlayerController::IntegrateGroundMovement(
    const PlayerInputCmd& input_command,
    const glm::vec3& movement_direction) {
    const float movement_speed = input_command.crouch
        ? CROUCH_SPEED
        : (input_command.sprint ? RUN_SPEED * SPRINT_MULTIPLIER : RUN_SPEED);
    velocity_.x = movement_direction.x * movement_speed;
    velocity_.y = movement_direction.y * movement_speed;
    velocity_.z = 0.0f;
}

void PlayerController::IntegrateAirMovement(
    const PlayerInputCmd& input_command,
    const glm::vec3& movement_direction,
    bool took_off) {
    if (!took_off) {
        velocity_.z -= GRAVITY;
    }

    const float input_magnitude = std::clamp(
        std::abs(input_command.forward) + std::abs(input_command.strafe),
        0.0f,
        1.0f);
    velocity_.x += movement_direction.x * AIR_CONTROL_SPEED * input_magnitude;
    velocity_.y += movement_direction.y * AIR_CONTROL_SPEED * input_magnitude;
}

void PlayerController::UpdateEyeHeight(bool crouching) {
    const float target_eye_height = crouching
        ? CROUCHING_EYE_HEIGHT
        : STANDING_EYE_HEIGHT;
    const float eye_height_delta = std::clamp(
        target_eye_height - current_eye_height_,
        -CROUCH_EASE_UNITS_PER_TICK,
        CROUCH_EASE_UNITS_PER_TICK);
    current_eye_height_ += eye_height_delta;
}

void PlayerController::Tick(
    const PlayerInputCmd& input_command,
    PlayerCollision::TerrainHeightQuery terrain_height_query,
    const std::vector<ObstacleCollider>& obstacles,
    bool (*legacy_collision_query)(float x, float y, float z)) {
    if (!IsAlive()) {
        return;
    }

    yaw_ += input_command.yaw_delta;
    pitch_ = std::clamp(pitch_ + input_command.pitch_delta, -89.0f, 89.0f);

    if (legacy_collision_query != nullptr) {
        SetCollisionQuery([legacy_collision_query](const glm::vec3& sample_position) {
            return legacy_collision_query(
                sample_position.x,
                sample_position.y,
                sample_position.z);
        });
    }

    const bool should_crouch = ResolveRequestedStance(input_command.crouch);
    UpdateEyeHeight(should_crouch);

    const PlayerGroundQuery ground_query = collision_.QueryGround(
        position_,
        current_eye_height_,
        terrain_height_query,
        is_grounded_,
        should_crouch);
    is_grounded_ = ground_query.is_grounded && velocity_.z <= 0.0f;

    const glm::vec3 movement_direction = CalculateMovementDirection(input_command);
    if (is_grounded_) {
        stance_ = should_crouch ? PlayerStanceState::Crouching : PlayerStanceState::Standing;
        position_.z = ground_query.ground_height;
        IntegrateGroundMovement(input_command, movement_direction);

        if (input_command.jump && !should_crouch) {
            velocity_.z = JUMP_SPEED;
            is_grounded_ = false;
            stance_ = PlayerStanceState::Airborne;
        }
    } else {
        stance_ = PlayerStanceState::Airborne;
        IntegrateAirMovement(input_command, movement_direction, false);
    }

    const glm::vec3 requested_position = position_ + velocity_;
    const PlayerWallSweepResult wall_sweep_result = collision_.SweepWalls(
        position_,
        requested_position,
        BODY_RADIUS,
        current_eye_height_,
        is_grounded_,
        should_crouch);
    position_ += wall_sweep_result.slide_velocity;

    if (!obstacles.empty()) {
        collision_.ResolveObstacles(position_, obstacles, BODY_RADIUS);
    }

    const PlayerGroundQuery post_move_ground_query = collision_.QueryGround(
        position_,
        current_eye_height_,
        terrain_height_query,
        is_grounded_,
        should_crouch);
    if (velocity_.z <= 0.0f && post_move_ground_query.is_grounded) {
        position_.z = post_move_ground_query.ground_height;
        velocity_.z = 0.0f;
        is_grounded_ = true;
        stance_ = should_crouch ? PlayerStanceState::Crouching : PlayerStanceState::Standing;
    } else {
        is_grounded_ = false;
        stance_ = PlayerStanceState::Airborne;
    }

}

} // namespace igi
