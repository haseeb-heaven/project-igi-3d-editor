// Unit tests for ladder placement gizmo math (#68) — port of open-igi
// LadderPlacement.cs + LadderClimbLine.cs (retail evidence sub_4404C0 / sub_4113E0 /
// sub_440280). All constants are game units (4096 = 1 m).
#include <gtest/gtest.h>
#include <cmath>
#include "../source/renderer/ladder_preview.h"

using igi::BuildLadderGizmoLines;
using igi::ComputeLadderGizmo;
using igi::kLadderActivateHalfWidthUnits;
using igi::kLadderActivateRadiusUnits;
using igi::kLadderBottomStandoffUnits;
using igi::kLadderBottomVerticalOffsetUnits;
using igi::kLadderStepLengthUnits;
using igi::kLadderTopVerticalOffsetUnits;
using igi::kLadderActivateRadiusUnits;
using igi::kLadderStepLengthUnits;

namespace {
// Identity orientation: local X = +X (face across X), standoff = +Y, vertical = +Z.
glm::dmat3 IdentityOrientation() { return glm::dmat3(1.0); }

struct Verts {
    glm::dvec3 origin, v1, v2, v3;
};
// A 5-metre upright ladder at the origin: foot at z=0, head at z=20480 (=5 m).
Verts UprightLadder() {
    return {glm::dvec3(0.0), glm::dvec3(0.0, -1000.0, 21504.0),
            glm::dvec3(0.0, 0.0, 0.0), glm::dvec3(0.0, 0.0, 20480.0)};
}
} // namespace

TEST(LadderGizmoTest, ConstantsMatchOpenIgiSource) {
    EXPECT_DOUBLE_EQ(kLadderActivateRadiusUnits, 8192.0);
    EXPECT_DOUBLE_EQ(kLadderActivateHalfWidthUnits, 2867.2);
    EXPECT_DOUBLE_EQ(kLadderBottomStandoffUnits, 1515.52);
    EXPECT_DOUBLE_EQ(kLadderBottomVerticalOffsetUnits, 3915.776);
    EXPECT_DOUBLE_EQ(kLadderTopVerticalOffsetUnits, 3481.6);
    EXPECT_DOUBLE_EQ(kLadderStepLengthUnits, 1228.8);
}

TEST(LadderGizmoTest, StepCountTruncatesHeightPerRung) {
    // open-igi: a 3 m ladder is 10 steps (12288/1228.8); truncation, not rounding.
    auto g = ComputeLadderGizmo(glm::dvec3(0.0), glm::dvec3(0, 0, 14336),
                                glm::dvec3(0.0), glm::dvec3(0, 0, 12288.0),
                                IdentityOrientation());
    EXPECT_EQ(g.step_count, 10);

    // The 5 m upright ladder: 20480/1228.8 = 16.66 -> 16 steps.
    auto v = UprightLadder();
    auto g5 = ComputeLadderGizmo(v.origin, v.v1, v.v2, v.v3, IdentityOrientation());
    EXPECT_EQ(g5.step_count, 16);
}

TEST(LadderGizmoTest, DegenerateLadderStillHasOneStep) {
    auto g = ComputeLadderGizmo(glm::dvec3(0.0), glm::dvec3(0.0),
                                glm::dvec3(0.0), glm::dvec3(0.0), IdentityOrientation());
    EXPECT_EQ(g.step_count, 1);
}

TEST(LadderGizmoTest, BottomMountUsesStandoffAndVerticalColumns) {
    auto v = UprightLadder();
    auto g = ComputeLadderGizmo(v.origin, v.v1, v.v2, v.v3, IdentityOrientation());
    // Identity frame: standoff=+Y, vertical=+Z.
    // bottom_mount = foot + (+Y * 1515.52) + (+Z * 3915.776)
    EXPECT_DOUBLE_EQ(g.bottom_mount.x, 0.0);
    EXPECT_DOUBLE_EQ(g.bottom_mount.y, kLadderActivateHalfWidthUnits * 0.0 + 1515.52);
    EXPECT_DOUBLE_EQ(g.bottom_mount.z, 3915.776);
}

TEST(LadderGizmoTest, TopMountUsesVerticalLiftAndReverseStandoff) {
    auto v = UprightLadder();
    auto g = ComputeLadderGizmo(v.origin, v.v1, v.v2, v.v3, IdentityOrientation());
    // top_mount = head + (+Z * 3481.6) + (+Y * -1638.4)
    EXPECT_DOUBLE_EQ(g.top_mount.x, 0.0);
    EXPECT_DOUBLE_EQ(g.top_mount.y, -1638.4);
    EXPECT_DOUBLE_EQ(g.top_mount.z, 20480.0 + 3481.6);
}

TEST(LadderGizmoTest, FacingIncludesAddedPi) {
    // Identity frame col0 = (1,0): atan2(0,1)=0, facing = pi.
    auto v = UprightLadder();
    auto g = ComputeLadderGizmo(v.origin, v.v1, v.v2, v.v3, IdentityOrientation());
    EXPECT_NEAR(g.facing, M_PI, 1e-12);

    // Col0 = +Y (face across Y): atan2(1,0)=pi/2 -> facing = 3pi/2.
    glm::dmat3 rot90(0.0);
    rot90[0] = glm::dvec3(0.0, 1.0, 0.0);  // col0
    rot90[1] = glm::dvec3(-1.0, 0.0, 0.0); // col1
    rot90[2] = glm::dvec3(0.0, 0.0, 1.0);  // col2
    auto g2 = ComputeLadderGizmo(v.origin, v.v1, v.v2, v.v3, rot90);
    EXPECT_NEAR(g2.facing, 1.5 * M_PI, 1e-12);
}

TEST(LadderGizmoTest, GizmoLinesCoverClimbBandsAndMounts) {
    auto v = UprightLadder();
    auto g = ComputeLadderGizmo(v.origin, v.v1, v.v2, v.v3, IdentityOrientation());
    auto lines = BuildLadderGizmoLines(g);

    // climb line + 15 interior rung ticks + 2 face bands + 2 mount connectors
    ASSERT_EQ(lines.size(), static_cast<size_t>(1 + g.step_count - 1 + 2 + 2));

    bool has_climb_line = false, has_bottom_connector = false, has_top_connector = false;
    for (const auto& seg : lines) {
        if (seg.first == v.v2 && seg.second == v.v3) has_climb_line = true;
        if (seg.first == v.v2 && seg.second == g.bottom_mount) has_bottom_connector = true;
        if (seg.first == v.v3 && seg.second == g.top_mount) has_top_connector = true;
    }
    EXPECT_TRUE(has_climb_line);
    EXPECT_TRUE(has_bottom_connector);
    EXPECT_TRUE(has_top_connector);

    // Face bands span +-2867.2 along local X at foot and head.
    const double band = kLadderActivateHalfWidthUnits;
    bool has_foot_band = false;
    for (const auto& seg : lines) {
        if (std::fabs(seg.first.x - (-band)) < 1e-9 && std::fabs(seg.second.x - band) < 1e-9 &&
            std::fabs(seg.first.z - v.v2.z) < 1e-9)
            has_foot_band = true;
    }
    EXPECT_TRUE(has_foot_band);
}
