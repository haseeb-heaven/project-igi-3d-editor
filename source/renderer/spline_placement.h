#pragma once
#include "../pch.h"
#include <vector>

// Arc-length placement math for road/bridge spline segments (issue #69).
//
// Pure logic, no GL — unit-testable standalone. The renderer samples one of the
// two track builders here into a SplinePlacementTrack, then lays stretched deck
// tiles at even ARC-LENGTH intervals across the WHOLE spline (the old path
// restarted the count at every knot pair, which bunched tiles on curves).
//
// Track sources (same authored-orientation policy as train placement, #59):
//   * Authored: igi::RailroadPath (waypoint-frame Hermite tangents, sub_4E4530)
//     when any waypoint carries a non-zero authored rotation.
//   * Legacy: chord-tangent Hermite per knot span (previous renderer behavior).

namespace igi {

struct SplineTrackSample {
    glm::dvec3 pos{0.0};
    glm::vec3 forward{1.0f, 0.0f, 0.0f}; // unit; zero-roll frame X column
};

// Zero-roll track frame matching RailroadPath::Evaluate: local X down the rail,
// side = (-f.y, f.x, 0) normalized (fallback +Y), up = forward x side.
struct SplineFrame {
    glm::vec3 forward{1.0f, 0.0f, 0.0f};
    glm::vec3 side{0.0f, 1.0f, 0.0f};
    glm::vec3 up{0.0f, 0.0f, 1.0f};
};
SplineFrame MakeTrackFrame(const glm::vec3& forward);

// Tile-count rule from renderer_splines.cpp: the count that lands closest to the
// tile's natural (authored-extent) length, at least 1, at most max_tiles. The
// chosen count then stretches every tile uniformly so they butt end-to-end.
int ChooseTileCount(float run_length, float tile_natural_len, int max_tiles);

// Cubic Hermite point with chord tangents clamped to the interval length
// (legacy fallback shape — the pre-#69 renderer curve, preserved verbatim).
glm::vec3 SplineHermitePoint(float t,
    const glm::vec3& p0, const glm::vec3& p1,
    const glm::vec3& t0, const glm::vec3& t1);

class SplinePlacementTrack {
public:
    // Appends one sample; arc distance accumulates from the previous sample.
    // Call in order along the spline.
    void AddSample(const glm::dvec3& pos, const glm::vec3& forward);

    // Total arc length (0 for an empty/degenerate track).
    float TotalLength() const { return total_len_; }
    size_t SampleCount() const { return samples_.size(); }

    // Position + forward at an arc distance, clamped to [0, TotalLength()].
    // Between samples: position lerps; forward lerps then renormalizes (falls
    // back to the nearer sample's forward for near-zero lengths).
    SplineTrackSample SampleAtArc(float arc) const;

private:
    std::vector<SplineTrackSample> samples_;
    std::vector<float> cum_;      // cumulative arc at each sample, cum_[0] == 0
    float total_len_ = 0.0f;
};

// Legacy builder: chord-tangent Hermite over consecutive knot positions,
// `samples_per_span` samples per span (minimum 2). Matches the pre-#69 curve.
SplinePlacementTrack BuildLegacyTrack(const std::vector<glm::vec3>& knots,
                                      int samples_per_span = 32);

} // namespace igi
