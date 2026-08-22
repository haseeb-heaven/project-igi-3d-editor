#pragma once
#include <glm/glm.hpp>
#include <vector>

// Weapon/ammo/generic pickup parity (#75) — retail constants and gizmo geometry
// from open-igi src/OpenIGI.Game/World/GunPickupRegistry.cs + GenericPickupRegistry.cs:
//   * GenericPickup::Update  0x46C630
//   * GunPickup::Update      0x46D230
//   * GunPickup_Create       0x46C980
//   * PickupRadius       = 2867.2 units (planar, player-feet centred)
//   * VerticalTolerance  = 4096.0 units
// The 2867.2 constant is shared with the ladder activation half-width (sub_4404C0).

namespace igi {

inline constexpr double kPickupRadiusUnits = 2867.2;
inline constexpr double kPickupVerticalToleranceUnits = 4096.0;

struct LadderStyleLine {
    glm::dvec3 first;
    glm::dvec3 second;
};

// Build pickup-volume gizmo lines at `centre` (authored pickup position):
//   * radius disc (16-segment circle on the XZ plane at pickup height)
//   * vertical tolerance ticks at centre.y +/- 4096
// Returns line segments in world/game units.
std::vector<LadderStyleLine> BuildPickupGizmoLines(const glm::dvec3& centre);

} // namespace igi
