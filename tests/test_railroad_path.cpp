// Unit tests for the railroad spline evaluation (issue #59) — port of open-igi
// RailroadPath.cs semantics (retail igi2 evidence: sub_4E4530 waypoint frames,
// signed end-relative scalar convention).
#include <gtest/gtest.h>
#include <cmath>
#include "../source/renderer/railroad_path.h"

using igi::RailroadPath;
using igi::RailroadWaypoint;

namespace {
// Mission-1-like straight rail: 4.7M units along +X, train starts at -971400.
std::vector<RailroadWaypoint> StraightRail(double length, int knots = 5) {
    std::vector<RailroadWaypoint> wps;
    for (int i = 0; i < knots; ++i) {
        RailroadWaypoint wp;
        wp.position = glm::dvec3(length * i / double(knots - 1), 0.0, 0.0);
        // Identity orientation -> tangent = local X = +down the rail.
        wps.push_back(wp);
    }
    return wps;
}
} // namespace

TEST(RailroadPathTest, NeedsAtLeastTwoWaypoints) {
    std::vector<RailroadWaypoint> one(1);
    EXPECT_FALSE(RailroadPath::Build(one, false, 0, 0, 0).has_value());
    EXPECT_TRUE(RailroadPath::Build(StraightRail(1000.0), false, 0, 0, 0).has_value());
}

TEST(RailroadPathTest, TangentFromEngineEulerIdentityPointsDownRail) {
    // Identity Euler -> first column of Rz*Ry*Rx = (1, 0, 0): rail runs down local X.
    auto path = RailroadPath::Build(StraightRail(4700000.0), false, 0, 0, 0).value();
    glm::dvec3 pos, orient_col0;
    glm::dmat3 orient;
    path.Evaluate(path.TotalLength() * 0.5, pos, orient);
    orient_col0 = orient[0];
    EXPECT_NEAR(orient_col0.x, 1.0, 1e-6);
    EXPECT_NEAR(orient_col0.y, 0.0, 1e-6);
    EXPECT_NEAR(orient_col0.z, 0.0, 1e-6);
}

TEST(RailroadPathTest, EngineEulerFirstColumnFormula) {
    // Rz(g)*Ry(b)*Rx(a) first column = (cg*cb, sg*cb, -sb); check beta=90deg:
    // first column = (0, 0, -1) for any gamma — rail runs down -Z.
    RailroadWaypoint wp;
    wp.alpha = 0.0f; wp.beta = 1.5707963f; wp.gamma = 0.7f;
    RailroadWaypoint wp2 = wp;
    wp2.position = glm::dvec3(0.0, 0.0, -100.0); // along the (0,0,-1) tangent
    auto path = RailroadPath::Build({wp, wp2}, false, 0, 0, 0).value();
    glm::dvec3 pos;
    glm::dmat3 orient;
    path.Evaluate(50.0, pos, orient);
    EXPECT_NEAR(orient[0].z, -1.0, 1e-6);
    EXPECT_NEAR(glm::length(glm::dvec3(orient[0].x, orient[0].y, 0.0)), 0.0, 1e-6);
}

TEST(RailroadPathTest, NegativeScalarMeasuresFromFarEnd) {
    // Retail convention: Mission 1's train starts at -971400 on a ~4.7M-unit rail.
    const double kRailLength = 4700000.0;
    auto path = RailroadPath::Build(StraightRail(kRailLength), false, -971400.0, 0, 0).value();
    ASSERT_NEAR(path.TotalLength(), kRailLength, kRailLength * 1e-3);

    glm::dvec3 pos;
    glm::dmat3 orient;
    path.Evaluate(-971400.0, pos, orient);
    // total + (-971400) from the start => x ~= 3728600 (near the far end).
    EXPECT_GT(pos.x, kRailLength * 0.75);
    EXPECT_LT(pos.x, kRailLength);

    // Negating the sign would place the consist ~500m from the cameras' end instead.
    glm::dvec3 pos_wrong;
    path.Evaluate(+971400.0, pos_wrong, orient);
    EXPECT_LT(pos_wrong.x, kRailLength * 0.25);
}

TEST(RailroadPathTest, ScalarClampsToRailEnds) {
    auto path = RailroadPath::Build(StraightRail(1000.0), false, 0, 0, 0).value();
    glm::dvec3 pos, pos2;
    glm::dmat3 orient;
    path.Evaluate(-999999999.0, pos, orient);
    EXPECT_NEAR(pos.x, 0.0, 1e-6); // clamps to first knot
    path.Evaluate(+999999999.0, pos2, orient);
    EXPECT_NEAR(pos2.x, 1000.0, 1.0); // clamps to last knot
}

TEST(RailroadPathTest, FlipDirectionNegatesTravel) {
    auto path_fwd = RailroadPath::Build(StraightRail(1000.0), false, 0, 0, 0).value();
    auto path_flip = RailroadPath::Build(StraightRail(1000.0), true, 0, 0, 0).value();
    glm::dvec3 p1, p2;
    glm::dmat3 o1, o2;
    path_fwd.Evaluate(500.0, p1, o1);
    path_flip.Evaluate(500.0, p2, o2);
    // Position identical on a straight symmetric rail; forward column must negate.
    EXPECT_NEAR(o2[0].x, -o1[0].x, 1e-9);
    EXPECT_NEAR(o2[0].y, -o1[0].y, 1e-9);
    EXPECT_NEAR(o2[0].z, -o1[0].z, 1e-9);
}

TEST(RailroadPathTest, DisplacementAppliedAlongFrameAxes) {
    auto path = RailroadPath::Build(StraightRail(1000.0), false, 0, 120.0, 40.0).value();
    glm::dvec3 pos;
    glm::dmat3 orient;
    path.Evaluate(500.0, pos, orient);
    // On an identity-oriented straight rail: side axis [1] = +Y-ish, third col [2] = up.
    // Offset X=120 shifts along the side axis, Y=40 along the frame third column.
    EXPECT_NEAR(pos.y, 120.0, 1e-6);
    EXPECT_NEAR(pos.z, 40.0, 1e-6);
}
