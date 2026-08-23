// Unit tests for per-mission fog presets (#66) — port of open-igi FogPresets.cs.
#include <gtest/gtest.h>
#include <cmath>
#include "../source/renderer/fog_presets.h"

using igi::FogPresetEntry;
using igi::FogPresets;
using igi::ResolvedFog;

TEST(FogPresetsTest, DeriveFlatSkyDensityMatchesOpenIgiRule) {
    // authoredDensity = 0.004 + clamp(FogAmount,0,1)*0.03 ; no-sky fallback 0.014
    EXPECT_FLOAT_EQ(igi::FogPresets::DeriveFlatSkyDensity(0.0f, true), 0.004f);
    EXPECT_FLOAT_EQ(igi::FogPresets::DeriveFlatSkyDensity(1.0f, true), 0.034f);
    EXPECT_FLOAT_EQ(igi::FogPresets::DeriveFlatSkyDensity(0.5f, true), 0.019f);
    EXPECT_FLOAT_EQ(igi::FogPresets::DeriveFlatSkyDensity(2.0f, true), 0.034f); // clamped
    EXPECT_FLOAT_EQ(igi::FogPresets::DeriveFlatSkyDensity(-3.0f, true), 0.004f); // clamped
    EXPECT_FLOAT_EQ(igi::FogPresets::DeriveFlatSkyDensity(0.7f, false), 0.014f); // no-sky fallback
}

TEST(FogPresetsTest, MissingFileYieldsEmptyPresets) {
    bool ok = true;
    FogPresets p = FogPresets::Load("/tmp/definitely-not-here-12345.json", &ok);
    EXPECT_FALSE(ok);
    ResolvedFog r = p.Resolve(1, 0.02f, 0.0f, 0);
    EXPECT_FALSE(r.density_authored);      // falls back to derived density
    EXPECT_FLOAT_EQ(r.density, 0.02f);     // == derived value passed in
}

TEST(FogPresetsTest, ResolveAppliesAuthoredDensityForMission) {
    bool ok = false;
    // Minimal JSON: default entry empty; mission 3 overrides density only.
    const char* json = "{\"default\":{},\"missions\":{\"3\":{\"density\":0.05}}}";
    FILE* f = fopen("/tmp/fog_test_preset.json", "wb");
    ASSERT_TRUE(f != nullptr);
    fwrite(json, 1, strlen(json), f);
    fclose(f);

    FogPresets p = FogPresets::Load("/tmp/fog_test_preset.json", &ok);
    ASSERT_TRUE(ok);

    ResolvedFog m3 = p.Resolve(3, 0.02f, 0.0f, 0);
    EXPECT_TRUE(m3.density_authored);
    EXPECT_FLOAT_EQ(m3.density, 0.05f);

    // Other missions keep the derived fallback.
    ResolvedFog m1 = p.Resolve(1, 0.02f, 0.0f, 0);
    EXPECT_FALSE(m1.density_authored);
    EXPECT_FLOAT_EQ(m1.density, 0.02f);
}
