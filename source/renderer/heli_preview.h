#pragma once
// ============================================================================
// heli_preview.h — Retail-parity helicopter preview parameters (issue #60).
//
// Ports the helicopter constants that open-igi reverse-engineered from the
// igi2.pdb symbols so the editor viewport preview spins rotors with the same
// collective-driven behavior the game renders.
//
// Evidence chain:
//   - Heli::ReadChannels  = 0x431B70 (tick-zero pose/collective restore,
//     channel-three hold sentinel -1)
//   - Heli::Tick          = 0x431E30 (consumes torque/smoothing/steps from the
//     physics-object config at +4..+36)
//   - Rotor parser        = 0x42D9F0 (ProducesLift / IsTailRotor / BladeSamples
//     / MaximumTilt / PhaseStep records)
//   - Rotor::Tick         = 0x42D440 (lift = Mass * Collective * gravity;
//     cos(tilt)*lift stored local-Y, lift local-Z)
//   open-igi sources: src/OpenIGI.Game/World/CutsceneRuntime.cs (lines ~908-1160,
//   ~1425-1583) and src/OpenIGI.Game/World/VehiclePhysicsRegistry.cs.
// ============================================================================

#include "../pch.h"
#include <string>
#include <vector>
#include <map>

struct LevelObject; // level/level_objects.h — full type only needed by the .cpp

namespace heli_preview {

// ---------------------------------------------------------------------------
// Per-model Heli control record (open-igi HelicopterPhysicsDefinition).
//
// VERIFIED: extracted from the retail physicsobj QVM define scripts
//   physicsobj/helis/bell/heli.qvm and physicsobj/helis/mil/heli.qvm.
//   Both shipped families carry byte-identical values (confirmed by scanning
//   the QVM immediates): torque triple 50/50/50, smoothing triple 0.4/0.4/0.4,
//   collective steps 0.027 / 0.003, dimensions 2.2 x 7.8 x 3. This matches the
//   default HelicopterPhysicsDefinition hardcoded in open-igi CutsceneRuntime.cs
//   (~1077): Mass=3000, Torque=(50,50,50), Smoothing=(0.4,0.4,0.4),
//   HighCollectiveStep=0.027, LowCollectiveStep=0.003, aerodynamics
//   [(20,X),(3,Y),(50,Z)].
struct PhysicsRecord {
    float mass = 3000.0f;                  // kg — VERIFIED (open-igi default; QVM immediate not float-decodable)
    float dimensions[3] = {2.2f, 7.8f, 3.0f}; // VERIFIED (bell+mil QVM @2.2/7.8/3 immediates)
    float torque[3] = {50.0f, 50.0f, 50.0f};  // VERIFIED (QVM immediates @0x22c/238/244 bell)
    float smoothing[3] = {0.4f, 0.4f, 0.4f};  // VERIFIED (QVM immediates)
    float high_collective_step = 0.027f;   // VERIFIED (QVM immediate) — step when Thrust >= 0.8
    float low_collective_step  = 0.003f;   // VERIFIED (QVM immediate) — step when Thrust <  0.8
};

// Shared Bell/Mil record (both retail scripts carry identical values).
const PhysicsRecord& GetPhysics();

// ---------------------------------------------------------------------------
// Known retail rotor body/model mapping.
//
// VERIFIED: model id strings embedded in the retail QVM scripts:
//   BELL family: body 709_01_1, rotors 711_01_1 + 711_02_1
//   MIL  family: body 700_01_1, rotors 700_03_1 + 700_04_1 + 700_02_1
bool IsKnownRotorModel(const std::string& modelId);
bool IsKnownHeliBodyModel(const std::string& modelId);

// ---------------------------------------------------------------------------
// Collective handling.
//
// Retail semantics (CutsceneRuntime.cs ~1108-1141):
//   - tick zero restores live AND target collective from the authored
//     "Original Thrust" parameter (Heli::ReadChannels 0x431B70), latching
//     channel three with -1 so the opening programme block cannot cancel it.
//   - per 30 Hz tick: TargetThrust follows channel 3 (when != -1); Thrust moves
//     toward TargetThrust by HighCollectiveStep (>= 0.8) or LowCollectiveStep.
//   - transform.RotorPhase += transform.Thrust every tick — rotor phase advance
//     is PROPORTIONAL TO COLLECTIVE. A heli authored with thrust 0 does not
//     spin its rotors up in-game.
//
// Clamp authored thrust into [0,1] for the preview: retail reads it as a Real32
// control magnitude (normalized cyclic/yaw/thrust channel map), negative or
// >1 values are not produced by vanilla level data.
float NormalizeCollective(float authoredThrust);

// Preview angular speeds (rad/s) at 30 Hz-equivalent full collective.
//
// INFERRED magnitudes: retail advances blade phase by
//   PhasePerCollectiveTick = BladeSamples * PhaseStep   (per unit collective,
//   per 30 Hz tick — VehiclePhysicsRegistry.RotorPhysicsDefinition), but the
//   BladeSamples/PhaseStep immediates inside the QVM bytecode could not be
//   decoded offline, so the baseline magnitudes reuse the pre-existing editor
//   preview rates (15 rad/s main, 25 rad/s tail) AT FULL COLLECTIVE and scale
//   them linearly with collective. Directions preserved from the existing
//   preview (main +, tail -): open-igi documents phase magnitude only, never
//   the visual rotation sign, so the sign stays INFERRED.
float MainRotorAngularSpeed(float collective); // default 15.0 * collective
float TailRotorAngularSpeed(float collective); // default 25.0 * |collective|, opposite sign

// Cache of resolved authored-collective token offsets per task type
// ("Heli" -> argTokens offset or -1 when the level declares no such field).
using DeclarationIndex = std::map<std::string, int>;

// Read the authored "Original Thrust" collective for a Heli object from the
// decompiled QSC data (name-resolved through this level's
// Task_DeclareParameters layout, mirroring open-igi RealNamed semantics).
// Returns -1.0f when unavailable — callers keep their default preview spin.
float LookupAuthoredCollective(const std::vector<LevelObject>* objects,
                               const LevelObject& heliObj,
                               DeclarationIndex& declCache);

} // namespace heli_preview
