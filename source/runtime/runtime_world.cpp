#include "runtime_world.h"
#include "audio_system.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace igi {

RuntimeWorld::RuntimeWorld()
    : ai_script_host_(qvm_registry_) {}
RuntimeWorld::~RuntimeWorld() = default;

void RuntimeWorld::Initialize(float (*get_terrain_z)(float x, float y), bool (*check_collision)(float x, float y, float z)) {
    get_terrain_z_ = get_terrain_z;
    check_collision_ = check_collision;
    if (check_collision_) {
        player_.SetCeilingQuery([this](const glm::vec3& body_position) {
            return FindWorldCeilingHeight(body_position);
        });
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
        player_.SetCeilingQuery({});
        ai_.SetMovementCollisionQuery({});
        ai_.SetLineOfSightQuery({});
    }
    Reset();
}

float RuntimeWorld::FindWorldCeilingHeight(const glm::vec3& body_position) const {
    if (!check_collision_) {
        return std::numeric_limits<float>::max();
    }

    // Only the standing envelope is needed for stance transitions. Keeping
    // this probe bounded prevents the per-tick ground query from scanning the
    // entire level while still detecting roofs that can block the player.
    constexpr float probe_start_offset = PlayerCollision::SkinWidthInUnits;
    constexpr float probe_limit = PlayerController::STANDING_EYE_HEIGHT +
        PlayerCollision::SkinWidthInUnits;
    constexpr float probe_step = 256.0f;
    constexpr int refinement_iterations = 12;

    const float lower_bound = body_position.z + probe_start_offset;
    const float upper_bound = body_position.z + probe_limit;
    const auto is_solid = [this, &body_position](float height) {
        return check_collision_(body_position.x, body_position.y, height);
    };

    if (is_solid(lower_bound)) {
        return lower_bound;
    }

    float previous_height = lower_bound;
    const int sample_count = std::max(
        1,
        static_cast<int>(std::ceil((upper_bound - lower_bound) / probe_step)));
    for (int sample_index = 1; sample_index <= sample_count; ++sample_index) {
        const float sample_fraction = static_cast<float>(sample_index) /
            static_cast<float>(sample_count);
        const float current_height = lower_bound +
            (upper_bound - lower_bound) * sample_fraction;
        if (!is_solid(current_height)) {
            previous_height = current_height;
            continue;
        }

        float lower_height = previous_height;
        float upper_height = current_height;
        for (int refinement_index = 0;
             refinement_index < refinement_iterations;
             ++refinement_index) {
            const float middle_height = (lower_height + upper_height) * 0.5f;
            if (is_solid(middle_height)) {
                upper_height = middle_height;
            } else {
                lower_height = middle_height;
            }
        }
        return upper_height;
    }

    return std::numeric_limits<float>::max();
}

void RuntimeWorld::Reset() {
    glm::vec3 spawn_pos(0.0f, 0.0f, 100.0f);
    if (get_terrain_z_) {
        spawn_pos.z = get_terrain_z_(spawn_pos.x, spawn_pos.y);
    }

    player_.Reset(spawn_pos, 0.0f);
    weapons_ = WeaponSystem();
    ai_.Clear();
    ClearGuardScripts();
    task_tree_.Clear();
    level_flow_.InitializeMission(1);
    guard_combat_states_.clear();
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
    if (input_cmd.switch_weapon >= 0 && input_cmd.switch_weapon <= 17) {
        weapons_.SelectWeaponSlot(static_cast<uint32_t>(input_cmd.switch_weapon));
    }
    weapons_.Update(dt, input_cmd.fire);
    if (input_cmd.fire) {
        std::vector<BulletTrace> traces;
        float yaw_rad = glm::radians(player_.GetYaw());
        float pitch_rad = glm::radians(player_.GetPitch());
        float sin_y = std::sin(yaw_rad);
        float cos_y = std::cos(yaw_rad);
        float sin_p = std::sin(pitch_rad);
        float cos_p = std::cos(pitch_rad);
        glm::vec3 aim_dir(-sin_y * cos_p, cos_y * cos_p, sin_p);

        if (weapons_.TryFire(player_.GetEyePosition(), aim_dir, traces)) {
            player_.SetOrientation(
                player_.GetYaw() + weapons_.GetLastRecoilYawDegrees(),
                std::clamp(
                    player_.GetPitch() + weapons_.GetLastRecoilPitchDegrees(),
                    -89.0f,
                    89.0f));
            bool hit_guard = false;
            bool hit_world_geometry = false;
            for (BulletTrace& trace : traces) {
                hit_guard = ApplyPlayerShotDamage(trace) || hit_guard;
                hit_world_geometry = trace.hit_world_geometry || hit_world_geometry;
            }
            AudioSystem::Play(SoundEffect::Gunshot);
            if (hit_guard || hit_world_geometry) {
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

bool RuntimeWorld::ApplyGuardShotDamage(BulletTrace& bullet_trace) {
    constexpr float player_hit_radius = 0.6f * PlayerController::WORLD_METER;
    const float direction_length = glm::length(bullet_trace.direction);
    if (direction_length <= 0.0001f) {
        return false;
    }

    const glm::vec3 shot_direction = bullet_trace.direction / direction_length;
    float world_impact_distance = bullet_trace.distance;
    if (FindWorldShotImpact(bullet_trace, world_impact_distance)) {
        bullet_trace.distance = world_impact_distance;
        bullet_trace.hit_position = bullet_trace.origin +
            shot_direction * world_impact_distance;
        bullet_trace.hit_world_geometry = true;
    }

    const glm::vec3 to_player = player_.GetEyePosition() - bullet_trace.origin;
    const float projected_distance = glm::dot(to_player, shot_direction);
    if (projected_distance < 0.0f || projected_distance > bullet_trace.distance) {
        return false;
    }

    const glm::vec3 closest_point = bullet_trace.origin +
        shot_direction * projected_distance;
    if (glm::distance(closest_point, player_.GetEyePosition()) > player_hit_radius) {
        return false;
    }

    bullet_trace.hit_entity_id = 0;
    bullet_trace.hit_position = closest_point;
    bullet_trace.distance = projected_distance;
    player_.ApplyDamage(bullet_trace.damage);
    return true;
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

        auto [combat_state_iterator, inserted] = guard_combat_states_.try_emplace(guard.id);
        GuardCombatState& combat_state = combat_state_iterator->second;
        if (inserted) {
            // Vanilla guards commonly use the M16 family in the supplied
            // missions. Keeping the weapon in the runtime state makes cadence,
            // spread, recoil metadata, and ammunition obey the same fixed-tick
            // path as the player weapon instead of using a damage shortcut.
            combat_state.weapon.SelectWeapon(4); // WEAPON_ID_M16A2
        }
        combat_state.weapon.Update(GameClock::TICK_INTERVAL_SECONDS, true);

        BulletTrace trace;
        const glm::vec3 aim_direction = player_.GetEyePosition() - guard_eye_position;
        if (!combat_state.weapon.TryFire(guard_eye_position, aim_direction, trace)) {
            continue;
        }

        const bool hit_player = ApplyGuardShotDamage(trace);
        AudioSystem::Play(SoundEffect::Gunshot);
        if (hit_player) {
            AudioSystem::Play(SoundEffect::Pain);
        }
        if (hit_player || trace.hit_world_geometry) {
            AudioSystem::Play(SoundEffect::BulletImpact);
        }

        AiStimulusEvent gunshot;
        gunshot.type = AiEventType::Gunshot;
        gunshot.position = guard.position;
        gunshot.loudness = 1.0f;
        gunshot.originator_id = guard.id;
        gunshot.tick_timestamp = tick_number;
        ai_.GetEventQueue().Post(gunshot);
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
