#pragma once
#include <glm/glm.hpp>
#include <string>
#include <vector>

// Light-fixture inference — C++ port of open-igi src/OpenIGI.Game/World/LightFixture.cs +
// LightFixtureExtractor.cs (+ the Store), written from retail evidence (see issue #63).
//
// Stance inherited from open-igi, verbatim in spirit:
//   * I.G.I. declares NO point lights in level data. A fixture here is a PROPOSAL built from
//     two stacked inferences — (1) which textures the artists treated as light sources,
//     (2) where those textures are drawn — and neither step proves the thing found is a lamp.
//     A reflective sign coming out as a candidate is normal output, not a bug.
//   * Everything but the position is soft: radius/intensity are plausible defaults from a
//     formula with a tunable constant, meant to be edited afterwards.
//   * Position IS a fact: the area-weighted centroid of the cluster's triangles transformed
//     into world space by the same matrix the renderer draws the object with.

#include "mef_native.h"
#include <glm/gtc/matrix_transform.hpp>

namespace igi {

// How far apart two emitter triangles may be and still belong to one fixture (metres).
// A GAP, not a size: triangles join transitively, so a long tube stays one fixture while
// lamps across a corridor split. 1.5 m errs towards splitting, the safer direction.
inline constexpr float kDefaultClusterDistance = 1.5f;

// Reach multiplier: radius = extent * scale * sqrt(luminance). 12 puts a typical wall
// fixture (a quarter-metre across) in charge of the three-four metres around it.
inline constexpr float kDefaultRadiusScale = 12.0f;

// Floor on any proposed fixture's reach (metres) so tiny housings still light something.
inline constexpr float kMinimumRadius = 2.0f;

// Editor world coordinates are retail game units; open-igi's extractor works in metres.
// All clustering/extent/radius math runs in metres internally; reported positions are
// converted back to game units so viewport gizmos line up with placed objects.
inline constexpr float kGameUnitsPerMetre = 40.96f;

struct LightFixture {
    std::string texture;      // dominant emitter texture of the cluster (pack name)
    glm::dvec3 position{0.0}; // area-weighted centroid, world space
    double radius = 0.0;      // proposed reach in metres
    glm::dvec3 color{1.0};    // linear, brightest channel ~1; white when unknown
    double area = 0.0;        // emitting surface area (m^2)
    double extent = 0.0;      // radius of the cluster's own bounding sphere (m)
    int triangle_count = 0;   // carried for review: one stray triangle is suspect
    bool ignored = false;     // reviewer false-positive flag (survives re-extraction by key)

    // Rec. 709 luminance — same weights an emissive-texture survey measures pages with.
    double Luminance() const {
        return 0.2126 * color.x + 0.7152 * color.y + 0.0722 * color.z;
    }
    // Emitting area weighted by luminance — deliberately relative units.
    double Intensity() const { return area * Luminance(); }
    // Stable key for reviewer flags across re-extractions.
    std:: string IgnoreKey() const;
};

// One drawable model instance: re-parsed MEF geometry (CPU side), the exact model matrix
// the renderer draws it with (see BuildModelMatrix below), and the material-slot ->
// texture-name mapping for THIS instance (from the DAT texture map).
struct PlacedMeshInput {
    const ::ParsedGeometry* geometry = nullptr; // re-parsed MEF (CPU side)
    glm::mat4 model_matrix{1.0f};
    std::vector<std::string> slot_textures;         // indexed by RenderBlock::materialSlot
};

// Composes the placement matrix EXACTLY as renderer_objects.cpp draws placed objects:
// translate(pos) * rotateZ(yaw) * rotateX(pitch) * rotateY(roll) * scale(40.96 * scale)
// (the non-door branch — doors are not lamp hosts). Keeping this next to the extractor
// guarantees a fixture sits where the viewport draws its host lamp.
glm::mat4 BuildModelMatrix(const glm::dvec3& pos, const glm::dvec3& rot, float obj_scale);

// Proposes a fixture for every cluster of emitter triangles among the given meshes.
// Emitter-triangle selection: every triangle of every RenderBlock whose resolved texture
// name is in `emitter_textures` (case-insensitive). Collision-fallback geometry is skipped
// (its UVs are fabricated; it never draws real materials). Results sorted strongest first.
std::vector<LightFixture> ProposeFixtures(
    const std::vector<PlacedMeshInput>& meshes,
    const std::vector<std::string>& emitter_textures,
    float cluster_distance = kDefaultClusterDistance,
    float radius_scale = kDefaultRadiusScale);

// Emitter-texture candidates when no human-reviewed survey exists yet.
// DOCUMENTED HEURISTIC (to be replaced by the lightmap-brightness survey): lamp textures in
// the retail packs are named after the lamp model whose housing draws them (open-igi cites
// level 1's dominant emitter as "236_01_1", baked pure white on thirty uses). Candidates are
// therefore textures referenced by exactly ONE model in the given set whose name matches a
// known fixture-model prefix family, plus any name containing a fixture keyword. Soft input,
// soft output: everything is a candidate for review.
std::vector<std::string> SuggestEmitterTexturesByHeuristic(
    const std::vector<std::string>& all_texture_names);

} // namespace igi
