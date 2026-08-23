#pragma once
#include <glm/glm.hpp>
#include <vector>

// Ladder placement parity gizmos (#68) — port of open-igi src/OpenIGI.Game/World/
// LadderPlacement.cs + LadderClimbLine.cs (retail evidence: sub_4404C0 activation,
// sub_4113E0 mounts, sub_440280 ladder constructor).
//
// A placed ladder exposes three retail magic vertices (world space, after the task
// transform):
//   vertex 1 = top activation reference (resolves activation height + top approach)
//   vertex 2 = foot of the climb line
//   vertex 3 = head of the climb line
//
// All constants below are game units (4096 units = 1 metre), cited from open-igi.

namespace igi {

// The activation radius at HumanPlayer's evaluate context +120, two metres. (sub_4404C0)
inline constexpr double kLadderActivateRadiusUnits = 8192.0;
// Strict half-width accepted across the face of the ladder: -2867.2 < localX < 2867.2. (sub_4404C0)
inline constexpr double kLadderActivateHalfWidthUnits = 2867.2;
// Cosine of the 45-degree facing tolerance. (sub_4404C0)
inline constexpr double kLadderFacingCosine = 0.70710678;
// Bottom mount stand-off along raw matrix column one. (sub_4113E0)
inline constexpr double kLadderBottomStandoffUnits = 1515.52;
// Bottom mount lift along raw matrix column two. (sub_4113E0)
inline constexpr double kLadderBottomVerticalOffsetUnits = 3915.776;
// Top mount lift along raw matrix column two. (sub_4113E0)
inline constexpr double kLadderTopVerticalOffsetUnits = 3481.6;
// Top mount reverse stand-off along raw matrix column one. (sub_4113E0)
inline constexpr double kLadderTopStandoffUnits = 1638.4;
// World units per climb step: 4096 * 0.3 = one rung spacing (0.3 m). (sub_440280)
inline constexpr double kLadderStepLengthUnits = 1228.8;

// One computed ladder gizmo: the three transformed magic vertices plus everything
// sub_4113E0 derives from them. Orientation columns are the raw zero-based frame:
// [0] = local X down the ladder face, [1] = standoff axis (raw column one),
// [2] = vertical axis (raw column two).
struct LadderGizmo {
    glm::dvec3 origin{0.0};            // task world position
    glm::dvec3 activation_top{0.0};    // magic vertex 1
    glm::dvec3 bottom{0.0};            // magic vertex 2 (climb foot)
    glm::dvec3 top{0.0};               // magic vertex 3 (climb head)
    glm::dvec3 bottom_mount{0.0};      // exact bottom position committed by sub_4113E0
    glm::dvec3 top_mount{0.0};         // exact top position committed by sub_4113E0
    glm::dvec3 local_x{1.0, 0.0, 0.0}; // raw column zero: local X across the ladder face
    double facing = 0.0;               // yaw committed on mounting, including the added pi
    int step_count = 1;                // whole steps the ladder is climbed in
};

// Computes the derived ladder quantities. Mirrors open-igi LadderPlacement's constructor:
//   Facing   = atan2(col0.y, col0.x) + PI          (Real32x9_GetAngles then gamma += PI)
//   BottomMount = bottom + col1*1515.52 + col2*3915.776
//   TopMount    = top    + col2*3481.6  + col1*(-1638.4)
//   StepCount   = max(1, trunc(|top.z - bottom.z| / 1228.8))
LadderGizmo ComputeLadderGizmo(const glm::dvec3& origin,
                               const glm::dvec3& activation_top,
                               const glm::dvec3& bottom,
                               const glm::dvec3& top,
                               const glm::dmat3& orientation);

// Line-segment gizmo geometry in world units (pairs of endpoints):
//   - climb line bottom->top, subdivided into step_count rung ticks
//   - face-width bands (+-2867.2 across local X) drawn at foot and head
//   - mount connectors bottom->bottom_mount and top->top_mount
std::vector<std::pair<glm::dvec3, glm::dvec3>> BuildLadderGizmoLines(const LadderGizmo& gizmo);

} // namespace igi
