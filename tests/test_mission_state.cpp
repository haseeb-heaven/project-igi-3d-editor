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

TEST(MissionStateLoaderTest, LoadsCutSceneExpressionsAndAuthoredDuration) {
    igi::MissionStateTaskSource source;
    source.task_type = "CutScene";
    source.task_id = "1204";
    source.authored_duration_seconds = 15.0f;
    source.authored_camera_shots.push_back({
        glm::vec3(1.0f, 2.0f, 3.0f),
        glm::vec3(0.1f, 0.2f, 0.3f),
        1.2f,
        4.0f,
        true,
    });
    source.argument_tokens = {
        "1204", "CutScene", "", "0", "0", "0", "0", "0", "0",
        "!CutScene_1204.isFinished", "", "", "0", "FALSE", "0.7",
        "0.8", "0.25", "0.5", "0", "", "",
    };

    const igi::AuthoredMissionStateDefinitions definitions =
        igi::LoadAuthoredMissionStateDefinitions({source});

    ASSERT_EQ(definitions.cut_scenes.size(), 1U);
    const igi::AuthoredMissionCutScene& cut_scene = definitions.cut_scenes.front();
    EXPECT_EQ(cut_scene.task_id, "1204");
    EXPECT_EQ(cut_scene.run_expression, "!CutScene_1204.isFinished");
    EXPECT_FALSE(cut_scene.initial_run);
    EXPECT_FLOAT_EQ(cut_scene.time_scale, 0.7f);
    EXPECT_FLOAT_EQ(cut_scene.duration_seconds, 15.0f);
    EXPECT_FLOAT_EQ(cut_scene.viewport_height_factor, 0.8f);
    ASSERT_EQ(cut_scene.camera_shots.size(), 1U);
    EXPECT_EQ(
        cut_scene.camera_shots[0].position,
        glm::vec3(1.0f, 2.0f, 3.0f));
    EXPECT_TRUE(cut_scene.camera_shots[0].smooth_to_next);
}

TEST(MissionStateLoaderTest, RejectsNonFiniteCutSceneTiming) {
    igi::MissionStateTaskSource source;
    source.task_type = "CutScene";
    source.task_id = "1204";
    source.authored_duration_seconds = 1.0f;
    source.argument_tokens = {
        "1204", "CutScene", "", "0", "0", "0", "0", "0", "0",
        "", "", "", "nan", "FALSE", "1.0", "0", "0", "0", "", "", "",
    };

    const igi::AuthoredMissionStateDefinitions definitions =
        igi::LoadAuthoredMissionStateDefinitions({source});

    EXPECT_TRUE(definitions.cut_scenes.empty());
}
