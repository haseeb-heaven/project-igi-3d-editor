// ai_system.h - Runtime AI guard simulation, dual-cone vision, and combat behavior
#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <vector>
#include <memory>
#include <string>
#include <cstdint>
#include <functional>
#include "ai_events.h"
#include "renderer/graph_writer.h"

namespace igi {

enum class AiGuardState {
    Idle,
    Patrol,
    Suspicious,
    Combat,
    Dead
};

struct AiVisionConeConfig {
    float patrol_sight_range = 30.0f * 4096.0f;  // 30 meters
    float alert_sight_range = 50.0f * 4096.0f;   // 50 meters
    float primary_fov_yaw = glm::radians(45.0f);
    float primary_fov_pitch = glm::radians(30.0f);
    float periph_sight_range = 28.0f * 4096.0f;  // 28 meters
    float periph_fov_yaw = glm::radians(85.0f);
    float periph_fov_pitch = glm::radians(45.0f);
};

enum class AiVisionResult {
    None = 0,
    Peripheral = 1,
    Primary = 2
};

// Patrol command kinds, matching OpenIGI AiPatrolCommand.cs.
enum class AiPatrolCommandKind {
    None = -1,
    Animation = 0,
    Delay = 1,
    WalkTo = 2,
    RunTo = 3,
    Crouch = 4,
    LookAtNode = 5,
    End = 6,
    Quit = 7,
    SetSpeed = 8
};

// One authored PatrolPathCommand: kind + operand (e.g. WalkTo(9), LookAtNode(34), Quit).
struct AiPatrolCommand {
    AiPatrolCommandKind kind = AiPatrolCommandKind::None;
    int operand = -1;
};

struct AiGuardEntity {
    uint32_t id = 0;
    std::string name;
    glm::vec3 position = glm::vec3(0.0f);
    float yaw = 0.0f;
    float pitch = 0.0f;
    float health = 100.0f;
    AiGuardState state = AiGuardState::Patrol;
    float suspicion = 0.0f; // [0.0 - 1.0]
    uint32_t current_waypoint = 0;
    std::vector<glm::vec3> waypoints;
    AiVisionConeConfig vision_config;

    // ---- OpenIGI patrol port (AiPatrolRoute cursor) ----
    std::vector<AiPatrolCommand> patrol_commands;
    int command_index = -1;         // NoCommand
    int loop_start_index = -1;
    int last_move_index = -1;
    int prev_move_index = -1;
    int end_index = -1;             // index of the End marker (once seen)
    bool walking = true;            // starts walking (SetSpeed / RunTo flips it)
    bool crouching = false;
    int deadline_tick = -1;         // NoDeadline; Delay sets tick + operand
    bool patrol_started = false;
    bool patrol_stopped = false;
    int requested_animation = -1;   // from Animation commands (ids into ai cache)
    bool animation_finished = true;

    // ---- Graph navigation state (AiSoldier.GoTo/Advance) ----
    std::shared_ptr<const GraphFile> graph;   // parsed nav graph (local node coords)
    glm::vec3 graph_offset = glm::vec3(0.0f); // AIGraph task world position
    int current_node = -1;          // node id guard is standing at
    int destination_node = -1;      // node id currently walking toward
    std::vector<int> route;         // remaining node ids (from enumeration)
    glm::vec3 leg_origin = glm::vec3(0.0f);   // where the current leg started

    // ---- Per-tick state ----
    uint64_t tick = 0;              // simulation tick counter (30 Hz)
    float yaw_clamp = glm::pi<float>();  // TurnTowards per-tick rotation limit
};

class AiSystem {
public:
    using MovementCollisionQuery = std::function<bool(const glm::vec3& position)>;
    using LineOfSightQuery = std::function<bool(
        const glm::vec3& line_origin,
        const glm::vec3& line_target)>;

    AiSystem();

    void Clear();
    void RegisterGuard(const AiGuardEntity& guard);
    AiGuardEntity* FindGuard(uint32_t guard_id);
    void SetMovementCollisionQuery(MovementCollisionQuery movement_collision_query);
    void SetLineOfSightQuery(LineOfSightQuery line_of_sight_query);

    // Fixed-step simulation tick
    void Update(double delta_seconds, const glm::vec3& player_pos, bool player_alive);

    // Perception
    AiVisionResult CheckVision(const AiGuardEntity& guard, const glm::vec3& target_pos, bool is_alerted) const;
    void ApplyDamage(uint32_t guard_id, float damage);

    AiEventQueue& GetEventQueue() { return event_queue_; }
    std::vector<AiGuardEntity>& GetGuards() { return guards_; }
    const std::vector<AiGuardEntity>& GetGuards() const { return guards_; }

private:
    std::vector<AiGuardEntity> guards_;
    AiEventQueue event_queue_;
    MovementCollisionQuery movement_collision_query_;
    LineOfSightQuery line_of_sight_query_;
    uint64_t tick_ = 0;  // 30 Hz simulation tick counter (monotonic across Update calls)
};

} // namespace igi
