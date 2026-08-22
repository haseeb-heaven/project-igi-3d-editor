// test_weather_params.cpp — fixture-independent unit tests for the vanilla-parity
// weather math in source/renderer/weather_math.h. All expected values are derived
// from open-igi's retail-verified RainRenderer.cs / SnowRenderer.cs (issues #57/#58).
#include <gtest/gtest.h>
#include "../source/renderer/weather_math.h"

using namespace igi::weather;

TEST(WeatherMathTest, RainFallSpeedMatchesOpenIgi) {
    // RainRenderer.CalculateFallSpeed: (0.08 + seed * 0.10) * fallRange
    EXPECT_FLOAT_EQ(RainFallSpeed(100.0f, 0.0f), 8.0f);
    EXPECT_FLOAT_EQ(RainFallSpeed(100.0f, 1.0f), 18.0f);
    EXPECT_FLOAT_EQ(RainFallSpeed(100.0f, 0.5f), 13.0f);
    EXPECT_NEAR(RainFallSpeed(12.0f, 0.25f), (0.08f + 0.25f * 0.10f) * 12.0f, 1e-5f);
}

TEST(WeatherMathTest, SnowFallSpeedIsSlowerThanRain) {
    // SnowRenderer.CalculateFallSpeed: (0.025 + seed * 0.04) * fallRange —
    // "slow drifting snow flakes" must never outrun the fastest rain droplet.
    for (float seed = 0.0f; seed <= 1.0f; seed += 0.125f) {
        EXPECT_LT(SnowFallSpeed(100.0f, seed), RainFallSpeed(100.0f, seed));
    }
    EXPECT_FLOAT_EQ(SnowFallSpeed(100.0f, 0.0f), 2.5f);
    EXPECT_FLOAT_EQ(SnowFallSpeed(100.0f, 1.0f), 6.5f);
}

TEST(WeatherMathTest, FallSpeedClampsSeed) {
    // Out-of-range seeds clamp into [0,1] like Math.Clamp in open-igi.
    EXPECT_FLOAT_EQ(FallSpeed(100.0f, -3.0f, kSnowMinSpeedMul, kSnowMaxSpeedMul),
                    SnowFallSpeed(100.0f, 0.0f));
    EXPECT_FLOAT_EQ(FallSpeed(100.0f, 42.0f, kSnowMinSpeedMul, kSnowMaxSpeedMul),
                    SnowFallSpeed(100.0f, 1.0f));
}

TEST(WeatherMathTest, RainAlphaResponse) {
    // RainRenderer.Draw: clamp(alpha * 1.25, 0, 0.28)
    EXPECT_FLOAT_EQ(RainStreakAlpha(0.0f), 0.0f);
    EXPECT_FLOAT_EQ(RainStreakAlpha(0.13f), 0.13f * 1.25f);   // level3-style authored value
    EXPECT_FLOAT_EQ(RainStreakAlpha(0.9f), kRainMaxAlpha);     // clamped high
    EXPECT_GT(RainStreakAlpha(0.13f), 0.0f);
}

TEST(WeatherMathTest, SnowAlphaResponse) {
    // SnowRenderer.Draw: clamp(alpha * 1.75, 0.10, 0.42)
    EXPECT_FLOAT_EQ(SnowFlakeAlpha(0.0f), kSnowMinAlpha);      // floor keeps flakes visible
    EXPECT_FLOAT_EQ(SnowFlakeAlpha(0.2f), 0.35f);
    EXPECT_FLOAT_EQ(SnowFlakeAlpha(0.9f), kSnowMaxAlpha);       // clamped high
}

TEST(WeatherMathTest, SnowDriftOscillatesWithinAmplitude) {
    for (float t = 0.0f; t <= 30.0f; t += 0.7f) {
        float dx = SnowDriftX(t, 0.37f);
        float dy = SnowDriftY(t, 0.73f);
        EXPECT_LE(std::abs(dx), kSnowDriftMeters + 1e-4f);
        EXPECT_LE(std::abs(dy), kSnowDriftMeters + 1e-4f);
    }
    // sin(0)=0 and cos(0)=1 anchors
    EXPECT_NEAR(SnowDriftX(0.0f, 0.0f), 0.0f, 1e-6f);
    EXPECT_NEAR(SnowDriftY(0.0f, 0.0f), kSnowDriftMeters, 1e-6f);
}

TEST(WeatherMathTest, WrapMatchesOpenIgiModHelper) {
    EXPECT_FLOAT_EQ(Wrap(-1.0f, 50.0f), 49.0f);
    EXPECT_FLOAT_EQ(Wrap(75.0f, 50.0f), 25.0f);
    EXPECT_FLOAT_EQ(Wrap(50.0f, 50.0f), 0.0f);
    EXPECT_FLOAT_EQ(Wrap(13.5f, 50.0f), 13.5f);
}

TEST(WeatherMathTest, VanillaConstantsMatchOpenIgiSource) {
    // Pin the ported constants so a silent drift from open-igi fails this test.
    EXPECT_EQ(kRainDrops, 1200);          // RainRenderer.DefaultParticleCount
    EXPECT_EQ(kSnowFlakes, 900);          // SnowRenderer.DefaultParticleCount
    EXPECT_FLOAT_EQ(kBoxMeters, 50.0f);   // shared camera wrap box
    EXPECT_FLOAT_EQ(kRainStreakMeters, 0.08f);
    EXPECT_FLOAT_EQ(kSnowFlakeMeters, 0.045f);
    EXPECT_FLOAT_EQ(kSnowDriftMeters, 0.35f);
    EXPECT_FLOAT_EQ(kRainMinSpeedMul, 0.08f);
    EXPECT_FLOAT_EQ(kRainMaxSpeedMul, 0.18f);
    EXPECT_FLOAT_EQ(kSnowMinSpeedMul, 0.025f);
    EXPECT_FLOAT_EQ(kSnowMaxSpeedMul, 0.065f);
    EXPECT_FLOAT_EQ(kRainMaxAlpha, 0.28f);
    EXPECT_FLOAT_EQ(kSnowMaxAlpha, 0.42f);
}
