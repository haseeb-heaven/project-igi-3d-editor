#include "spline_placement.h"

#include <algorithm>
#include <cmath>

namespace igi {

SplineFrame MakeTrackFrame(const glm::vec3& forward) {
    SplineFrame f;
    f.forward = glm::length(forward) > 1e-6f ? forward / glm::length(forward)
                                             : glm::vec3(1.0f, 0.0f, 0.0f);
    // Zero-roll: side axis horizontal (world Z up), matching RailroadPath::Evaluate.
    glm::vec3 side(-f.forward.y, f.forward.x, 0.0f);
    if (glm::length(side) < 1e-6f) {
        // Near-vertical fallback (matches the renderer's previous fallback).
        side = glm::cross(glm::vec3(1.0f, 0.0f, 0.0f), f.forward);
    }
    if (glm::length(side) < 1e-6f) side = glm::vec3(0.0f, 1.0f, 0.0f);
    f.side = side / glm::length(side);
    f.up = glm::cross(f.forward, f.side);
    return f;
}

int ChooseTileCount(float run_length, float tile_natural_len, int max_tiles) {
    if (tile_natural_len < 1.0f) tile_natural_len = 1.0f;
    int n = static_cast<int>(std::lround(run_length / tile_natural_len));
    return std::clamp(n, 1, max_tiles);
}

glm::vec3 SplineHermitePoint(float t,
    const glm::vec3& p0, const glm::vec3& p1,
    const glm::vec3& t0, const glm::vec3& t1) {
    const float t2 = t * t, t3 = t2 * t;
    return (2.0f * t3 - 3.0f * t2 + 1.0f) * p0
         + (t3 - 2.0f * t2 + t) * t0
         + (-2.0f * t3 + 3.0f * t2) * p1
         + (t3 - t2) * t1;
}

void SplinePlacementTrack::AddSample(const glm::dvec3& pos, const glm::vec3& forward) {
    if (!samples_.empty()) {
        total_len_ += static_cast<float>(glm::length(pos - samples_.back().pos));
    }
    samples_.push_back({pos, forward});
    cum_.push_back(total_len_);
}

SplineTrackSample SplinePlacementTrack::SampleAtArc(float arc) const {
    if (samples_.empty()) return {};
    if (samples_.size() == 1 || arc <= 0.0f) return samples_.front();
    if (arc >= total_len_) return samples_.back();

    // First sample with cum > arc; interpolate between it and its predecessor.
    auto it = std::upper_bound(cum_.begin(), cum_.end(), arc);
    const size_t hi = static_cast<size_t>(it - cum_.begin());
    const size_t lo = hi - 1;

    const float span = cum_[hi] - cum_[lo];
    const double t_a = span > 1e-9 ? static_cast<double>((arc - cum_[lo]) / span) : 0.0;

    SplineTrackSample out;
    out.pos = samples_[lo].pos + (samples_[hi].pos - samples_[lo].pos) * t_a;
    out.forward = samples_[lo].forward +
                  (samples_[hi].forward - samples_[lo].forward) * static_cast<float>(t_a);
    const float len = glm::length(out.forward);
    out.forward = len > 1e-6f ? out.forward / len : samples_[lo].forward;
    return out;
}

SplinePlacementTrack BuildLegacyTrack(const std::vector<glm::vec3>& knots,
                                      int samples_per_span) {
    SplinePlacementTrack track;
    if (knots.size() < 2) {
        if (!knots.empty()) track.AddSample(knots[0], glm::vec3(1.0f, 0.0f, 0.0f));
        return track;
    }
    if (samples_per_span < 2) samples_per_span = 2;

    for (size_t i = 0; i + 1 < knots.size(); ++i) {
        const size_t si = i, ei = i + 1;
        const size_t pi = (i > 0) ? i - 1 : si;
        const size_t ni = (i + 2 < knots.size()) ? i + 2 : ei;

        const glm::vec3 p0 = knots[si], p1 = knots[ei];
        const glm::vec3 p_prev = knots[pi], p_next = knots[ni];

        glm::vec3 tan0 = (p1 - p_prev) * 0.5f;
        glm::vec3 tan1 = (p_next - p0) * 0.5f;

        // Clamp tangent magnitude to the interval length (legacy overshoot guard).
        const float interval_len = glm::length(p1 - p0);
        const float t0len = glm::length(tan0);
        const float t1len = glm::length(tan1);
        if (t0len > interval_len) tan0 *= interval_len / t0len;
        if (t1len > interval_len) tan1 *= interval_len / t1len;

        for (int s = (i == 0 ? 0 : 1); s <= samples_per_span; ++s) {
            const float t = static_cast<float>(s) / static_cast<float>(samples_per_span);
            const glm::vec3 p = SplineHermitePoint(t, p0, p1, tan0, tan1);
            // Legacy orientation source is the chord direction; the derivative of
            // the Hermite at t is the local tangent — use it so slope pitch reads
            // smoothly instead of stepping at samples.
            glm::vec3 d = p1 - p0;
            // Analytic derivative: h'(t) terms with the same basis as above.
            const float d00 = 6.0f * t * t - 6.0f * t;
            const float d10 = 3.0f * t * t - 4.0f * t + 1.0f;
            const float d01 = -d00;
            const float d11 = 3.0f * t * t - 2.0f * t;
            d = p0 * d00 + tan0 * d10 + p1 * d01 + tan1 * d11;
            if (glm::length(d) < 1e-6f) d = (p1 - p0);
            track.AddSample(p, d);
        }
    }
    return track;
}

} // namespace igi
