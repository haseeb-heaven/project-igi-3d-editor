// test_heli_preview.cpp — Unit tests for the retail-parity helicopter preview
// constants and collective-driven rotor speeds (issue #60).
//
// Fixture-independent: exercises only heli_preview math + the authored
// "Original Thrust" lookup against synthetic LevelObject data.
#include <gtest/gtest.h>
#include "renderer/heli_preview.h"
#include "renderer/../level/level_objects.h"

using namespace heli_preview;

TEST(HeliPreviewTest, PhysicsRecordMatchesRetailQvmValues) {
    // Verified values scanned from physicsobj/helis/{bell,mil}/HELI.QVM
    // (identical in both scripts) and matching open-igi's
    // HelicopterPhysicsDefinition default (CutsceneRuntime.cs ~1077).
    const PhysicsRecord& rec = GetPhysics();
    EXPECT_FLOAT_EQ(rec.mass, 3000.0f);
    EXPECT_FLOAT_EQ(rec.dimensions[0], 2.2f);
    EXPECT_FLOAT_EQ(rec.dimensions[1], 7.8f);
    EXPECT_FLOAT_EQ(rec.dimensions[2], 3.0f);
    EXPECT_FLOAT_EQ(rec.torque[0], 50.0f);
    EXPECT_FLOAT_EQ(rec.torque[1], 50.0f);
    EXPECT_FLOAT_EQ(rec.torque[2], 50.0f);
    EXPECT_FLOAT_EQ(rec.smoothing[0], 0.4f);
    EXPECT_FLOAT_EQ(rec.high_collective_step, 0.027f);
    EXPECT_FLOAT_EQ(rec.low_collective_step, 0.003f);
}

TEST(HeliPreviewTest, KnownRetailRotorAndBodyModels) {
    // Model ids embedded in the retail physicsobj QVM scripts.
    EXPECT_TRUE(IsKnownRotorModel("711_01_1"));
    EXPECT_TRUE(IsKnownRotorModel("711_02_1"));
    EXPECT_TRUE(IsKnownRotorModel("700_03_1"));
    EXPECT_TRUE(IsKnownRotorModel("700_04_1"));
    EXPECT_TRUE(IsKnownRotorModel("700_02_1"));
    EXPECT_TRUE(IsKnownHeliBodyModel("709_01_1"));
    EXPECT_TRUE(IsKnownHeliBodyModel("700_01_1"));
    // Non-rotor attachment (e.g. glass canopy) must not classify.
    EXPECT_FALSE(IsKnownRotorModel("999_99_9"));
}

TEST(HeliPreviewTest, CollectiveClampsToUnitRange) {
    EXPECT_FLOAT_EQ(NormalizeCollective(-0.5f), 0.0f);
    EXPECT_FLOAT_EQ(NormalizeCollective(0.0f), 0.0f);
    EXPECT_FLOAT_EQ(NormalizeCollective(0.65f), 0.65f);
    EXPECT_FLOAT_EQ(NormalizeCollective(1.0f), 1.0f);
    EXPECT_FLOAT_EQ(NormalizeCollective(4.0f), 1.0f); // vanilla never authors > 1
}

TEST(HeliPreviewTest, RotorSpeedIsProportionalToCollective) {
    // Retail: RotorPhase += Thrust each 30 Hz tick -> speed scales linearly;
    // zero collective stops the rotors entirely, as in-game.
    EXPECT_FLOAT_EQ(MainRotorAngularSpeed(0.0f), 0.0f);
    EXPECT_FLOAT_EQ(TailRotorAngularSpeed(0.0f), 0.0f);
    EXPECT_NEAR(MainRotorAngularSpeed(0.5f), MainRotorAngularSpeed(1.0f) * 0.5f, 1e-4f);
    EXPECT_NEAR(TailRotorAngularSpeed(0.5f), TailRotorAngularSpeed(1.0f) * 0.5f, 1e-4f);
    // Full-collective magnitudes keep the historical preview rates; tail spins
    // opposite the main rotor.
    EXPECT_FLOAT_EQ(MainRotorAngularSpeed(1.0f), 15.0f);
    EXPECT_FLOAT_EQ(TailRotorAngularSpeed(1.0f), -25.0f);
}

namespace {

LevelObject MakeHeli(const std::vector<std::string>& tokens) {
    LevelObject o;
    o.type = "Heli";
    o.argTokens = tokens;
    return o;
}

LevelObject MakeDecl(const std::vector<std::string>& tokens) {
    LevelObject o;
    o.type = "Task_DeclareParameters";
    o.argTokens = tokens;
    return o;
}

} // namespace

TEST(HeliPreviewTest, LookupAuthoredCollectiveResolvesByName) {
    DeclarationIndex cache;
    // Declared layout: Heli -> Position(ObjectPos=3 tokens), Heading(Real32),
    // Original Thrust(Real32), Model(String16). Object argTokens: [id, type,
    // name, x, y, z, heading, thrust, model] — the ObjectPos width shifts the
    // Original Thrust token to offset 3+3+1 = 7.
    std::vector<LevelObject> objects = {
        MakeDecl({"Heli", "Position", "ObjectPos", "Heading", "Real32",
                  "Original Thrust", "Real32", "Model", "String16"}),
        MakeHeli({"1500", "\"Heli\"", "\"heli_1\"", "100", "200", "300",
                  "0", "0.75", "\"709_01_1\""}),
    };
    float thrust = LookupAuthoredCollective(&objects, objects[1], cache);
    ASSERT_GE(thrust, 0.0f) << "authored collective should resolve by name";
    EXPECT_FLOAT_EQ(thrust, 0.75f);
    // Second call uses the cached declaration resolution.
    EXPECT_FLOAT_EQ(LookupAuthoredCollective(&objects, objects[1], cache), 0.75f);
}

TEST(HeliPreviewTest, LookupAuthoredCollectiveMissingDeclarationReturnsUnknown) {
    DeclarationIndex cache;
    std::vector<LevelObject> objects = { MakeHeli({"1", "\"Heli\"", "\"x\""}) };
    EXPECT_FLOAT_EQ(LookupAuthoredCollective(&objects, objects[0], cache), -1.0f);
}
