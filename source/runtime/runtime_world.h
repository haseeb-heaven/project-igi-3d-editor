// runtime_world.h - Isolated simulation world and entity snapshot manager
#pragma once

#include <glm/glm.hpp>
#include <array>
#include <cstddef>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>
#include <string>
#include "../game_clock.h"
#include "../player_controller.h"
#include "../player_ladder.h"
#include "../weapon_system.h"
#include "../ai_system.h"
#include "../ai_script_host.h"
#include "../level_flow.h"
#include "../level/task_tree.h"
#include "../level/qvm_interpreter.h"
#include "projectile_system.h"
#include "door_state.h"
#include "../mission_expression.h"
#include "../mission_state.h"
#include "../weapon_view_sway.h"
#include "../weapon_view_recoil.h"

namespace igi {

struct RuntimeInteractionResult {
    bool handled = false;
    bool completed_objective = false;
};

// Immutable presentation snapshot for the currently running authored
// CutScene. RuntimeWorld computes it on the fixed-step boundary; App consumes
// the basis without reading mutable mission containers.
struct RuntimeCutSceneCamera {
    bool active = false;
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 forward = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 right = glm::vec3(1.0f, 0.0f, 0.0f);
    glm::vec3 up = glm::vec3(0.0f, 0.0f, 1.0f);
    float field_of_view_y_radians = 1.0f;
    float viewport_height_factor = 1.0f;
    float time_of_day = -1.0f;
    int shot_index = -1;
};

struct RuntimeMissionSoundEvent {
    std::string task_id;
    std::string sound_name;
    glm::vec3 position = glm::vec3(0.0f);
    bool playing = false;
    bool simple = false;
    bool relative_to_microphone = false;
};

struct RuntimeExplodeObjectSnapshot {
    int object_index = -1;
    std::string task_id;
    std::string destroyed_model_name;
    bool is_exploded = false;
};

class RuntimeWorld {
public:
    RuntimeWorld();
    ~RuntimeWorld();

    // Lifecycle
    void Initialize(float (*get_terrain_z)(float x, float y), bool (*check_collision)(float x, float y, float z) = nullptr);
    void Reset();
    void SetPlayerTuning(const PlayerController::Tuning& tuning);
    void SetPlayerWeaponCycle(const std::vector<uint32_t>& weapon_cycle);
    void SetLadderPlacements(std::vector<LadderPlacement> ladder_placements);
    void SetExtractionZone(const glm::vec3& center, float radius);
    void SetMissionStateBoolean(const std::string& variable_name, bool value);
    void SetMissionStateNumber(const std::string& variable_name, double value);
    void SetMissionStatePulse(const std::string& variable_name);
    using MissionSoundEventHandler = std::function<void(
        const RuntimeMissionSoundEvent& sound_event)>;
    void SetMissionSoundEventHandler(MissionSoundEventHandler event_handler) {
        mission_sound_event_handler_ = std::move(event_handler);
    }
    void SetAuthoredDoors(std::vector<RuntimeDoorDefinition> door_definitions);
    bool ToggleDoor(int object_index);
    const std::vector<RuntimeDoorSnapshot>& GetDoorSnapshots() const {
        return authored_door_snapshots_;
    }
    bool IsDoorFullyOpen(int object_index) const;
    void SetAuthoredMissionState(
        std::vector<AuthoredMissionAreaActivation> area_activations,
        std::vector<AuthoredMissionEditVariable> edit_variables,
        std::vector<AuthoredMissionLevelTimer> level_timers = {},
        std::vector<AuthoredMissionStatusMessage> status_messages = {},
        std::vector<AuthoredMissionCutScene> cut_scenes = {},
        std::vector<AuthoredMissionConditionalSound> conditional_sounds = {},
        std::vector<AuthoredMissionExplodeObject> explode_objects = {});
    const std::vector<RuntimeExplodeObjectSnapshot>&
    GetExplodeObjectSnapshots() const {
        return authored_explode_object_snapshots_;
    }
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

    const WeaponViewSway& GetWeaponViewSway() const {
        return weapon_view_sway_;
    }
    const WeaponViewRecoil& GetWeaponViewRecoil() const {
        return weapon_view_recoil_;
    }
    bool IsWeaponViewTransitioning() const {
        return weapon_selection_phase_ != WeaponSelectionPhase::Ready;
    }

    ProjectileSystem& GetProjectiles() { return projectiles_; }
    const ProjectileSystem& GetProjectiles() const { return projectiles_; }

    const std::vector<LadderPlacement>& GetLadderPlacements() const {
        return ladder_placements_;
    }

    AiSystem& GetAi() { return ai_; }
    const AiSystem& GetAi() const { return ai_; }

    LevelFlow& GetLevelFlow() { return level_flow_; }
    const LevelFlow& GetLevelFlow() const { return level_flow_; }

    TaskTree& GetTaskTree() { return task_tree_; }
    const TaskTree& GetTaskTree() const { return task_tree_; }

    QvmNativeRegistry& GetNativeRegistry() { return qvm_registry_; }

    // Current first-person flash exposure, normalized to [0, 1]. The renderer
    // consumes this value without coupling presentation to projectile state.
    float GetFlashEffectStrength() const { return flash_effect_strength_; }
    // Fixed-step first-person firearm cue, normalized to [0, 1]. This is a
    // presentation signal only; projectile and hit simulation remain separate.
    float GetMuzzleFlashStrength() const { return muzzle_flash_strength_; }
    bool IsZoomActive() const { return zoom_active_; }
    bool IsPlayerOnLadder() const { return ladder_traversal_.IsOnLadder(); }
    const LadderTraversal& GetLadderTraversal() const { return ladder_traversal_; }

    bool IsMissionActive() const { return level_flow_.GetStatus() == MissionStatus::InProgress; }
    const RuntimeCutSceneCamera& GetActiveCutSceneCamera() const {
        return active_cut_scene_camera_;
    }
    const std::vector<MissionStatusMessageDisplay>&
    GetDisplayedMissionStatusMessages() const {
        return displayed_mission_status_messages_;
    }

private:
    struct AuthoredExplodeObjectRuntime;
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
    bool UpdateWeaponSelection(const PlayerInputCmd& input_command);
    void UpdateAuthoredMissionState();
    void UpdateAuthoredCutScenes();
    void UpdateAuthoredConditionalSounds();
    void UpdateAuthoredExplodeObjects();
    void TriggerAuthoredExplodeObject(
        struct AuthoredExplodeObjectRuntime& runtime_object);
    void UpdateAuthoredCutSceneCamera(
        const AuthoredMissionCutScene& definition,
        int tick_count);
    void PublishAuthoredCutSceneState(
        const AuthoredMissionCutScene& definition,
        bool is_running,
        bool is_finished,
        int tick_count);
    void UpdateAuthoredDoors();
    void PublishAuthoredDoorState(
        const RuntimeDoorDefinition& definition,
        const RuntimeDoorState& door_state,
        bool is_locked,
        bool is_picked);
    void RefreshAuthoredDoorSnapshots();
    void UpdateAuthoredMissionStatusMessages(uint64_t tick_number);
    void PublishMissionStatusMessageState(
        const AuthoredMissionStatusMessage& definition,
        bool is_sent,
        int64_t sent_tick,
        bool is_finished_display,
        int64_t finished_display_tick,
        int64_t ticks_since_finished_display);
    void UpdateMissionActorState();
    bool TryMountNearestLadder();
    bool TickLadderTraversal(const PlayerInputCmd& input_command);
    void EndLadderTraversal();

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
    PlayerController::Tuning player_tuning_;
    std::vector<uint32_t> player_weapon_cycle_;
    std::vector<LadderPlacement> ladder_placements_;
    LadderTraversal ladder_traversal_;
    int active_ladder_index_ = -1;
    glm::vec3 ladder_slide_velocity_ = glm::vec3(0.0f);
    WeaponSystem weapons_;
    ProjectileSystem projectiles_;
    AiSystem ai_;
    LevelFlow level_flow_;
    TaskTree task_tree_;
    QvmNativeRegistry qvm_registry_;
    MissionExpressionState mission_expression_state_;
    struct MissionAreaActivationState {
        std::string task_id;
        glm::vec3 minimum = glm::vec3(0.0f);
        glm::vec3 maximum = glm::vec3(0.0f);
        bool accepts_player = false;
    };
    std::vector<MissionAreaActivationState> mission_area_activations_;
    std::vector<AuthoredMissionEditVariable> mission_edit_variables_;
    std::vector<int> mission_edit_variable_values_;
    std::vector<AuthoredMissionLevelTimer> mission_level_timers_;
    std::vector<int> mission_level_timer_ticks_;
    std::vector<uint8_t> mission_level_timer_running_;
    std::vector<AuthoredMissionCutScene> mission_cut_scenes_;
    std::vector<int> mission_cut_scene_ticks_;
    std::vector<uint8_t> mission_cut_scene_running_;
    std::vector<uint8_t> mission_cut_scene_finished_;
    RuntimeCutSceneCamera active_cut_scene_camera_;
    struct AuthoredConditionalSoundRuntime {
        AuthoredMissionConditionalSound definition;
        bool is_running = false;
        bool has_played = false;
    };
    std::vector<AuthoredConditionalSoundRuntime> mission_conditional_sounds_;
    MissionSoundEventHandler mission_sound_event_handler_;
    struct AuthoredExplodeObjectRuntime {
        AuthoredMissionExplodeObject definition;
        bool condition_active = false;
        bool delay_pending = false;
        int delay_ticks_remaining = 0;
        bool is_exploded = false;
    };
    std::vector<AuthoredExplodeObjectRuntime> mission_explode_objects_;
    std::vector<RuntimeExplodeObjectSnapshot>
        authored_explode_object_snapshots_;
    struct AuthoredDoorRuntime {
        RuntimeDoorDefinition definition;
        RuntimeDoorState state;
        bool is_locked = false;
        bool is_picked = false;
    };
    std::vector<AuthoredDoorRuntime> authored_doors_;
    std::vector<RuntimeDoorSnapshot> authored_door_snapshots_;
    struct MissionStatusMessageRuntime {
        AuthoredMissionStatusMessage definition;
        bool is_sent = false;
        int64_t sent_tick = -100000;
        bool is_finished_display = false;
        int64_t finished_display_tick = -1;
        int64_t ticks_since_finished_display = 0;
        bool is_displaying = false;
        int display_frame = 0;
        int characters_remaining = 0;
        int hold_ticks = 0;
        int slot_index = -1;
    };
    static constexpr size_t kMissionStatusMessageSlotCount = 24;
    std::vector<MissionStatusMessageRuntime> mission_status_messages_;
    std::array<int, kMissionStatusMessageSlotCount> mission_status_message_slots_{};
    std::vector<MissionStatusMessageDisplay> displayed_mission_status_messages_;
    std::vector<std::string> mission_state_pulse_names_;
    AiScriptHost ai_script_host_;
    std::unordered_map<uint32_t, GuardScriptState> guard_scripts_;
    struct GuardCombatState {
        WeaponSystem weapon;
    };

    std::unordered_map<uint32_t, GuardCombatState> guard_combat_states_;
    enum class WeaponSelectionPhase {
        Ready,
        Lowering,
        Raising,
    };
    WeaponViewSway weapon_view_sway_;
    WeaponViewRecoil weapon_view_recoil_;
    WeaponSelectionPhase weapon_selection_phase_ = WeaponSelectionPhase::Ready;
    int pending_weapon_slot_ = -1;
    InteractionQuery interaction_query_;
    bool fire_was_held_ = false;
    bool zoom_active_ = false;
    float flash_effect_strength_ = 0.0f;
    float flash_effect_decay_per_second_ = 0.0f;
    float flash_effect_remaining_seconds_ = 0.0f;
    float muzzle_flash_strength_ = 0.0f;
    double footstep_timer_seconds_ = 0.0;
    glm::vec3 extraction_zone_center_ = glm::vec3(1000.0f, 1000.0f, 0.0f);
    float extraction_zone_radius_ = 8.0f * PlayerController::WORLD_METER;
};

} // namespace igi
