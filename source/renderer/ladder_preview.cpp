#include "ladder_preview.h"

#include <cmath>

namespace igi {

LadderGizmo ComputeLadderGizmo(const glm::dvec3& origin,
                               const glm::dvec3& activation_top,
                               const glm::dvec3& bottom,
                               const glm::dvec3& top,
                               const glm::dmat3& orientation) {
    LadderGizmo g;
    g.origin = origin;
    g.activation_top = activation_top;
    g.bottom = bottom;
    g.top = top;

    // open-igi: Real32x9_GetAngles(..., &gamma) followed by gamma += PI in sub_4113E0.
    g.facing = std::atan2(orientation[0].y, orientation[0].x) + 3.14159265358979323846;
    g.local_x = orientation[0];

    // The retail Real32x9 reads are zero-based columns: raw column one is the standoff
    // axis, raw column two is the vertical axis (open-igi's Column2/Column3 one-based).
    const glm::dvec3 standoff_axis = orientation[1];
    const glm::dvec3 vertical_axis = orientation[2];

    g.bottom_mount = bottom + standoff_axis * kLadderBottomStandoffUnits
                           + vertical_axis * kLadderBottomVerticalOffsetUnits;
    g.top_mount = top + vertical_axis * kLadderTopVerticalOffsetUnits
                      + standoff_axis * (-kLadderTopStandoffUnits);

    // sub_440280 measures the vertical height and truncates height/1228.8 into an int;
    // at least one step so a degenerate ladder still climbs.
    const double height = std::fabs(top.z - bottom.z);
    int steps = static_cast<int>(height / kLadderStepLengthUnits);
    g.step_count = steps < 1 ? 1 : steps;
    return g;
}

std::vector<std::pair<glm::dvec3, glm::dvec3>> BuildLadderGizmoLines(const LadderGizmo& gizmo) {
    std::vector<std::pair<glm::dvec3, glm::dvec3>> lines;

    // Climb line foot->head.
    lines.emplace_back(gizmo.bottom, gizmo.top);

    // Rung ticks along the climb line — one per whole step (interior ticks only).
    const double height = std::fabs(gizmo.top.z - gizmo.bottom.z);
    if (height > 1e-9) {
        for (int s = 1; s < gizmo.step_count; ++s) {
            const double f0 = static_cast<double>(s) / static_cast<double>(gizmo.step_count);
            const glm::dvec3 p = gizmo.bottom + (gizmo.top - gizmo.bottom) * f0;
            // Short horizontal tick perpendicular to the climb direction, half a rung long.
            const glm::dvec3 dir = glm::normalize(gizmo.top - gizmo.bottom);
            glm::dvec3 side(-dir.y, dir.x, 0.0);
            if (glm::length(side) < 1e-6) side = glm::dvec3(0.0, 1.0, 0.0);
            side = glm::normalize(side) * (kLadderStepLengthUnits * 0.5);
            lines.emplace_back(p - side, p + side);
        }
    }

    // Face-width bands across local X at foot and head (+-2867.2, sub_4404C0's strict
    // -2867.2 < localX < 2867.2 test).
    {
        const glm::dvec3 side = glm::normalize(gizmo.local_x) * kLadderActivateHalfWidthUnits;
        lines.emplace_back(gizmo.bottom - side, gizmo.bottom + side);
        lines.emplace_back(gizmo.top - side, gizmo.top + side);
    }

    // Mount connectors.
    lines.emplace_back(gizmo.bottom, gizmo.bottom_mount);
    lines.emplace_back(gizmo.top, gizmo.top_mount);

    return lines;
}

} // namespace igi
