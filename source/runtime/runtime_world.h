// runtime_world.h - Isolated simulation world and entity snapshot manager
#pragma once

#include <glm/glm.hpp>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>
#include <string>
#include "../game_clock.h"
#include "../player_controller.h"
#include "../weapon_system.h"
#include "../ai_system.h"
#include "../ai_script_host.h"
#include "../level_flow.h"
#include "../level/task_tree.h"
#include "../level/qvm_interpreter.h"
#include "projectile_system.h"

namespace igi {

struct RuntimeInteractionResult {
    bool handled = false;
    bool completed_objective = false;
};

class RuntimeWorld {
public:
    RuntimeWorld();
    ~RuntimeWorld();

    // Lifecycle
    void Initialize(float (*get_terrain_z)(float x, float y), bool (*check_collision)(float x, float y, float z) = nullptr);
    void Reset();
    void SetExtractionZone(const glm::vec3& center, float radius);
    using InteractionQuery = std::function<RuntimeInteractionResult(
        const glm::vec3& interaction_origin,
        const glm::vec3& interaction_direction)>;
    void SetInteractionQuery(InteractionQuery interaction_query);

    // Binds a parsed retail mission AI program to an already registered guard.
    bool AttachGuardScript(
        uint32_t guard_id,
        const QVMFile& parsed_file,
        const std::string& source_path = {});
    bool AttachGuardScriptFromFile(uint32_t guard_id, const std::string& path);
    void ClearGuardScripts();
    bool HasGuardScript(uint32_t guard_id) const;

    // Simulation tick
    void UpdateSimulationTick(uint64_t tick_number, const PlayerInputCmd& input_cmd);

    // Subsystems
    PlayerController& GetPlayer() { return player_; }
    const PlayerController& GetPlayer() const { return player_; }

    WeaponSystem& GetWeapons() { return weapons_; }
    const WeaponSystem& GetWeapons() const { return weapons_; }

    ProjectileSystem& GetProjectiles() { return projectiles_; }
    const ProjectileSystem& GetProjectiles() const { return projectiles_; }

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
    bool ApplyGuardShotDamage(BulletTrace& bullet_trace);
    bool FindWorldShotImpact(const BulletTrace& bullet_trace, float& impact_distance) const;
    bool IsWorldLineBlocked(const glm::vec3& line_origin, const glm::vec3& line_target) const;
    float FindWorldCeilingHeight(const glm::vec3& body_position) const;
    bool FindProjectileCollision(
        const glm::vec3& start_position,
        const glm::vec3& end_position,
        ProjectileCollisionHit& collision_hit) const;
    bool IsProjectileTargetInRange(
        const glm::vec3& center,
        float radius_units) const;
    void ApplyProjectileDetonations();
    void LaunchPlayerProjectile(
        const glm::vec3& origin,
        const glm::vec3& direction,
        const WeaponDefinition& weapon);
    void DispatchGuardScripts();
    void ApplyScriptPatrolRoute(AiGuardEntity& guard) const;
    void ApplyGuardCombatDamage(uint64_t tick_number);
    void PlayFootstepIfNeeded(const PlayerInputCmd& input_command, bool was_grounded);

    struct GuardScriptState {
        QvmProgram program;
        std::string source_path;
        std::string last_error;
        bool dispatched_create = false;
        bool faulted = false;
    };

    float (*get_terrain_z_)(float x, float y) = nullptr;
    bool (*check_collision_)(float x, float y, float z) = nullptr;

    PlayerController player_;
    WeaponSystem weapons_;
    ProjectileSystem projectiles_;
    AiSystem ai_;
    LevelFlow level_flow_;
    TaskTree task_tree_;
    QvmNativeRegistry qvm_registry_;
    AiScriptHost ai_script_host_;
    std::unordered_map<uint32_t, GuardScriptState> guard_scripts_;
    struct GuardCombatState {
        WeaponSystem weapon;
    };

    std::unordered_map<uint32_t, GuardCombatState> guard_combat_states_;
    InteractionQuery interaction_query_;
    bool fire_was_held_ = false;
    double footstep_timer_seconds_ = 0.0;
    glm::vec3 extraction_zone_center_ = glm::vec3(1000.0f, 1000.0f, 0.0f);
    float extraction_zone_radius_ = 8.0f * PlayerController::WORLD_METER;
};

} // namespace igi
