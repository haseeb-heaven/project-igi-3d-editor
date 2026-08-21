#include <gtest/gtest.h>

#include "../source/ai_script_host.h"
#include "../source/level/qvm_decompiler.h"
#include "../source/level/qvm_ai_bindings.h"
#include "../source/level/qvm_parser.h"
#include "../source/renderer/graph_writer.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace {

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });
    return value;
}

std::filesystem::path FindChildCaseInsensitive(
    const std::filesystem::path& parent,
    const std::string& expected_name) {
    std::error_code error;
    const std::filesystem::path exact_path = parent / expected_name;
    if (std::filesystem::is_regular_file(exact_path, error) ||
        std::filesystem::is_directory(exact_path, error)) {
        return exact_path;
    }

    error.clear();
    if (!std::filesystem::is_directory(parent, error)) {
        return {};
    }

    const std::string expected_lower = ToLower(expected_name);
    for (const auto& entry : std::filesystem::directory_iterator(parent, error)) {
        if (error) {
            return {};
        }
        if (ToLower(entry.path().filename().string()) == expected_lower) {
            return entry.path();
        }
    }
    return {};
}

std::filesystem::path VanillaRoot() {
    const char* configured_root = std::getenv("IGI_VANILLA_ROOT");
    if (configured_root == nullptr || configured_root[0] == '\0') {
        return {};
    }
    return std::filesystem::path(configured_root);
}

std::filesystem::path VanillaLevelOneDirectory() {
    const std::filesystem::path root = VanillaRoot();
    const std::filesystem::path missions = FindChildCaseInsensitive(root, "missions");
    const std::filesystem::path location = FindChildCaseInsensitive(missions, "location0");
    return FindChildCaseInsensitive(location, "level1");
}

std::filesystem::path VanillaFile(const std::string& relative_name) {
    std::filesystem::path current = VanillaLevelOneDirectory();
    const std::filesystem::path relative_path(relative_name);
    for (const auto& component : relative_path) {
        current = FindChildCaseInsensitive(current, component.string());
        if (current.empty()) {
            return {};
        }
    }
    return current;
}

bool ContainsText(const QVMFile& qvm, const std::string& expected_text) {
    const auto contains = [&expected_text](const std::vector<std::string>& values) {
        return std::any_of(values.begin(), values.end(),
                           [&expected_text](const std::string& value) {
                               return value.find(expected_text) != std::string::npos;
                           });
    };

    if (contains(qvm.identifiers) || contains(qvm.strings)) {
        return true;
    }
    return std::any_of(qvm.instructions.begin(), qvm.instructions.end(),
                       [&expected_text](const QVMInstruction& instruction) {
                           return instruction.inline_text.find(expected_text) !=
                                  std::string::npos;
                       });
}

} // namespace

TEST(VanillaFixtureParityTest, LevelOneObjectsExposeAuthoredGameplayPrimitives) {
    const std::filesystem::path objects_path = VanillaFile("objects.qvm");
    if (VanillaRoot().empty()) {
        GTEST_SKIP() << "Set IGI_VANILLA_ROOT to the vanilla Project IGI install "
                        "to run this fixture parity test";
    }
    ASSERT_FALSE(objects_path.empty()) << "Vanilla objects.qvm is missing below "
                                          "IGI_VANILLA_ROOT";

    const QVMFile objects = QVM_Parse(objects_path.string());
    ASSERT_TRUE(objects.valid) << objects.error;
    EXPECT_EQ(objects.header.ver_major, 8U);
    EXPECT_EQ(objects.header.ver_minor, 5U);

    // These definitions are the authored bridge between the QVM and the
    // native runtime: player spawn, patrol, mission flow, and objectives.
    for (const std::string& primitive : {
             "HumanPlayer", "PatrolPath", "PatrolPathCommand", "LevelFlow",
             "DefineComputerObjective", "AreaActivate", "EditVariable"}) {
        EXPECT_TRUE(ContainsText(objects, primitive)) << primitive;
    }

    const std::string decompiled = QVM_DecompileToString(objects);
    EXPECT_NE(decompiled.find("HumanPlayer_0"), std::string::npos);
    EXPECT_NE(decompiled.find("LevelFlow"), std::string::npos);
}

TEST(VanillaFixtureParityTest, LevelOnePreservesAuthoredMissionResultExpressions) {
    const std::filesystem::path objects_path = VanillaFile("objects.qvm");
    if (VanillaRoot().empty()) {
        GTEST_SKIP() << "Set IGI_VANILLA_ROOT to the vanilla Project IGI install "
                        "to run this fixture parity test";
    }
    ASSERT_FALSE(objects_path.empty()) << "Vanilla objects.qvm is missing below "
                                          "IGI_VANILLA_ROOT";

    const QVMFile objects = QVM_Parse(objects_path.string());
    ASSERT_TRUE(objects.valid) << objects.error;

    // Level 1 completes through the authored cutscene and fails through the
    // authored status-message expression; it does not use an extraction zone.
    EXPECT_TRUE(ContainsText(objects, "CutScene_1204.isFinished"));
    EXPECT_TRUE(ContainsText(objects, "StatusMessage_1391.isSendt"));
    EXPECT_TRUE(ContainsText(
        objects,
        "HumanPlayer_0.isDead ||\nCar_1099.isExploded ||\nHumanSoldier_4023.isDead"));
    EXPECT_TRUE(ContainsText(objects, "MISSION_FAILED"));
}

TEST(VanillaFixtureParityTest, LevelOneGraphProvidesNavigableAuthoredData) {
    const std::filesystem::path graph_path = VanillaFile("graphs/graph1.dat");
    if (VanillaRoot().empty()) {
        GTEST_SKIP() << "Set IGI_VANILLA_ROOT to the vanilla Project IGI install "
                        "to run this fixture parity test";
    }
    ASSERT_FALSE(graph_path.empty()) << "Vanilla graph1.dat is missing below "
                                         "IGI_VANILLA_ROOT";

    const GraphFile graph = GRAPH_Parse(graph_path.string());
    ASSERT_TRUE(graph.valid) << graph.error;
    ASSERT_GT(graph.max_nodes, 0);
    ASSERT_FALSE(graph.nodes.empty());
    EXPECT_EQ(graph.route_table.size(),
              static_cast<size_t>(graph.max_nodes) * graph.max_nodes);

    for (const GraphNode& node : graph.nodes) {
        EXPECT_GE(node.id, 0);
        EXPECT_TRUE(std::isfinite(node.x));
        EXPECT_TRUE(std::isfinite(node.y));
        EXPECT_TRUE(std::isfinite(node.z));
    }
}

TEST(VanillaFixtureParityTest, RetailAiScriptDrivesPatrolAndAlarmBindings) {
    const std::filesystem::path ai_script_path = VanillaFile("ai/2205.qvm");
    if (VanillaRoot().empty()) {
        GTEST_SKIP() << "Set IGI_VANILLA_ROOT to the vanilla Project IGI install "
                        "to run this fixture parity test";
    }
    ASSERT_FALSE(ai_script_path.empty()) << "Vanilla AI 2205 QVM is missing below "
                                            "IGI_VANILLA_ROOT";

    const QVMFile ai_script = QVM_Parse(ai_script_path.string());
    ASSERT_TRUE(ai_script.valid) << ai_script.error;
    EXPECT_EQ(igi::FindFirstCallIntegerArgument(ai_script, "AIAction_Patrol"), 2405);

    igi::QvmNativeRegistry registry;
    igi::AiScriptHost script_host(registry);
    igi::QvmProgram program;
    ASSERT_TRUE(script_host.LoadProgram(ai_script, program))
        << script_host.GetLastError();

    igi::AiGuardEntity guard;
    guard.patrol_routes[2405] = {
        igi::AiPatrolCommand{igi::AiPatrolCommandKind::Delay, 3},
    };
    ASSERT_TRUE(script_host.Run(program, guard, 4))
        << script_host.GetLastError();
    EXPECT_EQ(guard.script_patrol_path_id, 2405);
    EXPECT_EQ(guard.active_patrol_path_id, 2405);
    ASSERT_EQ(guard.patrol_commands.size(), 1U);
    EXPECT_EQ(guard.patrol_commands[0].kind, igi::AiPatrolCommandKind::Delay);

    guard.patrol_started = true;
    guard.command_index = 0;
    ASSERT_TRUE(script_host.Run(program, guard, 4))
        << script_host.GetLastError();
    EXPECT_EQ(guard.command_index, 0);

    ASSERT_TRUE(script_host.Run(program, guard, 0))
        << script_host.GetLastError();
    EXPECT_EQ(guard.script_alarm_control_id, 98);
}
