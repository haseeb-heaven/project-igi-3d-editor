#include "pickup_preview.h"

#include <cmath>

namespace igi {

std::vector<LadderStyleLine> BuildPickupGizmoLines(const glm::dvec3& centre) {
    std::vector<LadderStyleLine> lines;

    // Radius disc: 16-segment circle on the XZ plane (retail planar distance test).
    constexpr int kSegments = 16;
    constexpr double kPi = 3.14159265358979323846;
    glm::dvec3 prev(centre.x + kPickupRadiusUnits, centre.y, centre.z);
    for (int i = 1; i <= kSegments; ++i) {
        const double ang = (2.0 * kPi * i) / kSegments;
        const glm::dvec3 next(centre.x + kPickupRadiusUnits * std::cos(ang),
                              centre.y,
                              centre.z + kPickupRadiusUnits * std::sin(ang));
        lines.push_back({prev, next});
        prev = next;
    }

    // Vertical tolerance ticks at the top and bottom of the accepted band.
    lines.push_back({glm::dvec3(centre.x, centre.y - kPickupVerticalToleranceUnits, centre.z),
                     glm::dvec3(centre.x, centre.y + kPickupVerticalToleranceUnits, centre.z)});
    return lines;
}

} // namespace igi
