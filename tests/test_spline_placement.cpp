// Unit tests for arc-length spline placement math (#69).
#include <gtest/gtest.h>
#include <cmath>
#include "../source/renderer/spline_placement.h"

using namespace igi;

TEST(SplinePlacementTest, ChooseTileCountNearestNaturalLength) {
    EXPECT_EQ(ChooseTileCount(1000.0f, 500.0f, 64), 2);   // exactly 2 tiles
    EXPECT_EQ(ChooseTileCount(1200.0f, 400.0f, 64), 3);   // exactly 3 tiles
    EXPECT_EQ(ChooseTileCount(900.0f, 400.0f, 64), 2);    // 2.25 -> 2
    EXPECT_EQ(ChooseTileCount(1100.0f, 400.0f, 64), 3);   // 2.75 -> 3
}

TEST(SplinePlacementTest, ChooseTileCountClampsAndGuards) {
    EXPECT_EQ(ChooseTileCount(50.0f, 500.0f, 64), 1);     // at least one tile
    EXPECT_EQ(ChooseTileCount(99999.0f, 10.0f, 64), 64);  // capped
    EXPECT_EQ(ChooseTileCount(1000.0f, 0.0f, 64), 64);    // degenerate extent guarded to 1.0, then capped
}

TEST(SplinePlacementTest, MakeTrackFrameZeroRollIdentityForward) {
    SplineFrame f = MakeTrackFrame(glm::vec3(1.0f, 0.0f, 0.0f));
    EXPECT_FLOAT_EQ(f.forward.x, 1.0f);
    EXPECT_FLOAT_EQ(f.side.y, 1.0f);   // side = (-fy, fx, 0)
    EXPECT_FLOAT_EQ(f.up.z, 1.0f);     // up = forward x side
}

TEST(SplinePlacementTest, MakeTrackFrameVerticalFallback) {
    // Near-vertical forward: worldZ cross degenerates, falls back to X cross.
    SplineFrame f = MakeTrackFrame(glm::vec3(0.0f, 0.0f, 1.0f));
    EXPECT_FLOAT_EQ(glm::length(f.forward), 1.0f);
    EXPECT_FLOAT_EQ(glm::length(f.side), 1.0f);
    EXPECT_FLOAT_EQ(glm::dot(f.forward, f.side), 0.0f);
}

TEST(SplinePlacementTest, TrackArcLengthAccumulatesPolyline) {
    SplinePlacementTrack t;
    t.AddSample({0.0, 0.0, 0.0}, {1, 0, 0});
    t.AddSample({100.0, 0.0, 0.0}, {1, 0, 0});
    t.AddSample({100.0, 100.0, 0.0}, {0, 1, 0});
    EXPECT_NEAR(t.TotalLength(), 200.0, 1e-3);
}

TEST(SplinePlacementTest, SampleAtArcInterpolatesMidpoint) {
    SplinePlacementTrack t;
    t.AddSample({0.0, 0.0, 0.0}, {1, 0, 0});
    t.AddSample({100.0, 0.0, 0.0}, {1, 0, 0});
    auto s = t.SampleAtArc(50.0f);
    EXPECT_NEAR(s.pos.x, 50.0, 1e-6);
    EXPECT_NEAR(glm::length(glm::vec3(s.forward)), 1.0, 1e-5);
}

TEST(SplinePlacementTest, SampleAtArcClampsToEnds) {
    SplinePlacementTrack t;
    t.AddSample({0.0, 0.0, 0.0}, {1, 0, 0});
    t.AddSample({100.0, 0.0, 0.0}, {1, 0, 0});
    EXPECT_NEAR(t.SampleAtArc(-5.0f).pos.x, 0.0, 1e-6);
    EXPECT_NEAR(t.SampleAtArc(500.0f).pos.x, 100.0, 1e-6);
}

TEST(SplinePlacementTest, LegacyTrackStraightRailLengthMatchesKnotSpan) {
    std::vector<glm::vec3> knots = {
        {0, 0, 0}, {1000, 0, 0}, {2000, 0, 0}, {3000, 0, 0}};
    auto t = BuildLegacyTrack(knots);
    EXPECT_NEAR(t.TotalLength(), 3000.0, 1.0);
    auto mid = t.SampleAtArc(1500.0f);
    EXPECT_NEAR(mid.pos.x, 1500.0, 2.0);
    EXPECT_NEAR(mid.pos.y, 0.0, 2.0);
}

TEST(SplinePlacementTest, LegacyCurveLongerThanChord) {
    // An L-shaped path: arc length exceeds the end-to-end chord — proves the
    // track measures true curve length, not displacement.
    std::vector<glm::vec3> knots = {
        {0, 0, 0}, {1000, 0, 0}, {1000, 1000, 0}};
    auto t = BuildLegacyTrack(knots);
    // Hermite corner rounding adds a little length over the polyline (~2%).
    EXPECT_NEAR(t.TotalLength(), 2000.0, 60.0);
    EXPECT_GT(t.TotalLength(), glm::length(knots.back() - knots.front()) + 100.0);
}
