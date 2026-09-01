#include <gtest/gtest.h>

#include "animation.h"
#include "runtime/graph_camera_target.h"
#include "utils.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {

AnimationClip Clip(int duration, bool loop = false) {
    AnimationClip clip;
    clip.length_ms = duration;
    clip.tp_flag = loop ? 1 : 0;
    return clip;
}

GraphNode Node(int id, double x, double y, double z) {
    GraphNode node;
    node.id = id;
    node.x = x;
    node.y = y;
    node.z = z;
    return node;
}

}  // namespace

TEST(AnimationPlaybackTest, AdvancesWhilePlaying) {
    const AnimationClip clip = Clip(1000);
    AnimPlayback playback;
    playback.Start(&clip);

    playback.Update(125.0f);

    EXPECT_FLOAT_EQ(playback.currentTimeMs, 125.0f);
    EXPECT_TRUE(playback.playing);
}

TEST(AnimationPlaybackTest, ForceLoopKeepsNonLoopingAiAnimationAlive) {
    const AnimationClip clip = Clip(1000);
    AnimPlayback playback;
    playback.Start(&clip);
    playback.forceLoop = true;

    playback.Update(2250.0f);

    EXPECT_FLOAT_EQ(playback.currentTimeMs, 250.0f);
    EXPECT_TRUE(playback.playing);
}

TEST(AnimationPlaybackTest, NonLoopingClipStopsAtItsEnd) {
    const AnimationClip clip = Clip(1000);
    AnimPlayback playback;
    playback.Start(&clip);

    playback.Update(1250.0f);

    EXPECT_FLOAT_EQ(playback.currentTimeMs, 1000.0f);
    EXPECT_FALSE(playback.playing);
}

TEST(AnimationRenderGateTest, RequiresActivePlaybackAndSkinGeometry) {
    const AnimationClip clip = Clip(1000);
    AnimPlayback playback;
    playback.Start(&clip);

    EXPECT_TRUE(ShouldUseSkinnedReplacement(playback, 2, 5, false, true));
    EXPECT_FALSE(ShouldUseSkinnedReplacement(playback, 2, 5, false, false));
    playback.Pause();
    EXPECT_FALSE(ShouldUseSkinnedReplacement(playback, 2, 5, false, true));
    EXPECT_FALSE(ShouldUseSkinnedReplacement(playback, 5, 5, false, true));
    EXPECT_FALSE(ShouldUseSkinnedReplacement(playback, 2, 5, true, true));
}

TEST(GraphCameraTargetTest, SelectedNodeUsesGraphOffset) {
    GraphFile graph;
    graph.valid = true;
    graph.nodes.push_back(Node(7, 10.0, 20.0, 30.0));

    const GraphCameraTarget target = ResolveGraphCameraTarget(
        graph, true, 7, glm::dvec3(100.0, 200.0, 300.0), glm::dvec3(1.0));

    EXPECT_EQ(target.kind, GraphCameraTargetKind::GraphNode);
    EXPECT_EQ(target.node_id, 7);
    EXPECT_DOUBLE_EQ(target.position.x, 110.0);
    EXPECT_DOUBLE_EQ(target.position.y, 220.0);
    EXPECT_DOUBLE_EQ(target.position.z, 330.0);
}

TEST(GraphCameraTargetTest, VisibleGraphWithoutNodeSelectionUsesOrigin) {
    GraphFile graph;
    graph.valid = true;
    graph.nodes.push_back(Node(1, 10.0, 20.0, 30.0));

    const GraphCameraTarget target = ResolveGraphCameraTarget(
        graph, true, -1, glm::dvec3(100.0, 200.0, 300.0), glm::dvec3(1.0));

    EXPECT_EQ(target.kind, GraphCameraTargetKind::GraphOrigin);
    EXPECT_DOUBLE_EQ(target.position.x, 100.0);
    EXPECT_DOUBLE_EQ(target.position.y, 200.0);
    EXPECT_DOUBLE_EQ(target.position.z, 300.0);
}

TEST(GraphCameraTargetTest, FallsBackToObjectWhenGraphIsHidden) {
    GraphFile graph;
    graph.valid = true;

    const GraphCameraTarget target = ResolveGraphCameraTarget(
        graph, false, 3, glm::dvec3(100.0), glm::dvec3(4.0, 5.0, 6.0));

    EXPECT_EQ(target.kind, GraphCameraTargetKind::Object);
    EXPECT_EQ(target.position, glm::dvec3(4.0, 5.0, 6.0));
}

TEST(GraphCameraTargetTest, HiddenOverlayUsesRelatedAiGraphOrigin) {
    GraphFile graph;
    graph.valid = true;

    const GraphCameraTarget target = ResolveGraphCameraTarget(
        graph, false, -1, glm::dvec3(100.0), glm::dvec3(4.0, 5.0, 6.0),
        glm::dvec3(400.0, 500.0, 600.0));

    EXPECT_EQ(target.kind, GraphCameraTargetKind::GraphOrigin);
    EXPECT_EQ(target.position, glm::dvec3(400.0, 500.0, 600.0));
}

#if defined(_WIN32)
TEST(AnimationIntegrationTest, EditorSubmitsSkinnedAiReplacements) {
    if (std::getenv("IGI_RUN_LIVE_EDITOR_TESTS") == nullptr) {
        GTEST_SKIP() << "Set IGI_RUN_LIVE_EDITOR_TESTS=1 for the opt-in editor integration run";
    }

    namespace fs = std::filesystem;
    const char* gamePathEnv = std::getenv("IGI_GAME_PATH");
    if (gamePathEnv == nullptr || *gamePathEnv == '\0') {
        GTEST_SKIP() << "IGI_GAME_PATH must point to the co-located IGI install";
    }

    const fs::path gameRoot(gamePathEnv);
    const fs::path configPath = gameRoot / "editor" / "qed" / "qedconfig.qsc";
    const fs::path configQvmPath = gameRoot / "editor" / "qed" / "qedconfig.qvm";
    const fs::path logPath = gameRoot / "igi1ed.log";
    const fs::path exePath = fs::path(Utils::GetExeDirectory()) / "igi1ed.exe";
    ASSERT_TRUE(fs::exists(configPath));
    ASSERT_TRUE(fs::exists(configQvmPath));
    ASSERT_TRUE(fs::exists(exePath));

    const std::string configSource = [&]() {
        std::ifstream in(configPath, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(in)), {});
    }();
    std::ifstream qvmIn(configQvmPath, std::ios::binary);
    const std::vector<char> configQvm(
        (std::istreambuf_iterator<char>(qvmIn)), std::istreambuf_iterator<char>());
    const auto restoreConfig = [&]() {
        std::ofstream out(configPath, std::ios::binary | std::ios::trunc);
        out.write(configSource.data(), static_cast<std::streamsize>(configSource.size()));
        out.close();
        std::ofstream qvmOut(configQvmPath, std::ios::binary | std::ios::trunc);
        qvmOut.write(configQvm.data(), static_cast<std::streamsize>(configQvm.size()));
    };

    const size_t loggingSetting = configSource.find("QEDLogs(FALSE);");
    ASSERT_NE(loggingSetting, std::string::npos);
    std::string enabledConfig = configSource;
    enabledConfig.replace(loggingSetting, std::string("QEDLogs(FALSE);").size(),
                          "QEDLogs(TRUE);");
    {
        std::ofstream out(configPath, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(out.is_open());
        out << enabledConfig;
    }
    struct RestoreOnExit {
        std::function<void()> restore;
        ~RestoreOnExit() { restore(); }
    } restore{restoreConfig};

    const auto logBefore = fs::exists(logPath) ? fs::file_size(logPath) : 0;
    std::string command = "\"" + exePath.string() + "\" -level 12";
    std::vector<char> commandBuffer(command.begin(), command.end());
    commandBuffer.push_back('\0');
    STARTUPINFOA startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo{};
    ASSERT_TRUE(CreateProcessA(nullptr, commandBuffer.data(), nullptr, nullptr, FALSE,
                               CREATE_NEW_CONSOLE, nullptr, gameRoot.string().c_str(),
                               &startupInfo, &processInfo));
    const DWORD waitResult = WaitForSingleObject(processInfo.hProcess, 10000);
    if (waitResult == WAIT_TIMEOUT) {
        ASSERT_TRUE(TerminateProcess(processInfo.hProcess, 0));
        ASSERT_EQ(WaitForSingleObject(processInfo.hProcess, 5000), WAIT_OBJECT_0);
    } else {
        ASSERT_EQ(waitResult, WAIT_OBJECT_0);
    }
    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);

    std::ifstream logIn(logPath, std::ios::binary);
    const std::string log((std::istreambuf_iterator<char>(logIn)), {});
    ASSERT_GT(log.size(), logBefore);
    const std::string newLog = log.substr(logBefore);
    EXPECT_NE(newLog.find("Animation init complete"), std::string::npos);
    EXPECT_NE(newLog.find("Skinned/animated replacement active"), std::string::npos);
}
#endif
