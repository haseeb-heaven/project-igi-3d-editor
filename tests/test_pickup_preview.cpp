// Unit tests for pickup parity constants and gizmo geometry (#75).
// Reference: open-igi GunPickupRegistry.cs / GenericPickupRegistry.cs
// (0x46C630 / 0x46C980 / 0x46D230).
#include <gtest/gtest.h>
#include <cmath>
#include "../source/renderer/pickup_preview.h"

using igi::BuildPickupGizmoLines;
using igi::kPickupRadiusUnits;
using igi::kPickupVerticalToleranceUnits;

TEST(PickupParityTest, ConstantsMatchOpenIgiSource) {
    EXPECT_DOUBLE_EQ(kPickupRadiusUnits, 2867.2);
    EXPECT_DOUBLE_EQ(kPickupVerticalToleranceUnits, 4096.0);
}

TEST(PickupParityTest, RadiusDiscIsClosedAndCentred) {
    const glm::dvec3 centre(100.0, 2000.0, 300.0);
    auto lines = BuildPickupGizmoLines(centre);
    ASSERT_FALSE(lines.empty());
    // Disc segments come first; the vertical tolerance tick is appended LAST.
    const size_t last_disc = lines.size() - 2;
    // Ring closes: first segment starts where the last disc segment ends.
    EXPECT_NEAR(lines.front().first.x, lines[last_disc].second.x, 1e-6);
    EXPECT_NEAR(lines.front().first.z, lines[last_disc].second.z, 1e-6);
    // All disc points share the pickup height (skip the trailing vertical tick).
    for (size_t i = 0; i + 1 < lines.size(); ++i) {
        const auto& l = lines[i];
        EXPECT_NEAR(l.first.y, centre.y, 1e-9);
        EXPECT_NEAR(l.second.y, centre.y, 1e-9);
    }
}

TEST(PickupParityTest, DiscPointsAtExactRadius) {
    const glm::dvec3 centre(0.0, 0.0, 0.0);
    auto lines = BuildPickupGizmoLines(centre);
    ASSERT_GE(lines.size(), 2u) << "disc segments plus vertical tick";
    for (size_t i = 0; i + 1 < lines.size(); ++i) {  // exclude trailing tick
        const auto& l = lines[i];
        const double r0 = std::sqrt(l.first.x * l.first.x + l.first.z * l.first.z);
        const double r1 = std::sqrt(l.second.x * l.second.x + l.second.z * l.second.z);
        EXPECT_NEAR(r0, kPickupRadiusUnits, 1e-6);
        EXPECT_NEAR(r1, kPickupRadiusUnits, 1e-6);
    }
}

TEST(PickupParityTest, VerticalToleranceTickSpansBand) {
    const glm::dvec3 centre(50.0, 1000.0, 60.0);
    auto lines = BuildPickupGizmoLines(centre);
    bool found_tick = false;
    for (const auto& l : lines) {
        if (std::fabs(l.first.x - centre.x) < 1e-9 &&
            std::fabs(l.first.y - (centre.y - kPickupVerticalToleranceUnits)) < 1e-9 &&
            std::fabs(l.second.y - (centre.y + kPickupVerticalToleranceUnits)) < 1e-9) {
            found_tick = true;
        }
    }
    EXPECT_TRUE(found_tick);
}
