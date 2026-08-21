#include "runtime_world.h"
#include "audio_system.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

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
    projectiles_.SetCollisionQuery(
        [this](
            const glm::vec3& start_position,
            const glm::vec3& end_position,
            ProjectileCollisionHit& collision_hit) {
            return FindProjectileCollision(
                start_position,
                end_position,
                collision_hit);
        });
    projectiles_.SetProximityTriggerQuery(
        [this](const glm::vec3& center, float radius_units) {
            return IsProjectileTargetInRange(center, radius_units);
        });
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

bool RuntimeWorld::FindProjectileCollision(
    const glm::vec3& start_position,
    const glm::vec3& end_position,
    ProjectileCollisionHit& collision_hit) const {
    const glm::vec3 movement = end_position - start_position;
    if (glm::dot(movement, movement) <= 0.00000001f) {
        return false;
    }

    const auto normalize_surface_normal = [](const glm::vec3& normal) {
        const float length_squared = glm::dot(normal, normal);
        if (length_squared <= 0.00000001f) {
            return glm::vec3(0.0f, 0.0f, 1.0f);
        }
        return normal / std::sqrt(length_squared);
    };

    const auto is_solid = [this](const glm::vec3& sample_position) {
        if (get_terrain_z_ && sample_position.z <= get_terrain_z_(
                sample_position.x,
                sample_position.y)) {
            return true;
        }
        return check_collision_ && check_collision_(
            sample_position.x,
            sample_position.y,
            sample_position.z);
    };

    if (!is_solid(end_position)) {
        return false;
    }

    float lower_fraction = 0.0f;
    float upper_fraction = 1.0f;
    if (is_solid(start_position)) {
        upper_fraction = 0.0f;
    } else {
        for (int refinement_index = 0;
             refinement_index < 12;
             ++refinement_index) {
            const float middle_fraction =
                (lower_fraction + upper_fraction) * 0.5f;
            const glm::vec3 middle_position = start_position +
                movement * middle_fraction;
            if (is_solid(middle_position)) {
                upper_fraction = middle_fraction;
            } else {
                lower_fraction = middle_fraction;
            }
        }
    }

    collision_hit.position = start_position + movement * upper_fraction;

    // A terrain crossing has an actual height-field normal. Static model
    // collision only supplies a boolean in the existing editor callback, so
    // use the incoming horizontal direction as a conservative wall normal.
    bool crossed_terrain = false;
    if (get_terrain_z_) {
        const float start_terrain_height = get_terrain_z_(
            start_position.x,
            start_position.y);
        const float end_terrain_height = get_terrain_z_(
            end_position.x,
            end_position.y);
        crossed_terrain = start_position.z > start_terrain_height &&
            end_position.z <= end_terrain_height;
        if (crossed_terrain) {
            constexpr float terrain_probe_spacing = 64.0f;
            const float left_height = get_terrain_z_(
                collision_hit.position.x - terrain_probe_spacing,
                collision_hit.position.y);
            const float right_height = get_terrain_z_(
                collision_hit.position.x + terrain_probe_spacing,
                collision_hit.position.y);
            const float down_height = get_terrain_z_(
                collision_hit.position.x,
                collision_hit.position.y - terrain_probe_spacing);
            const float up_height = get_terrain_z_(
                collision_hit.position.x,
                collision_hit.position.y + terrain_probe_spacing);
            collision_hit.normal = normalize_surface_normal(
                glm::vec3(
                    -(right_height - left_height),
                    -(up_height - down_height),
                    terrain_probe_spacing * 2.0f));
        }
    }

    if (!crossed_terrain) {
        collision_hit.normal = normalize_surface_normal(
            glm::vec3(-movement.x, -movement.y, 0.0f));
        if (glm::dot(collision_hit.normal, collision_hit.normal) <= 0.00000001f) {
            collision_hit.normal = glm::vec3(0.0f, 0.0f, 1.0f);
        }
    }
    return true;
}

bool RuntimeWorld::IsProjectileTargetInRange(
    const glm::vec3& center,
    float radius_units) const {
    if (player_.IsAlive() && glm::distance(
            center,
            player_.GetPosition()) <= radius_units) {
        return true;
    }

    for (const AiGuardEntity& guard : ai_.GetGuards()) {
        if (guard.state != AiGuardState::Dead && glm::distance(
                center,
                guard.position) <= radius_units) {
            return true;
        }
    }
    return false;
}

void RuntimeWorld::LaunchPlayerProjectile(
    const glm::vec3& origin,
    const glm::vec3& direction,
    const WeaponDefinition& weapon) {
    const float direction_length = glm::length(direction);
    if (direction_length <= 0.0001f) {
        return;
    }

    ProjectileLaunch launch;
    launch.position = origin;
    launch.velocity = direction / direction_length * (
        weapon.muzzle_velocity * PlayerController::WORLD_METER / 30.0f);
    launch.type = weapon.projectile_type;
    launch.owner_entity_id = 0;
    launch.fuse_ticks = ProjectileSystem::ReferenceGrenadeFuseTicks;
    launch.damage = weapon.damage;
    launch.damage_factor = 1.0f;
    launch.explosion_radius_units = 5.0f * PlayerController::WORLD_METER;
    launch.explosion_falloff_units = 0.0f;

    switch (weapon.projectile_type) {
        case ProjectileType::ProximityMine:
            launch.fuse_ticks = 0;
            launch.proximity_trigger_radius_units =
                1.5f * PlayerController::WORLD_METER;
            break;
        case ProjectileType::Rocket:
            // OpenIGI's missile path is impact-driven and uses the fixed
            // 2.5-metre/1.75-metre blast profile. Steering remains a later
            // seam; the current launch is still a real moving projectile.
            launch.damage_factor = 5.0f;
            launch.explosion_radius_units = 2.5f * PlayerController::WORLD_METER;
            launch.explosion_falloff_units = 1.75f * PlayerController::WORLD_METER;
            launch.detonate_on_impact = true;
            launch.affected_by_gravity = false;
            break;
        case ProjectileType::FragGrenade:
        case ProjectileType::Flashbang:
        case ProjectileType::None:
            break;
    }

    projectiles_.Spawn(launch);
}

void RuntimeWorld::ApplyProjectileDetonations() {
    constexpr float target_collision_radius = 0.6f * PlayerController::WORLD_METER;
    constexpr float body_radius_scale = 0.33333334f;
    constexpr float flash_maximum_distance = 15.0f * PlayerController::WORLD_METER;
    constexpr float flash_minimum_duration_seconds = 0.75f;
    constexpr float flash_maximum_duration_seconds = 1.5f;

    const auto calculate_damage = [](const ProjectileDetonation& detonation,
                                     float target_distance) {
        const float effective_distance = target_distance -
            target_collision_radius * body_radius_scale -
            detonation.explosion_radius_units;
        if (effective_distance < 0.0f) {
            return detonation.damage * detonation.damage_factor;
        }
        if (effective_distance >= detonation.explosion_falloff_units ||
            detonation.explosion_falloff_units <= 0.0f) {
            return 0.0f;
        }
        const float dose = (detonation.explosion_falloff_units -
            effective_distance) / detonation.explosion_falloff_units;
        return detonation.damage * detonation.damage_factor * dose;
    };

    for (const ProjectileDetonation& detonation : projectiles_.GetDetonations()) {
        if (detonation.type == ProjectileType::Flashbang) {
            AudioSystem::Play(SoundEffect::Flashbang);
            if (player_.IsAlive()) {
                const float player_distance = glm::distance(
                    detonation.position,
                    player_.GetEyePosition());
                if (player_distance < flash_maximum_distance &&
                    !IsWorldLineBlocked(
                        detonation.position,
                        player_.GetEyePosition())) {
                    const float exposure = std::clamp(
                        1.0f - player_distance / flash_maximum_distance,
                        0.0f,
                        1.0f);
                    const float duration = flash_minimum_duration_seconds +
                        exposure * (flash_maximum_duration_seconds -
                            flash_minimum_duration_seconds);
                    flash_effect_strength_ = std::max(
                        flash_effect_strength_,
                        exposure);
                    flash_effect_decay_per_second_ = std::max(
                        flash_effect_decay_per_second_,
                        exposure / duration);
                    flash_effect_remaining_seconds_ = std::max(
                        flash_effect_remaining_seconds_,
                        duration);
                }
            }
            continue;
        }

        AudioSystem::Play(SoundEffect::Explosion);
        if (player_.IsAlive()) {
            const float player_distance = glm::distance(
                detonation.position,
                player_.GetPosition());
            if (player_distance <= detonation.explosion_radius_units +
                    detonation.explosion_falloff_units &&
                !IsWorldLineBlocked(
                    detonation.position,
                    player_.GetEyePosition())) {
                player_.ApplyDamage(calculate_damage(detonation, player_distance));
            }
        }

        for (const AiGuardEntity& guard : ai_.GetGuards()) {
            if (guard.state == AiGuardState::Dead ||
                guard.id == detonation.owner_entity_id) {
                continue;
            }
            const float guard_distance = glm::distance(
                detonation.position,
                guard.position);
            if (guard_distance > detonation.explosion_radius_units +
                    detonation.explosion_falloff_units ||
                IsWorldLineBlocked(
                    detonation.position,
                    guard.position + glm::vec3(
                        0.0f,
                        0.0f,
                        1.0f * PlayerController::WORLD_METER))) {
                continue;
            }
            ai_.ApplyDamage(
                guard.id,
                calculate_damage(detonation, guard_distance));
        }
    }
}

void RuntimeWorld::Reset() {
    glm::vec3 spawn_pos(0.0f, 0.0f, 100.0f);
    if (get_terrain_z_) {
        spawn_pos.z = get_terrain_z_(spawn_pos.x, spawn_pos.y);
    }

    player_.Reset(spawn_pos, 0.0f);
    player_.ApplyTuning(player_tuning_);
    weapons_ = WeaponSystem();
    weapons_.SetPlayerWeaponCycle(player_weapon_cycle_);
    weapons_.SelectWeaponSlot(0);
    ladder_placements_.clear();
    projectiles_.Clear();
    ai_.Clear();
    ClearGuardScripts();
    task_tree_.Clear();
    level_flow_.InitializeMission(1);
    guard_combat_states_.clear();
    fire_was_held_ = false;
    zoom_active_ = false;
    flash_effect_strength_ = 0.0f;
    flash_effect_decay_per_second_ = 0.0f;
    flash_effect_remaining_seconds_ = 0.0f;
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

void RuntimeWorld::SetPlayerTuning(const PlayerController::Tuning& tuning) {
    player_tuning_ = tuning;
    player_.ApplyTuning(player_tuning_);
}

void RuntimeWorld::SetPlayerWeaponCycle(const std::vector<uint32_t>& weapon_cycle) {
    player_weapon_cycle_ = weapon_cycle;
    weapons_.SetPlayerWeaponCycle(player_weapon_cycle_);
}

void RuntimeWorld::SetLadderPlacements(std::vector<LadderPlacement> ladder_placements) {
    ladder_placements_ = std::move(ladder_placements);
}

void RuntimeWorld::SetInteractionQuery(InteractionQuery interaction_query) {
    interaction_query_ = std::move(interaction_query);
}

void RuntimeWorld::UpdateSimulationTick(uint64_t tick_number, const PlayerInputCmd& input_cmd) {
    constexpr double dt = GameClock::TICK_INTERVAL_SECONDS;

    if (flash_effect_remaining_seconds_ > 0.0f) {
        flash_effect_remaining_seconds_ = std::max(
            0.0f,
            flash_effect_remaining_seconds_ - static_cast<float>(dt));
        flash_effect_strength_ = std::max(
            0.0f,
            flash_effect_strength_ - flash_effect_decay_per_second_ *
                static_cast<float>(dt));
        if (flash_effect_remaining_seconds_ <= 0.0f) {
            flash_effect_strength_ = 0.0f;
            flash_effect_decay_per_second_ = 0.0f;
        }
    }

    std::vector<ObstacleCollider> obstacles;
    zoom_active_ = input_cmd.zoom;
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
    const PlayerFallImpact& landing_impact = player_.GetLastLandingImpact();
    if (landing_impact.hearing_radius_units > 0.0f) {
        AiStimulusEvent ground_impact;
        ground_impact.type = AiEventType::GroundImpact;
        ground_impact.position = player_.GetPosition();
        ground_impact.hearing_radius_units = landing_impact.hearing_radius_units;
        ground_impact.originator_id = 0;
        ground_impact.tick_timestamp = tick_number;
        ai_.GetEventQueue().Post(ground_impact);
    }
    if (landing_impact.damage > 0.0f) {
        AudioSystem::PlayWeaponFire(landing_impact.sound_name, SoundEffect::Pain);
    }
    if (input_cmd.jump && was_grounded && !player_.IsGrounded()) {
        AudioSystem::Play(SoundEffect::Jump);
    }
    PlayFootstepIfNeeded(input_cmd, was_grounded);

    // 2. Weapon switching, firing & cooldowns
    if (input_cmd.switch_weapon >= 0 && input_cmd.switch_weapon <= 17) {
        weapons_.SelectWeaponSlot(static_cast<uint32_t>(input_cmd.switch_weapon));
    }
    weapons_.Update(dt, input_cmd.fire);
    const WeaponDefinition& active_weapon = weapons_.GetActiveWeapon();
    const bool is_projectile_weapon =
        active_weapon.projectile_type != ProjectileType::None;
    if (input_cmd.fire && (!is_projectile_weapon || !fire_was_held_)) {
        float yaw_rad = glm::radians(player_.GetYaw());
        float pitch_rad = glm::radians(player_.GetPitch());
        float sin_y = std::sin(yaw_rad);
        float cos_y = std::cos(yaw_rad);
        float sin_p = std::sin(pitch_rad);
        float cos_p = std::cos(pitch_rad);
        glm::vec3 aim_dir(-sin_y * cos_p, cos_y * cos_p, sin_p);

        const auto apply_recoil = [this]() {
            player_.SetOrientation(
                player_.GetYaw() + weapons_.GetLastRecoilYawDegrees(),
                std::clamp(
                    player_.GetPitch() + weapons_.GetLastRecoilPitchDegrees(),
                    -89.0f,
                    89.0f));
        };
        if (is_projectile_weapon) {
            BulletTrace fired_trace;
            if (weapons_.TryFire(
                    player_.GetEyePosition(),
                    aim_dir,
                    fired_trace)) {
                apply_recoil();
                LaunchPlayerProjectile(
                    player_.GetEyePosition(),
                    fired_trace.direction,
                    active_weapon);
                AudioSystem::PlayWeaponFire(
                    active_weapon.fire_sound,
                    SoundEffect::ProjectileLaunch);
            }
        } else {
            std::vector<BulletTrace> traces;
            if (weapons_.TryFire(
                    player_.GetEyePosition(),
                    aim_dir,
                    traces)) {
                apply_recoil();
                bool hit_guard = false;
                bool hit_world_geometry = false;
                for (BulletTrace& trace : traces) {
                    hit_guard = ApplyPlayerShotDamage(trace) || hit_guard;
                    hit_world_geometry = trace.hit_world_geometry || hit_world_geometry;
                }
                AudioSystem::PlayWeaponFire(
                    active_weapon.fire_sound,
                    SoundEffect::Gunshot);
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
    }
    fire_was_held_ = input_cmd.fire;

    if (input_cmd.reload) {
        weapons_.Reload();
        AudioSystem::Play(SoundEffect::Reload);
    }

    if (input_cmd.interact) {
        RuntimeInteractionResult interaction_result;
        if (interaction_query_) {
            const float yaw_radians = glm::radians(player_.GetYaw());
            const float pitch_radians = glm::radians(player_.GetPitch());
            const float cosine_pitch = std::cos(pitch_radians);
            interaction_result = interaction_query_(
                player_.GetEyePosition(),
                glm::vec3(
                    -std::sin(yaw_radians) * cosine_pitch,
                    std::cos(yaw_radians) * cosine_pitch,
                    std::sin(pitch_radians)));
        }

        const bool objective_completed = interaction_query_
            ? (interaction_result.completed_objective &&
                level_flow_.CompleteFirstPendingPrimaryObjective())
            : level_flow_.CompleteFirstPendingPrimaryObjective();
        if (objective_completed) {
            AudioSystem::Play(SoundEffect::ObjectiveComplete);
        }
    }

    // 3. Tick AI perception & state machine
    ai_.Update(dt, player_.GetPosition(), player_.IsAlive());
    DispatchGuardScripts();
    ApplyGuardCombatDamage(tick_number);
    projectiles_.Tick();
    ApplyProjectileDetonations();

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
            const bool selected_authored_weapon =
                !guard.weapon_script_id.empty() &&
                combat_state.weapon.SelectWeaponByScriptId(guard.weapon_script_id);
            if (!selected_authored_weapon) {
                combat_state.weapon.SelectWeapon(4); // WEAPON_ID_M16A2 fallback
            }
        }
        combat_state.weapon.Update(GameClock::TICK_INTERVAL_SECONDS, true);

        BulletTrace trace;
        const glm::vec3 aim_direction = player_.GetEyePosition() - guard_eye_position;
        if (!combat_state.weapon.TryFire(guard_eye_position, aim_direction, trace)) {
            continue;
        }

        const bool hit_player = ApplyGuardShotDamage(trace);
        AudioSystem::PlayWeaponFire(
            combat_state.weapon.GetActiveWeapon().fire_sound,
            SoundEffect::Gunshot);
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
