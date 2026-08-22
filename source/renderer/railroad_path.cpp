#include "railroad_path.h"

#include <algorithm>
#include <cmath>

namespace igi {

namespace {

constexpr int kAreasPerSegment = 20; // open-igi RailroadPath.cs BuildSamples()

// First column of Rz(gamma) * Ry(beta) * Rx(alpha) (open-igi Matrix3f.FromEngineEulerAngles).
// Derivation: first column of Rx is (1,0,0); Ry*(1,0,0)^T = (cb, 0, -sb);
// Rz*(cb, 0, -sb)^T = (cg*cb, sg*cb, -sb).
glm::dvec3 EngineEulerFirstColumn(float alpha, float beta, float gamma) {
    (void)alpha; // alpha only affects columns 2/3
    const double cb = std::cos(static_cast<double>(beta));
    const double sb = std::sin(static_cast<double>(beta));
    const double cg = std::cos(static_cast<double>(gamma));
    const double sg = std::sin(static_cast<double>(gamma));
    return {cg * cb, sg * cb, -sb};
}

} // namespace

std::optional<RailroadPath> RailroadPath::Build(
    const std::vector<RailroadWaypoint>& waypoints,
    bool flipped,
    double initial_position,
    double offset_x,
    double offset_y) {
    if (waypoints.size() < 2) {
        return std::nullopt;
    }
    std::vector<glm::dvec3> points;
    std::vector<glm::dvec3> tangents;
    points.reserve(waypoints.size());
    tangents.reserve(waypoints.size());
    for (const auto& wp : waypoints) {
        points.push_back(wp.position);
        // sub_4E4530 reads elements [0,3,6]: the local-X column of the waypoint frame.
        tangents.push_back(EngineEulerFirstColumn(wp.alpha, wp.beta, wp.gamma));
    }
    return RailroadPath(std::move(points), std::move(tangents),
                        flipped, initial_position, offset_x, offset_y);
}

void RailroadPath::Evaluate(double scalar_position,
                            glm::dvec3& position,
                            glm::dmat3& orientation) const {
    const double total = samples_.back().distance;
    // Retail signed end-relative convention: negative measures back from the last knot.
    double distance = scalar_position < 0.0 ? total + scalar_position : scalar_position;
    distance = std::clamp(distance, 0.0, total);

    // Binary search for the first sample with distance >= target (upper bound), then
    // interpolate between its predecessor and itself — mirrors open-igi exactly.
    auto it = std::upper_bound(samples_.begin(), samples_.end(), distance,
                               [](double d, const SplineSample& s) { return d < s.distance; });
    int upper = static_cast<int>(it - samples_.begin());
    upper = std::clamp(upper, 1, static_cast<int>(samples_.size()) - 1);

    const SplineSample& before = samples_[static_cast<size_t>(upper) - 1];
    const SplineSample& after = samples_[static_cast<size_t>(upper)];
    const double span = after.distance - before.distance;
    const double amount = span > 0.0 ? (distance - before.distance) / span : 0.0;
    const double t = before.segment == after.segment
        ? before.t + ((after.t - before.t) * amount)
        : after.t * amount;

    glm::dvec3 derivative;
    EvaluateHermite(after.segment, t, position, derivative);

    glm::dvec3 forward = flipped_ ? -derivative : derivative;
    double forward_length = glm::length(forward);
    if (forward_length <= 1e-9) {
        forward = glm::dvec3(1.0, 0.0, 0.0);
        forward_length = 1.0;
    }
    forward /= forward_length;

    // Zero-roll frame matching sub_4E4530/sub_4FEC10: local X down the rail.
    glm::dvec3 side(-forward.y, forward.x, 0.0);
    const double side_length = glm::length(side);
    side = side_length > 1e-9 ? side / side_length : glm::dvec3(0.0, 1.0, 0.0);
    const glm::dvec3 up = glm::cross(forward, side);

    orientation = glm::dmat3(forward, side, up);

    const glm::dvec3 right = orientation[1]; // frame side axis (Displacement X)
    const glm::dvec3 third = orientation[2]; // frame third column (Displacement Y)
    position += right * offset_x_ + third * offset_y_;
}

void RailroadPath::BuildSamples() {
    samples_.clear();
    samples_.push_back({0.0, 0, 0.0});
    glm::dvec3 previous = points_.front();
    double distance = 0.0;
    for (int segment = 0; segment + 1 < static_cast<int>(points_.size()); ++segment) {
        for (int area = 1; area <= kAreasPerSegment; ++area) {
            const double t = area / static_cast<double>(kAreasPerSegment);
            glm::dvec3 point;
            glm::dvec3 unused;
            EvaluateHermite(segment, t, point, unused);
            distance += glm::length(point - previous);
            samples_.push_back({distance, segment, t});
            previous = point;
        }
    }
}

void RailroadPath::EvaluateHermite(int segment, double t,
                                   glm::dvec3& position, glm::dvec3& derivative) const {
    const glm::dvec3 from = points_[static_cast<size_t>(segment)];
    const glm::dvec3 to = points_[static_cast<size_t>(segment) + 1];
    const glm::dvec3 chord_vector = to - from;
    const double chord = glm::length(chord_vector);
    glm::dvec3 tangent0 = tangents_[static_cast<size_t>(segment)];
    glm::dvec3 tangent1 = tangents_[static_cast<size_t>(segment) + 1];
    tangent0 *= chord;
    tangent1 *= chord;

    const double t2 = t * t;
    const double t3 = t2 * t;
    const double h00 = (2.0 * t3) - (3.0 * t2) + 1.0;
    const double h10 = t3 - (2.0 * t2) + t;
    const double h01 = (-2.0 * t3) + (3.0 * t2);
    const double h11 = t3 - t2;
    position = (from * h00) + (tangent0 * h10) + (to * h01) + (tangent1 * h11);

    const double d00 = (6.0 * t2) - (6.0 * t);
    const double d10 = (3.0 * t2) - (4.0 * t) + 1.0;
    const double d01 = (6.0 * t) - (6.0 * t2);
    const double d11 = (3.0 * t2) - (2.0 * t);
    derivative = (from * d00) + (tangent0 * d10) + (to * d01) + (tangent1 * d11);
}

} // namespace igi
