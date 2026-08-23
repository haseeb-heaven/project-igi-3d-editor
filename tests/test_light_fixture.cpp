// Unit tests for light-fixture inference (issue #63) — port of open-igi
// LightFixture.cs / LightFixtureExtractor.cs semantics. Pure logic: no game assets.
#include <gtest/gtest.h>
#include <cmath>
#include <math.h>
#include "../source/renderer/light_fixture.h"
#include "../source/renderer/light_fixture_store.h"
#include "../source/renderer/mef_native.h"

using namespace igi;

namespace {

// Builds a ParsedGeometry with `count` unit quads (2 triangles each) centred at
// (cx, cy, cz), lying in the XY plane, all drawing material slot `slot`.
::ParsedGeometry QuadGeometry(int count, double cx, double cy, double cz, int slot = 0) {
    ::ParsedGeometry geo;
    geo.fromRenderMesh = true;
    ::ParsedGeometry::RenderBlock block;
    block.triangleStart = 0;
    block.triangleCount = 0;
    block.materialSlot = slot;
    for (int i = 0; i < count; ++i) {
        // 1m x 1m quad centred at (cx + i*0.01, cy, cz) — tiny offset so multiple quads
        // chain (0.01m apart, well under the 1.5m join distance).
        const double ox = cx + i * 0.01;
        const uint32_t base = static_cast<uint32_t>(geo.vertices.size());
        auto add = [&](double x, double y, double z) {
            ::RenderVertex v;
            v.pos = glm::vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
            geo.vertices.push_back(v);
        };
        add(ox - 0.5, cy - 0.5, cz);
        add(ox + 0.5, cy - 0.5, cz);
        add(ox + 0.5, cy + 0.5, cz);
        add(ox - 0.5, cy + 0.5, cz);
        geo.triangles.push_back({base, base + 1, base + 2});
        geo.triangles.push_back({base, base + 2, base + 3});
        block.triangleCount += 2;
    }
    geo.renderBlocks.push_back(block);
    return geo;
}

PlacedMeshInput Placed(const ParsedGeometry& geo, glm::mat4 matrix,
                       std::vector<std::string> slots) {
    PlacedMeshInput in;
    in.geometry = &geo;
    in.model_matrix = matrix;
    in.slot_textures = std::move(slots);
    return in;
}

} // namespace

TEST(LightFixtureTest, SingleQuadProducesOneFixtureAtQuadCentre) {
    ParsedGeometry geo = QuadGeometry(1, 10.0, 20.0, 30.0);
    auto fixtures = ProposeFixtures(
        {Placed(geo, glm::mat4(1.0f), {"lamp_tex"})}, {"lamp_tex"});
    ASSERT_EQ(fixtures.size(), 1u);
    EXPECT_NEAR(fixtures[0].position.x, 10.0, 1e-3);
    EXPECT_NEAR(fixtures[0].position.y, 20.0, 1e-3);
    EXPECT_NEAR(fixtures[0].position.z, 30.0, 1e-3);
    EXPECT_EQ(fixtures[0].triangle_count, 2);
    // Geometry units are game units: a 1-unit quad is 1/40.96 m per side.
    EXPECT_NEAR(fixtures[0].area, std::pow(1.0 / kGameUnitsPerMetre, 2), 1e-8);
    EXPECT_EQ(fixtures[0].texture, "lamp_tex");
}

TEST(LightFixtureTest, RadiusFloorAppliesToSmallFixtures) {
    // 1 m quad, white light: reach = 0.707 * 12 * 1 = 8.49 — above the floor.
    // A tiny quad (0.05 m) gives reach ~0.42 -> floored to kMinimumRadius = 2.
    ParsedGeometry tiny = QuadGeometry(1, 0.0, 0.0, 0.0);
    for (auto& v : tiny.vertices) v.pos *= 0.05f;
    auto fixtures = ProposeFixtures(
        {Placed(tiny, glm::mat4(1.0f), {"t"})}, {"t"});
    ASSERT_EQ(fixtures.size(), 1u);
    EXPECT_NEAR(fixtures[0].radius, static_cast<double>(kMinimumRadius), 1e-6);
}

TEST(LightFixtureTest, DistantQuadsSplitIntoTwoFixtures) {
    // Two quads 10 m apart: gap >> kDefaultClusterDistance -> two proposals.
    ParsedGeometry a = QuadGeometry(1, 0.0, 0.0, 0.0);
    ParsedGeometry b = QuadGeometry(1, 10.0 * kGameUnitsPerMetre, 0.0, 0.0); // 10 real metres away
    auto fixtures = ProposeFixtures(
        {Placed(a, glm::mat4(1.0f), {"t"}), Placed(b, glm::mat4(1.0f), {"t"})},
        {"t"});
    ASSERT_EQ(fixtures.size(), 2u);
}

TEST(LightFixtureTest, ChainedQuadsStayOneFixture) {
    // 10 quads strung along X with 1 m steps: transitive joining keeps them together
    // (the gap rule — a long tube is one fixture).
    ParsedGeometry strip = QuadGeometry(10, 0.0, 0.0, 0.0);
    for (int i = 0; i < 10; ++i) {
        // reposition: quad i centred at x = i * 1.0 (each consecutive pair 0.99m apart
        // edge-to-edge, centroids 1.0m apart — inside the 1.5m join distance).
        for (size_t v = i * 4; v < (i + 1) * 4; ++v) {
            strip.vertices[v].pos.x += static_cast<float>(i);
        }
    }
    auto fixtures = ProposeFixtures(
        {Placed(strip, glm::mat4(1.0f), {"t"})}, {"t"});
    ASSERT_EQ(fixtures.size(), 1u);
    EXPECT_EQ(fixtures[0].triangle_count, 20);
}

TEST(LightFixtureTest, WorldTransformMovesCentroidLikeRenderer) {
    // Rotate 90 deg about Z and translate: centroid must follow the composed matrix
    // exactly — the fixture has to sit where the viewport draws the lamp.
    ParsedGeometry geo = QuadGeometry(1, 0.0, 0.0, 0.0);
    glm::mat4 m = BuildModelMatrix(glm::dvec3(100.0, 200.0, 300.0),
                                   glm::dvec3(0.0, 0.0, 1.5707963), 1.0f);
    auto fixtures = ProposeFixtures({Placed(geo, m, {"t"})}, {"t"});
    ASSERT_EQ(fixtures.size(), 1u);
    // Rotation about Z maps (0,0,0) to the translation — centroid at the object origin.
    EXPECT_NEAR(fixtures[0].position.x, 100.0, 1e-3);
    EXPECT_NEAR(fixtures[0].position.y, 200.0, 1e-3);
    EXPECT_NEAR(fixtures[0].position.z, 300.0, 1e-3);
    // Scale check: 1 m model quad stays 1 m in world metres (units cancel).
    EXPECT_NEAR(fixtures[0].extent, 0.70710678, 1e-3);
}

TEST(LightFixtureTest, NonEmitterTexturesProduceNothing) {
    ParsedGeometry geo = QuadGeometry(1, 0.0, 0.0, 0.0);
    auto fixtures = ProposeFixtures(
        {Placed(geo, glm::mat4(1.0f), {"wall"})}, {"lamp_tex"});
    EXPECT_TRUE(fixtures.empty());
}

TEST(LightFixtureTest, CollisionFallbackGeometryIsSkipped) {
    ParsedGeometry geo = QuadGeometry(1, 0.0, 0.0, 0.0);
    geo.fromRenderMesh = false; // XTVC/ECFC fallback — fabricated UVs, no materials
    auto fixtures = ProposeFixtures(
        {Placed(geo, glm::mat4(1.0f), {"t"})}, {"t"});
    EXPECT_TRUE(fixtures.empty());
}

TEST(LightFixtureTest, StrongestFirstOrdering) {
    // One 4 m^2 emitter and one 1 m^2 emitter, same texture, far apart: the bigger one
    // (higher intensity) must sort first.
    ParsedGeometry big = QuadGeometry(4, 0.0, 0.0, 0.0);   // chained strip, ~4 m^2 total
    for (int i = 0; i < 4; ++i)
        for (size_t v = i * 4; v < (i + 1) * 4; ++v)
            big.vertices[v].pos.x += static_cast<float>(i);
    ParsedGeometry small = QuadGeometry(1, 50.0 * kGameUnitsPerMetre, 0.0, 0.0); // 50 real metres away
    auto fixtures = ProposeFixtures(
        {Placed(big, glm::mat4(1.0f), {"t"}), Placed(small, glm::mat4(1.0f), {"t"})},
        {"t"});
    ASSERT_EQ(fixtures.size(), 2u);
    EXPECT_GT(fixtures[0].area, fixtures[1].area);
    EXPECT_NEAR(fixtures[1].position.x, 50.0 * kGameUnitsPerMetre, 1e-2);
}

TEST(LightFixtureTest, LuminanceAndIntensityMath) {
    LightFixture f;
    f.color = glm::dvec3(1.0, 0.5, 0.0);
    f.area = 2.0;
    EXPECT_NEAR(f.Luminance(), 0.2126 + 0.7152 * 0.5, 1e-9);
    EXPECT_NEAR(f.Intensity(), 2.0 * f.Luminance(), 1e-9);
}

TEST(LightFixtureStoreTest, IgnoreFlagSurvivesReExtractionByKey) {
    LightFixtureStore& store = LightFixtureStore::Get();
    LightFixture a;
    a.texture = "236_01_1";
    a.position = glm::dvec3(1.0, 2.0, 3.0);
    LightFixture same_place = a;
    LightFixture elsewhere = a;
    elsewhere.position = glm::dvec3(9.0, 9.0, 9.0);

    store.SetFixtures({a, elsewhere});
    store.SetIgnored(0, true);
    EXPECT_TRUE(store.IsIgnored(0));
    EXPECT_FALSE(store.IsIgnored(1));
    EXPECT_EQ(store.ActiveCount(), 1);

    // Re-extraction at the same place: flag re-applied; the other lamp stays active.
    store.SetFixtures({same_place, elsewhere});
    EXPECT_TRUE(store.IsIgnored(0));
    EXPECT_FALSE(store.IsIgnored(1));
    EXPECT_EQ(store.ActiveCount(), 1);

    // Clear keeps reviewer decisions (they belong to the level, not the run).
    store.Clear();
    store.SetFixtures({same_place});
    EXPECT_TRUE(store.IsIgnored(0));
    store.SetIgnored(0, false); // cleanup for other tests
    store.Clear();
}
