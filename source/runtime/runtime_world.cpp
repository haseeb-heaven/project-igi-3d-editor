#include "runtime_world.h"
#include "audio_system.h"
#include <algorithm>
#include <cmath>

namespace igi {

RuntimeWorld::RuntimeWorld() = default;
RuntimeWorld::~RuntimeWorld() = default;

void RuntimeWorld::Initialize(float (*get_terrain_z)(float x, float y), bool (*check_collision)(float x, float y, float z)) {
    get_terrain_z_ = get_terrain_z;
    check_collision_ = check_collision;
    if (check_collision_) {
        ai_.SetMovementCollisionQuery([check_collision](const glm::vec3& position) {
            return check_collision(
                position.x,
                position.y,
                position.z + 0.9f * PlayerController::WORLD_METER);
        });
    } else {
        ai_.SetMovementCollisionQuery({});
    }
    Reset();
}

void RuntimeWorld::Reset() {
    glm::vec3 spawn_pos(0.0f, 0.0f, 100.0f);
    if (get_terrain_z_) {
        spawn_pos.z = get_terrain_z_(spawn_pos.x, spawn_pos.y);
    }

    player_.Reset(spawn_pos, 0.0f);
    weapons_.SelectWeapon(0);
    ai_.Clear();
    task_tree_.Clear();
    level_flow_.InitializeMission(1);
    next_guard_attack_tick_.clear();
    footstep_timer_seconds_ = 0.0;
    extraction_zone_center_ = glm::vec3(1000.0f, 1000.0f, 0.0f);
    extraction_zone_radius_ = 8.0f * PlayerController::WORLD_METER;
}

void RuntimeWorld::SetExtractionZone(const glm::vec3& center, float radius) {
    extraction_zone_center_ = center;
    extraction_zone_radius_ = std::max(0.0f, radius);
}

void RuntimeWorld::UpdateSimulationTick(uint64_t tick_number, const PlayerInputCmd& input_cmd) {
    constexpr double dt = GameClock::TICK_INTERVAL_SECONDS;
    std::vector<ObstacleCollider> obstacles;
    for (const auto& guard : ai_.GetGuards()) {
        if (guard.state == AiGuardState::Dead) {
            continue;
        }
        ObstacleCollider obs;
        obs.center = guard.position;
        obs.radius = 0.4f * PlayerController::WORLD_METER;
        obs.height = 1.8f * PlayerController::WORLD_METER;
        obstacles.push_back(obs);
    }

    // 1. Tick player physics, obstacle & 3D building collision, and movement
    const bool was_grounded = player_.IsGrounded();
    player_.Tick(input_cmd, get_terrain_z_, obstacles, check_collision_);
    if (input_cmd.jump && was_grounded && !player_.IsGrounded()) {
        AudioSystem::Play(SoundEffect::Jump);
    }
    PlayFootstepIfNeeded(input_cmd, was_grounded);

    // 2. Weapon switching, firing & cooldowns
    if (input_cmd.switch_weapon >= 0 && input_cmd.switch_weapon <= 5) {
        weapons_.SelectWeapon(input_cmd.switch_weapon);
    }
    weapons_.Update(dt);
    if (input_cmd.fire) {
        BulletTrace trace;
        float yaw_rad = glm::radians(player_.GetYaw());
        float pitch_rad = glm::radians(player_.GetPitch());
        float sin_y = std::sin(yaw_rad);
        float cos_y = std::cos(yaw_rad);
        float sin_p = std::sin(pitch_rad);
        float cos_p = std::cos(pitch_rad);
        glm::vec3 aim_dir(-sin_y * cos_p, cos_y * cos_p, sin_p);

        if (weapons_.TryFire(player_.GetEyePosition(), aim_dir, trace)) {
            const bool hit_guard = ApplyPlayerShotDamage(trace);
            AudioSystem::Play(SoundEffect::Gunshot);
            if (hit_guard) {
                AudioSystem::Play(SoundEffect::BulletImpact);
            }
            // Post gunshot stimulus to AI
            AiStimulusEvent gunshot;
            gunshot.type = AiEventType::Gunshot;
            gunshot.position = player_.GetPosition();
            gunshot.loudness = 1.0f;
            gunshot.originator_id = 0; // Player ID
            gunshot.tick_timestamp = tick_number;
            ai_.GetEventQueue().Post(gunshot);
        }
    }

    if (input_cmd.reload) {
        weapons_.Reload();
        AudioSystem::Play(SoundEffect::Reload);
    }

    if (input_cmd.interact && level_flow_.CompleteFirstPendingPrimaryObjective()) {
        AudioSystem::Play(SoundEffect::ObjectiveComplete);
    }

    // 3. Tick AI perception & state machine
    ai_.Update(dt, player_.GetPosition(), player_.IsAlive());
    ApplyGuardCombatDamage(tick_number);

    // 4. Tick runtime task tree
    task_tree_.Update(dt);

    // 5. Evaluate mission flow & objectives
    bool in_extraction = (glm::distance(
        player_.GetPosition(),
        glm::vec3(
            extraction_zone_center_.x,
            extraction_zone_center_.y,
            player_.GetPosition().z)) < extraction_zone_radius_);
    level_flow_.Update(player_.IsAlive(), in_extraction);
}

bool RuntimeWorld::ApplyPlayerShotDamage(BulletTrace& bullet_trace) {
    constexpr float guard_aim_height = 1.7f * PlayerController::WORLD_METER;
    constexpr float guard_hit_radius = 0.6f * PlayerController::WORLD_METER;

    const float direction_length = glm::length(bullet_trace.direction);
    if (direction_length <= 0.0001f) {
        return false;
    }

    const glm::vec3 shot_direction = bullet_trace.direction / direction_length;
    float closest_hit_distance = bullet_trace.distance;
    uint32_t closest_guard_id = 0;
    glm::vec3 closest_hit_position = bullet_trace.hit_position;
    bool found_guard = false;

    for (const AiGuardEntity& guard : ai_.GetGuards()) {
        if (guard.state == AiGuardState::Dead) {
            continue;
        }

        const glm::vec3 guard_aim_position = guard.position + glm::vec3(
            0.0f, 0.0f, guard_aim_height);
        const glm::vec3 to_guard = guard_aim_position - bullet_trace.origin;
        const float projected_distance = glm::dot(to_guard, shot_direction);
        if (projected_distance < 0.0f || projected_distance > closest_hit_distance) {
            continue;
        }

        const glm::vec3 closest_point = bullet_trace.origin + shot_direction * projected_distance;
        const float miss_distance = glm::distance(closest_point, guard_aim_position);
        if (miss_distance > guard_hit_radius) {
            continue;
        }

        closest_hit_distance = projected_distance;
        closest_guard_id = guard.id;
        closest_hit_position = closest_point;
        found_guard = true;
    }

    if (!found_guard) {
        return false;
    }

    bullet_trace.hit_entity_id = closest_guard_id;
    bullet_trace.distance = closest_hit_distance;
    bullet_trace.hit_position = closest_hit_position;
    ai_.ApplyDamage(closest_guard_id, bullet_trace.damage);
    return true;
}

void RuntimeWorld::ApplyGuardCombatDamage(uint64_t tick_number) {
    constexpr float combat_range = 50.0f * PlayerController::WORLD_METER;
    constexpr uint64_t attack_interval_ticks = 30;
    constexpr float damage_per_attack = 5.0f;

    if (!player_.IsAlive()) {
        return;
    }

    for (const AiGuardEntity& guard : ai_.GetGuards()) {
        if (guard.state != AiGuardState::Combat || guard.health <= 0.0f) {
            continue;
        }
        if (glm::distance(guard.position, player_.GetPosition()) > combat_range) {
            continue;
        }

        uint64_t& next_attack_tick = next_guard_attack_tick_[guard.id];
        if (tick_number < next_attack_tick) {
            continue;
        }

        player_.ApplyDamage(damage_per_attack);
        next_attack_tick = tick_number + attack_interval_ticks;
        AudioSystem::Play(SoundEffect::Pain);
    }
}

void RuntimeWorld::PlayFootstepIfNeeded(const PlayerInputCmd& input_command, bool was_grounded) {
    const bool has_movement_input =
        std::abs(input_command.forward) > 0.01f || std::abs(input_command.strafe) > 0.01f;
    if (!was_grounded || !player_.IsGrounded() || !has_movement_input) {
        footstep_timer_seconds_ = 0.0;
        return;
    }

    footstep_timer_seconds_ -= GameClock::TICK_INTERVAL_SECONDS;
    if (footstep_timer_seconds_ > 0.0) {
        return;
    }

    AudioSystem::Play(SoundEffect::Footstep);
    footstep_timer_seconds_ = 0.35;
}

} // namespace igi
