// runtime_world.cpp - Isolated simulation world and entity snapshot manager implementation
#include "runtime_world.h"

namespace igi {

RuntimeWorld::RuntimeWorld() = default;
RuntimeWorld::~RuntimeWorld() = default;

void RuntimeWorld::Initialize(float (*get_terrain_z)(float x, float y)) {
    get_terrain_z_ = get_terrain_z;
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

    // Create demo patrol guard
    AiGuardEntity guard;
    guard.id = 101;
    guard.name = "PatrolGuard1";
    guard.position = glm::vec3(200.0f, 300.0f, spawn_pos.z);
    guard.waypoints = {
        glm::vec3(200.0f, 300.0f, spawn_pos.z),
        glm::vec3(400.0f, 300.0f, spawn_pos.z),
        glm::vec3(400.0f, 500.0f, spawn_pos.z),
        glm::vec3(200.0f, 500.0f, spawn_pos.z)
    };
    ai_.RegisterGuard(guard);
}

void RuntimeWorld::UpdateSimulationTick(uint64_t tick_number, const PlayerInputCmd& input_cmd) {
    constexpr double dt = GameClock::TICK_INTERVAL_SECONDS;

    // 1. Tick player physics and movement
    player_.Tick(input_cmd, get_terrain_z_, glm::vec3(0.0f));

    // 2. Weapon firing & cooldowns
    weapons_.Update(dt);
    if (input_cmd.fire) {
        BulletTrace trace;
        float yaw_rad = glm::radians(player_.GetYaw());
        float pitch_rad = glm::radians(player_.GetPitch());
        glm::vec3 aim_dir(
            std::sin(yaw_rad) * std::cos(pitch_rad),
            std::cos(yaw_rad) * std::cos(pitch_rad),
            std::sin(pitch_rad)
        );

        if (weapons_.TryFire(player_.GetEyePosition(), aim_dir, trace)) {
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
    }

    // 3. Tick AI perception & state machine
    ai_.Update(dt, player_.GetPosition(), player_.IsAlive());

    // 4. Tick runtime task tree
    task_tree_.Update(dt);

    // 5. Evaluate mission flow & objectives
    bool in_extraction = (glm::distance(player_.GetPosition(), glm::vec3(1000.0f, 1000.0f, player_.GetPosition().z)) < 150.0f);
    level_flow_.Update(player_.IsAlive(), in_extraction);
}

} // namespace igi
