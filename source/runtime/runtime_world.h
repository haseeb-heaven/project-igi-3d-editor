// runtime_world.h - Isolated simulation world and entity snapshot manager
#pragma once

#include <glm/glm.hpp>
#include <memory>
#include <unordered_map>
#include <vector>
#include <string>
#include "../game_clock.h"
#include "../player_controller.h"
#include "../weapon_system.h"
#include "../ai_system.h"
#include "../level_flow.h"
#include "../level/task_tree.h"
#include "../level/qvm_interpreter.h"

namespace igi {

class RuntimeWorld {
public:
    RuntimeWorld();
    ~RuntimeWorld();

    // Lifecycle
    void Initialize(float (*get_terrain_z)(float x, float y), bool (*check_collision)(float x, float y, float z) = nullptr);
    void Reset();
    void SetExtractionZone(const glm::vec3& center, float radius);

    // Simulation tick
    void UpdateSimulationTick(uint64_t tick_number, const PlayerInputCmd& input_cmd);

    // Subsystems
    PlayerController& GetPlayer() { return player_; }
    const PlayerController& GetPlayer() const { return player_; }

    WeaponSystem& GetWeapons() { return weapons_; }
    const WeaponSystem& GetWeapons() const { return weapons_; }

    AiSystem& GetAi() { return ai_; }
    const AiSystem& GetAi() const { return ai_; }

    LevelFlow& GetLevelFlow() { return level_flow_; }
    const LevelFlow& GetLevelFlow() const { return level_flow_; }

    TaskTree& GetTaskTree() { return task_tree_; }
    const TaskTree& GetTaskTree() const { return task_tree_; }

    QvmNativeRegistry& GetNativeRegistry() { return qvm_registry_; }

    bool IsMissionActive() const { return level_flow_.GetStatus() == MissionStatus::InProgress; }

private:
    bool ApplyPlayerShotDamage(BulletTrace& bullet_trace);
    bool FindWorldShotImpact(const BulletTrace& bullet_trace, float& impact_distance) const;
    void ApplyGuardCombatDamage(uint64_t tick_number);
    void PlayFootstepIfNeeded(const PlayerInputCmd& input_command, bool was_grounded);

    float (*get_terrain_z_)(float x, float y) = nullptr;
    bool (*check_collision_)(float x, float y, float z) = nullptr;

    PlayerController player_;
    WeaponSystem weapons_;
    AiSystem ai_;
    LevelFlow level_flow_;
    TaskTree task_tree_;
    QvmNativeRegistry qvm_registry_;
    std::unordered_map<uint32_t, uint64_t> next_guard_attack_tick_;
    double footstep_timer_seconds_ = 0.0;
    glm::vec3 extraction_zone_center_ = glm::vec3(1000.0f, 1000.0f, 0.0f);
    float extraction_zone_radius_ = 8.0f * PlayerController::WORLD_METER;
};

} // namespace igi
