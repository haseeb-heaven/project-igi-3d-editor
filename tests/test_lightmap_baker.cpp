// Unit tests for the lightmap bake engine (#72) — pure logic only.
// Tonemap math is verbatim from open-igi PostProcessShaders.cs (ToneMapAces :111,
// ToneMapNeutral :130); falloff and packing are this file's documented contracts.
#include <gtest/gtest.h>
#include <cmath>
#include "../source/renderer/lightmap_baker.h"

using namespace igi;

TEST(BakeMathTest, AcesTonemapMatchesOpenIgiCurve) {
    // Narkowicz constants: a=2.51 b=0.03 c=2.43 d=0.59 e=0.14.
    const glm::vec3 x(0.5f);
    const float expect = (0.5f * (2.51f * 0.5f + 0.03f)) /
                         (0.5f * (2.43f * 0.5f + 0.59f) + 0.14f);
    const glm::vec3 r = AcesTonemap(x);
    EXPECT_NEAR(r.r, expect, 1e-5f);
    // Curve anchors: 0 maps to 0; bright input rolls off toward (never past) 1.
    EXPECT_FLOAT_EQ(AcesTonemap(glm::vec3(0.0f)).r, 0.0f);
    EXPECT_GT(AcesTonemap(glm::vec3(4.0f)).r, 0.8f);
    EXPECT_LE(AcesTonemap(glm::vec3(4.0f)).r, 1.0f);
}

TEST(BakeMathTest, NeutralTonemapRollsOffAtWhitePoint) {
    // x*(1+x/w^2)/(1+x): at x=w this evaluates to exactly 1 (the shoulder's anchor).
    const glm::vec3 r = NeutralTonemap(glm::vec3(4.0f), 4.0f);
    EXPECT_NEAR(r.r, 1.0f, 1e-5f);
    // White point floor of 1: w<1 behaves like w=1.
    const glm::vec3 lo = NeutralTonemap(glm::vec3(4.0f), 0.5f);
    const glm::vec3 one = NeutralTonemap(glm::vec3(4.0f), 1.0f);
    EXPECT_FLOAT_EQ(lo.r, one.r);
    // Small values pass through near-unchanged (shoulder only bites near/above w).
    EXPECT_NEAR(NeutralTonemap(glm::vec3(0.25f), 8.0f).r,
                0.25f * (1.0f + 0.25f / 64.0f) / 1.25f, 1e-5f);
}

TEST(BakeMathTest, PointLightFalloffReachesZeroAtRadius) {
    BakePointLight l;
    l.position = glm::vec3(100.0f, 0.0f, 0.0f);
    l.color = glm::vec3(1.0f);
    l.radius = 50.0f;

    // Surface facing the light head-on at half radius: NdotL=1, window=(1-0.25)^2.
    const glm::vec3 pos_half(l.position.x - 25.0f, 0.0f, 0.0f);
    const glm::vec3 n_right(1.0f, 0.0f, 0.0f);
    const glm::vec3 v = BakePointLight::Evaluate(l, pos_half, n_right, false);
    EXPECT_FLOAT_EQ(v.r, 0.75f * 0.75f);

    // At the radius edge the contribution is exactly zero.
    const glm::vec3 pos_edge(l.position.x - 50.0f, 0.0f, 0.0f);
    EXPECT_FLOAT_EQ(BakePointLight::Evaluate(l, pos_edge, n_right, false).r, 0.0f);

    // Beyond the radius: zero.
    const glm::vec3 pos_out(l.position.x - 60.0f, 0.0f, 0.0f);
    EXPECT_FLOAT_EQ(BakePointLight::Evaluate(l, pos_out, n_right, false).r, 0.0f);

    // Shadowed sample: zero regardless of geometry.
    EXPECT_FLOAT_EQ(BakePointLight::Evaluate(l, pos_half, n_right, true).r, 0.0f);

    // Back-facing surface: zero (NdotL<=0).
    const glm::vec3 n_left(-1.0f, 0.0f, 0.0f);
    EXPECT_FLOAT_EQ(BakePointLight::Evaluate(l, pos_half, n_left, false).r, 0.0f);
}

TEST(BakeMathTest, SunLambertIsPureNdotl) {
    // sun_dir points TOWARD the sun; an up-facing normal is fully lit from straight above.
    const glm::vec3 up(0.0f, 0.0f, 1.0f);
    EXPECT_FLOAT_EQ(SunLambert(up, glm::vec3(0.0f, 0.0f, 1.0f)), 1.0f);
    // Grazing light along the surface plane: NdotL = 0.
    EXPECT_FLOAT_EQ(SunLambert(up, glm::vec3(1.0f, 0.0f, 0.0f)), 0.0f);
    // Light below the horizon clamps to zero.
    EXPECT_FLOAT_EQ(SunLambert(up, glm::vec3(0.0f, 0.0f, -1.0f)), 0.0f);
    EXPECT_NEAR(SunLambert(up, glm::vec3(-1.0f, 0.0f, 1.0f)), std::sqrt(0.5f), 1e-6f);
}

TEST(BakeMathTest, ShelfPackFirstFitAndShelfAdvance) {
    std::vector<int> w = {30, 30, 30};
    std::vector<int> h = {10, 10, 10};
    std::vector<ShelfRect> rects;
    ASSERT_TRUE(ShelfPack(64, w, h, rects));
    // Two fit side by side (30+30=60 <= 64; a third would be 90 > 64) then wrap.
    EXPECT_EQ(rects[0].x, 0);  EXPECT_EQ(rects[0].y, 0);
    EXPECT_EQ(rects[1].x, 30); EXPECT_EQ(rects[1].y, 0);
    EXPECT_EQ(rects[2].x, 0);  EXPECT_EQ(rects[2].y, 10); // next shelf down
}

TEST(BakeMathTest, ShelfPackRejectsOversizeItems) {
    std::vector<int> w = {128};
    std::vector<int> h = {64};
    std::vector<ShelfRect> rects;
    // Item wider than the page cannot be placed.
    EXPECT_FALSE(ShelfPack(64, w, h, rects));
}

TEST(BakeMathTest, ShelfPackFailsWhenAtlasOverflows) {
    // Two 40x50 items: the first starts shelf 0, the second wraps to a new shelf at
    // y=50, and 50+50 exceeds the 64-tall page.
    std::vector<int> w = {40, 40};
    std::vector<int> h = {50, 50};
    std::vector<ShelfRect> rects;
    EXPECT_FALSE(ShelfPack(64, w, h, rects));
}
