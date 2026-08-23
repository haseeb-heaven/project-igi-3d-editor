// Unit tests for the animation playback port (#73) — open-igi evidence:
// BoneRotationSampler.cs (0x4D5180/0x4D4500), BoneClip.cs tick (0x4D4B60),
// MefSkinner.cs (0x49B700).
#include <gtest/gtest.h>
#include <cmath>
#include "../source/renderer/anim_player.h"

using namespace igi;

namespace {
AnimTranslationTrack MakeLinearTrack() {
    AnimTranslationTrack t;
    AnimTransKey k0; k0.time = 0.0f; k0.position = glm::vec3(0.0f);
    AnimTransKey k1; k1.time = 1.0f; k1.position = glm::vec3(10.0f, 0.0f, 0.0f);
    t.keys.push_back(k0);
    t.keys.push_back(k1);
    return t;
}
} // namespace

TEST(AnimPlayerTest, TranslationSampleMidpointLinear) {
    AnimTranslationTrack t = MakeLinearTrack();
    const glm::vec3 v = AnimTranslationSampler::Sample(t, 0.5f, /*spline=*/false);
    EXPECT_FLOAT_EQ(v.x, 5.0f);
    EXPECT_FLOAT_EQ(v.y, 0.0f);
    EXPECT_FLOAT_EQ(v.z, 0.0f);
}

TEST(AnimPlayerTest, TranslationSampleExactKeyUntouched) {
    AnimTranslationTrack t = MakeLinearTrack();
    const glm::vec3 v = AnimTranslationSampler::Sample(t, 1.0f, false);
    EXPECT_FLOAT_EQ(v.x, 10.0f);
}

TEST(AnimPlayerTest, TranslationSamplePastEndHolds) {
    AnimTranslationTrack t = MakeLinearTrack();
    const glm::vec3 v = AnimTranslationSampler::Sample(t, 7.0f, false);
    EXPECT_FLOAT_EQ(v.x, 10.0f);
}

TEST(AnimPlayerTest, RotationSlerpShorterArc) {
    // 90-degree yaw: slerp at half-way must be 45 degrees.
    const glm::quat from(1.0f, 0.0f, 0.0f, 0.0f); // identity (w,x,y,z)
    const glm::quat to = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0.f, 1.f, 0.f));
    const glm::quat mid = AnimRotationSampler::Slerp(from, to, 0.5f);
    const float angle = 2.0f * std::acos(std::clamp(std::abs(mid.w), 0.0f, 1.0f));
    // Half-way between identity and a 90-degree yaw is exactly 45 degrees.
    EXPECT_NEAR(angle, glm::quarter_pi<float>(), 1e-4f);
}

TEST(AnimPlayerTest, ClipClockLoopsAndWraps) {
    // Default rate is one authored frame step (160 units) per tick.
    AnimClipClock clock(/*duration=*/160.0f, /*loops=*/true);
    EXPECT_FLOAT_EQ(clock.Rate(), igi::kAnimFrameStep);
    clock.Tick(); // time = 160 >= duration -> end crossed, loop adjusts by one duration
    EXPECT_TRUE(clock.EndedThisTick());
    EXPECT_FLOAT_EQ(clock.Time(), 0.0f); // 160 + (-160)
    clock.Tick(); // second pass: time = 160 again -> wraps once more
    EXPECT_TRUE(clock.EndedThisTick());
    EXPECT_FLOAT_EQ(clock.Time(), 0.0f);
}

TEST(AnimPlayerTest, ClipClockNonLoopingStopsAtEnd) {
    AnimClipClock clock(/*duration=*/2.0f, /*loops=*/false);
    clock.Tick();
    clock.Tick();
    EXPECT_TRUE(clock.Ended());
    EXPECT_TRUE(clock.EndedThisTick());
    const float t = clock.Time();
    clock.Tick(); // clamped, no further progress past the end
    EXPECT_FLOAT_EQ(clock.Time(), t);
}
