#pragma once
#include <glm/glm.hpp>
#include <optional>
#include <vector>

// Railroad spline evaluation — C++ port of open-igi src/OpenIGI.Game/World/RailroadPath.cs
// (written from igi2.pdb symbol evidence: sub_4E4530 waypoint-frame tangents).
//
// Resolves a retail Train task's scalar rail position onto its SplineObj waypoints.
//
// Key retail behaviors reproduced here:
//   * Waypoint tangent = local-X column of the waypoint's orientation frame built from
//     engine Euler angles as Rz(gamma) * Ry(beta) * Rx(alpha); sub_4E4530 reads elements
//     [0,3,6] of that matrix (the first column).
//   * Signed end-relative scalar convention: positive values measure arc length from the
//     first knot, NEGATIVE values measure backwards from the last knot. Mission 1's train
//     starts at -971400 on a ~4.7-million-unit rail.
//   * Cubic Hermite interpolation per segment with tangents scaled by chord length;
//     20 arc-length samples per segment; binary search over the cumulative table.
//   * Zero-roll frame: forward down the rail, side = (-f.y, f.x, 0) normalized,
//     up = forward x side (matching sub_4E4530/sub_4FEC10).
//   * Flip Direction negates the derivative; Displacement X/Y are applied along the
//     frame's side axis / third column respectively.

namespace igi {

struct RailroadWaypoint {
    glm::dvec3 position{0.0};
    // Engine Euler angles (radians): alpha=X, beta=Y, gamma=Z applied as Rz*Ry*Rx.
    float alpha = 0.0f;
    float beta = 0.0f;
    float gamma = 0.0f;
};

class RailroadPath {
public:
    // Returns nullopt when fewer than two usable waypoints are supplied.
    static std::optional<RailroadPath> Build(
        const std::vector<RailroadWaypoint>& waypoints,
        bool flipped,
        double initial_position,
        double offset_x,
        double offset_y);

    bool Flipped() const { return flipped_; }
    double InitialPosition() const { return initial_position_; }
    double OffsetX() const { return offset_x_; }
    double OffsetY() const { return offset_y_; }
    double TotalLength() const { return samples_.empty() ? 0.0 : samples_.back().distance; }

    // Evaluates the rail at `scalar_position` using the retail signed convention.
    // `orientation` columns: [0]=forward (local X down the rail), [1]=side,
    // [2]=frame third column. Displacement X/Y are added along [1] and [2].
    void Evaluate(double scalar_position,
                  glm::dvec3& position,
                  glm::dmat3& orientation) const;

private:
    struct SplineSample {
        double distance = 0.0;
        int segment = 0;
        double t = 0.0;
    };

    RailroadPath(std::vector<glm::dvec3> points,
                 std::vector<glm::dvec3> tangents,
                 bool flipped,
                 double initial_position,
                 double offset_x,
                 double offset_y)
        : points_(std::move(points)),
          tangents_(std::move(tangents)),
          flipped_(flipped),
          initial_position_(initial_position),
          offset_x_(offset_x),
          offset_y_(offset_y) {
        BuildSamples();
    }

    void BuildSamples();
    void EvaluateHermite(int segment, double t,
                         glm::dvec3& position, glm::dvec3& derivative) const;

    std::vector<glm::dvec3> points_;
    std::vector<glm::dvec3> tangents_;
    std::vector<SplineSample> samples_;
    bool flipped_ = false;
    double initial_position_ = 0.0;
    double offset_x_ = 0.0;
    double offset_y_ = 0.0;
};

} // namespace igi
