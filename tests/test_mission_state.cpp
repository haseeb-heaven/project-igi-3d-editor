#include <gtest/gtest.h>

#include "../source/mission_state_loader.h"

TEST(MissionStateLoaderTest, LoadsOrientedAreaActivationDefinition) {
    igi::MissionStateTaskSource source;
    source.task_type = "AreaActivate";
    source.task_id = "200";
    source.argument_tokens = {
        "200", "AreaActivate", "", "100", "200", "300", "0", "0",
        "1.5707963", "400", "500", "600", "CRITERIA_HUMAN0",
    };

    const igi::AuthoredMissionStateDefinitions definitions =
        igi::LoadAuthoredMissionStateDefinitions({source});

    ASSERT_EQ(definitions.area_activations.size(), 1U);
    const igi::AuthoredMissionAreaActivation& area =
        definitions.area_activations.front();
    EXPECT_EQ(area.task_id, "200");
    EXPECT_EQ(area.position, glm::vec3(100.0f, 200.0f, 300.0f));
    EXPECT_EQ(area.dimensions, glm::vec3(400.0f, 500.0f, 600.0f));
    EXPECT_EQ(area.criteria, "CRITERIA_HUMAN0");
}

TEST(MissionStateLoaderTest, LoadsEditVariableExpressionsAndInitialValue) {
    igi::MissionStateTaskSource source;
    source.task_type = "EditVariable";
    source.task_id = "105";
    source.argument_tokens = {
        "105", "EditVariable", "", "0", "0", "0", "2",
        "EditVariable_105.nValue == 2", "EditVariable_105.nValue == 3",
    };

    const igi::AuthoredMissionStateDefinitions definitions =
        igi::LoadAuthoredMissionStateDefinitions({source});

    ASSERT_EQ(definitions.edit_variables.size(), 1U);
    const igi::AuthoredMissionEditVariable& edit =
        definitions.edit_variables.front();
    EXPECT_EQ(edit.task_id, "105");
    EXPECT_EQ(edit.initial_value, 2);
    EXPECT_EQ(edit.add_expression, "EditVariable_105.nValue == 2");
    EXPECT_EQ(edit.subtract_expression, "EditVariable_105.nValue == 3");
}

TEST(MissionStateLoaderTest, LoadsTimersAndStatusMessagePresentationFields) {
    igi::MissionStateTaskSource timer_source;
    timer_source.task_type = "LevelTimer";
    timer_source.task_id = "95";
    timer_source.argument_tokens = {
        "95", "LevelTimer", "", "0", "0", "0", "0", "0", "0",
        "Terminal_501.isHacked", "Terminal_501.isHackedThisTick", "FALSE",
    };

    igi::MissionStateTaskSource message_source;
    message_source.task_type = "StatusMessage";
    message_source.task_id = "1391";
    message_source.argument_tokens = {
        "1391", "StatusMessage", "", "0", "0", "0", "0", "0", "0",
        "HumanPlayer_0.isDead", "MISSION_FAILED", "", "fail", "TRUE", "FALSE", "2.0",
    };

    const igi::AuthoredMissionStateDefinitions definitions =
        igi::LoadAuthoredMissionStateDefinitions({timer_source, message_source});

    ASSERT_EQ(definitions.level_timers.size(), 1U);
    EXPECT_EQ(definitions.level_timers[0].task_id, "95");
    EXPECT_EQ(definitions.level_timers[0].on_expression, "Terminal_501.isHacked");
    EXPECT_FALSE(definitions.level_timers[0].initial_run);

    ASSERT_EQ(definitions.status_messages.size(), 1U);
    EXPECT_EQ(definitions.status_messages[0].task_id, "1391");
    EXPECT_EQ(definitions.status_messages[0].send_expression, "HumanPlayer_0.isDead");
    EXPECT_EQ(definitions.status_messages[0].text_resource, "MISSION_FAILED");
    EXPECT_EQ(definitions.status_messages[0].sound_name, "fail");
    EXPECT_TRUE(definitions.status_messages[0].send_once);
    EXPECT_FALSE(definitions.status_messages[0].cutscene_message);
    EXPECT_FLOAT_EQ(definitions.status_messages[0].duration_seconds, 2.0f);
}
