#include "runtime_world.h"
#include "audio_system.h"
#include <algorithm>
#include <cmath>

namespace igi {

RuntimeWorld::RuntimeWorld()
    : ai_script_host_(qvm_registry_) {}
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
        ai_.SetLineOfSightQuery([this](
            const glm::vec3& line_origin,
            const glm::vec3& line_target) {
            return !IsWorldLineBlocked(line_origin, line_target);
        });
    } else {
        ai_.SetMovementCollisionQuery({});
        ai_.SetLineOfSightQuery({});
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
    ClearGuardScripts();
    task_tree_.Clear();
    level_flow_.InitializeMission(1);
    next_guard_attack_tick_.clear();
    footstep_timer_seconds_ = 0.0;
    extraction_zone_center_ = glm::vec3(1000.0f, 1000.0f, 0.0f);
    extraction_zone_radius_ = 8.0f * PlayerController::WORLD_METER;
}

bool RuntimeWorld::AttachGuardScript(
    uint32_t guard_id,
    const QVMFile& parsed_file,
    const std::string& source_path) {
    if (ai_.FindGuard(guard_id) == nullptr) {
        return false;
    }

    GuardScriptState script_state;
    script_state.source_path = source_path;
    if (!ai_script_host_.LoadProgram(parsed_file, script_state.program)) {
        script_state.last_error = ai_script_host_.GetLastError();
        script_state.faulted = true;
        guard_scripts_[guard_id] = std::move(script_state);
        return false;
    }

    guard_scripts_[guard_id] = std::move(script_state);
    return true;
}

bool RuntimeWorld::AttachGuardScriptFromFile(uint32_t guard_id, const std::string& path) {
    const QVMFile parsed_file = QVM_Parse(path);
    return AttachGuardScript(guard_id, parsed_file, path);
}

void RuntimeWorld::ClearGuardScripts() {
    guard_scripts_.clear();
    ai_script_host_.Reset();
}

bool RuntimeWorld::HasGuardScript(uint32_t guard_id) const {
    const auto script = guard_scripts_.find(guard_id);
    return script != guard_scripts_.end() && !script->second.faulted;
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
            if (hit_guard || trace.hit_world_geometry) {
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
    DispatchGuardScripts();
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

void RuntimeWorld::DispatchGuardScripts() {
    // Iterate in the same stable order in which guards were registered. The
    // script registry carries the current guard binding, so hash-map order
    // must never become part of the simulation result.
    for (AiGuardEntity& guard : ai_.GetGuards()) {
        const auto script_iterator = guard_scripts_.find(guard.id);
        if (script_iterator == guard_scripts_.end()) {
            continue;
        }
        GuardScriptState& script = script_iterator->second;
        if (script.faulted) {
            continue;
        }

        int32_t event_type = 4; // AIEVENT_IDLE
        if (!script.dispatched_create) {
            event_type = 0; // AIEVENT_CREATE
            script.dispatched_create = true;
        } else if (guard.state == AiGuardState::Dead) {
            event_type = 2; // AIEVENT_DEAD
        } else if (guard.state == AiGuardState::Combat) {
            event_type = 7; // AIEVENT_COMBAT
        } else if (guard.state == AiGuardState::Suspicious) {
            event_type = 5; // AIEVENT_ALERT
        }

        if (!ai_script_host_.Run(script.program, guard, event_type)) {
            script.faulted = true;
            script.last_error = ai_script_host_.GetLastError();
            continue;
        }

        ApplyScriptPatrolRoute(guard);
    }
}

void RuntimeWorld::ApplyScriptPatrolRoute(AiGuardEntity& guard) const {
    if (guard.script_patrol_path_id < 0
        || guard.script_patrol_path_id == guard.active_patrol_path_id) {
        return;
    }

    const auto route = guard.patrol_routes.find(guard.script_patrol_path_id);
    if (route == guard.patrol_routes.end()) {
        return;
    }

    guard.patrol_commands = route->second;
    guard.active_patrol_path_id = guard.script_patrol_path_id;
    guard.command_index = -1;
    guard.loop_start_index = -1;
    guard.last_move_index = -1;
    guard.prev_move_index = -1;
    guard.end_index = -1;
    guard.deadline_tick = -1;
    guard.patrol_started = false;
    guard.patrol_stopped = false;
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

    float world_impact_distance = bullet_trace.distance;
    if (FindWorldShotImpact(bullet_trace, world_impact_distance)) {
        closest_hit_distance = world_impact_distance;
        bullet_trace.distance = world_impact_distance;
        bullet_trace.hit_position = bullet_trace.origin + shot_direction * world_impact_distance;
        bullet_trace.hit_world_geometry = true;
    }

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

bool RuntimeWorld::FindWorldShotImpact(
    const BulletTrace& bullet_trace,
    float& impact_distance) const {
    if (!check_collision_ || bullet_trace.distance <= 0.0f) {
        return false;
    }

    constexpr int sample_count = 512;
    constexpr int refinement_iterations = 12;
    const float direction_length = glm::length(bullet_trace.direction);
    if (direction_length <= 0.0001f) {
        return false;
    }
    const glm::vec3 shot_direction = bullet_trace.direction / direction_length;

    const auto is_solid = [this, &bullet_trace, &shot_direction](float distance) {
        const glm::vec3 sample_position =
            bullet_trace.origin + shot_direction * distance;
        return check_collision_(sample_position.x, sample_position.y, sample_position.z);
    };

    float previous_distance = 0.0f;
    if (is_solid(previous_distance)) {
        impact_distance = 0.0f;
        return true;
    }

    for (int sample_index = 1; sample_index <= sample_count; ++sample_index) {
        const float sample_fraction = static_cast<float>(sample_index) /
            static_cast<float>(sample_count);
        const float current_distance = bullet_trace.distance * sample_fraction;
        if (!is_solid(current_distance)) {
            previous_distance = current_distance;
            continue;
        }

        float lower_distance = previous_distance;
        float upper_distance = current_distance;
        for (int refinement_index = 0;
             refinement_index < refinement_iterations;
             ++refinement_index) {
            const float middle_distance = (lower_distance + upper_distance) * 0.5f;
            if (is_solid(middle_distance)) {
                upper_distance = middle_distance;
            } else {
                lower_distance = middle_distance;
            }
        }
        impact_distance = upper_distance;
        return true;
    }

    return false;
}

bool RuntimeWorld::IsWorldLineBlocked(
    const glm::vec3& line_origin,
    const glm::vec3& line_target) const {
    const glm::vec3 line_delta = line_target - line_origin;
    const float line_length = glm::length(line_delta);
    if (line_length <= 0.0001f) {
        return false;
    }

    BulletTrace visibility_trace;
    visibility_trace.origin = line_origin;
    visibility_trace.direction = line_delta;
    visibility_trace.distance = line_length;

    float impact_distance = line_length;
    return FindWorldShotImpact(visibility_trace, impact_distance);
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

        const glm::vec3 guard_eye_position = guard.position + glm::vec3(
            0.0f,
            0.0f,
            1.7f * PlayerController::WORLD_METER);
        if (IsWorldLineBlocked(guard_eye_position, player_.GetEyePosition())) {
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
