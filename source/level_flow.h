// level_flow.h - Mission objective evaluation, flow state, and extraction logic
#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <functional>

namespace igi {

enum class ObjectiveState {
    Pending,
    Completed,
    Failed,
    Cancelled
};

struct MissionObjective {
    uint32_t id = 0;
    std::string description;
    std::string text_resource;
    int32_t link_task_id = -1;
    std::string completion_expression;
    std::string failure_expression;
    bool is_primary = true;
    ObjectiveState state = ObjectiveState::Pending;
};

struct AuthoredMissionObjectiveDefinition {
    std::string text_resource;
    int32_t link_task_id = -1;
    std::string completion_expression;
    std::string failure_expression;
};

struct AuthoredMissionObjectiveSet {
    std::string valid_expression;
    std::vector<AuthoredMissionObjectiveDefinition> objectives;
};

using MissionObjectiveTextResolver = std::function<std::string(const std::string&)>;
using MissionExpressionEvaluator = std::function<bool(const std::string&)>;

enum class MissionStatus {
    InProgress,
    Success,
    Failed
};

class LevelFlow {
public:
    LevelFlow();

    void InitializeMission(uint32_t mission_number);
    void InitializeMission(
        uint32_t mission_number,
        const std::vector<AuthoredMissionObjectiveSet>& authored_objective_sets,
        MissionObjectiveTextResolver text_resolver = {});
    void AddObjective(uint32_t id, const std::string& desc, bool is_primary = true);
    void SetObjectiveState(uint32_t id, ObjectiveState state);
    bool CompleteFirstPendingPrimaryObjective();

    void Update(
        bool player_alive,
        bool in_extraction_zone,
        MissionExpressionEvaluator expression_evaluator = {});

    MissionStatus GetStatus() const { return status_; }
    const std::vector<MissionObjective>& GetObjectives() const { return objectives_; }
    uint32_t GetMissionNumber() const { return mission_number_; }
    std::string GetObjectiveDisplayText() const;

private:
    void InitializeFallbackObjectives(uint32_t mission_number);
    void LoadAuthoredObjectiveSet(size_t authored_set_index);
    bool AdvanceAuthoredObjectiveSet();
    void EvaluateAuthoredObjectiveExpressions(
        const MissionExpressionEvaluator& expression_evaluator);

    uint32_t mission_number_ = 1;
    std::vector<MissionObjective> objectives_;
    MissionStatus status_ = MissionStatus::InProgress;
    std::vector<AuthoredMissionObjectiveSet> authored_objective_sets_;
    MissionObjectiveTextResolver objective_text_resolver_;
    size_t active_authored_objective_set_index_ = 0;
};

} // namespace igi
