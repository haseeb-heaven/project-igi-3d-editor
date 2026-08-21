#include <gtest/gtest.h>

#include "../source/level_flow.h"
#include "../source/mission_objective_loader.h"

#include <initializer_list>
#include <string>
#include <vector>

namespace {

igi::MissionObjectiveTaskSource MakeObjectiveTask(
    std::initializer_list<std::string> arguments) {
    igi::MissionObjectiveTaskSource source;
    source.task_type = "DefineComputerObjective";
    source.argument_tokens.assign(arguments.begin(), arguments.end());
    return source;
}

} // namespace

TEST(MissionObjectiveLoaderTest, ReadsAuthoredRowsAndPreservesExpressions) {
    const auto definitions = igi::LoadAuthoredMissionObjectiveDefinitions({
        MakeObjectiveTask({
            "104", "\"DefineComputerObjective\"", "\"\"", "\"\"",
            "\"M1_OBJ1\"", "1120", "\"EditVariable_105.nValue == 1\"", "\"\"",
            "\"M1_OBJ2\"", "1121", "\"Terminal_501.isHacked\"", "\"HumanPlayer_0.isDead\"",
            "\"\"", "-1", "\"\"", "\"\"",
        }),
    });

    ASSERT_EQ(definitions.size(), 1U);
    ASSERT_EQ(definitions[0].objectives.size(), 2U);
    EXPECT_EQ(definitions[0].valid_expression, "");
    EXPECT_EQ(definitions[0].objectives[0].text_resource, "M1_OBJ1");
    EXPECT_EQ(definitions[0].objectives[0].link_task_id, 1120);
    EXPECT_EQ(
        definitions[0].objectives[0].completion_expression,
        "EditVariable_105.nValue == 1");
    EXPECT_EQ(definitions[0].objectives[1].failure_expression, "HumanPlayer_0.isDead");
}

TEST(MissionObjectiveLoaderTest, IgnoresUnsupportedTaskTypesAndEmptyRows) {
    igi::MissionObjectiveTaskSource unsupported_task;
    unsupported_task.task_type = "StatusMessage";
    unsupported_task.argument_tokens = {"1", "\"StatusMessage\"", "\"text\""};

    const auto definitions = igi::LoadAuthoredMissionObjectiveDefinitions({
        unsupported_task,
        MakeObjectiveTask({
            "200", "\"DefineComputerObjective\"", "\"\"", "\"condition\"",
            "\"\"", "-1", "\"\"", "\"\"",
            "\"M2_OBJ1\"", "1200", "\"done\"", "\"failed\"",
        }),
    });

    ASSERT_EQ(definitions.size(), 1U);
    ASSERT_EQ(definitions[0].objectives.size(), 1U);
    EXPECT_EQ(definitions[0].valid_expression, "condition");
    EXPECT_EQ(definitions[0].objectives[0].text_resource, "M2_OBJ1");
}

TEST(LevelFlowTest, UsesAuthoredTextAndAdvancesAuthoredDefinition) {
    const std::vector<igi::AuthoredMissionObjectiveDefinition> first_definition = {
        {"M1_OBJ1", 1120, "complete", "failed"},
    };
    const std::vector<igi::AuthoredMissionObjectiveDefinition> second_definition = {
        {"M1_OBJ2", 1121, "complete-2", "failed-2"},
    };

    igi::AuthoredMissionObjectiveSet first_set;
    first_set.objectives = first_definition;
    igi::AuthoredMissionObjectiveSet second_set;
    second_set.objectives = second_definition;

    igi::LevelFlow flow;
    flow.InitializeMission(
        1,
        {first_set, second_set},
        [](const std::string& resource_key) {
            return resource_key == "M1_OBJ1"
                ? std::string("Steal the truck near the main entrance.")
                : std::string("Reach the extraction point.");
        });

    ASSERT_EQ(flow.GetObjectives().size(), 1U);
    EXPECT_EQ(flow.GetObjectiveDisplayText(), "Steal the truck near the main entrance.");
    EXPECT_TRUE(flow.CompleteFirstPendingPrimaryObjective());
    ASSERT_EQ(flow.GetObjectives().size(), 1U);
    EXPECT_EQ(flow.GetObjectiveDisplayText(), "Reach the extraction point.");
    EXPECT_EQ(flow.GetObjectives()[0].text_resource, "M1_OBJ2");
}
