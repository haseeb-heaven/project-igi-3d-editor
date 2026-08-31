#include <gtest/gtest.h>

#include "animation.h"
#include "runtime/graph_camera_target.h"

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
