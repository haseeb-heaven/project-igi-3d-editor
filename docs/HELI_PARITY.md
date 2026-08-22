# Helicopter Preview Parity — open-igi / igi2.pdb evidence (Issue #60)

Goal: the editor's Heli rotor preview must behave like the retail game as
documented by [open-igi](https://github.com/OpenIGI), whose implementation was
written from the `igi2.pdb` symbols.

## Retail symbol evidence (via open-igi)

| Retail function | Address | What it does |
|---|---|---|
| `Heli::ReadChannels` | `0x431B70` | Bidirectional channel read. On animation **tick zero** it restores the authored pose and both live + target **collective** from the level's `Original Thrust` parameter, clears velocity/controls/rotor phase/angular momentum, and writes hold sentinels into the shared AnimTask block (`0,0,0,-1,-2`). The `-1` on channel three latches the collective so the opening cutscene programme block cannot cancel `Original Thrust` on tick one. |
| `Heli::Tick` / config reader | `0x431E30` | Per 30 Hz physics step: smooths normalized cyclic/yaw controls (they are **not** integrated as Euler radians), tracks `TargetThrust` from channel 3 (when != -1), moves `Thrust` toward target by the high/low collective step, advances `RotorPhase += Thrust`, then runs the rigid flight step (forces, drag samples, quaternion orientation integration, angular damping 0.92). |
| Rotor parser | `0x42D9F0` | Parses `TASKTYPE_ROTOR` records from the shipped `physicsobj/helis/<family>/HELI.QVM` define scripts: `ProducesLift`, `IsTailRotor`, `BladeSamples`, `MaximumTilt`, `PhaseStep`, `Sound`. |
| `Rotor::Tick` | `0x42D440` | Computes `lift = Mass * Collective * gravity` (gravity `44.600887`), stores `cos(tiltAngle) * lift` in local Y and `lift` in local Z, and queues the force for the **next** `Heli::Tick` (rotor tasks run after their parent Heli task). |

open-igi sources: `src/OpenIGI.Game/World/CutsceneRuntime.cs` (~908-1160,
~1425-1583) and `src/OpenIGI.Game/World/VehiclePhysicsRegistry.cs`.

## Per-model physics record (`heli_preview::PhysicsRecord`)

Both shipped helicopter families carry **identical** Heli control values. The
values below were verified two ways: (1) they are the default
`HelicopterPhysicsDefinition` hardcoded in open-igi `CutsceneRuntime.cs`, and
(2) the same numbers were independently scanned out of the retail QVM
immediates in this repo's local game copy
(`physicsobj/helis/bell/HELI.QVM`: torque `50/50/50`, smoothing `0.4/0.4/0.4`,
steps `0.027/0.003`, dimensions `2.2/7.8/3`).

| Field | Value | Status |
|---|---|---|
| `mass` | `3000` | **verified** (open-igi default; QVM immediate not float-decodable offline) |
| `dimensions` | `2.2 x 7.8 x 3.0` | **verified** (QVM immediates, both families) |
| `torque` | `50, 50, 50` | **verified** (QVM immediates) |
| `smoothing` | `0.4, 0.4, 0.4` | **verified** (QVM immediates) |
| `high_collective_step` | `0.027` | **verified** (QVM immediate; used when `Thrust >= 0.8`) |
| `low_collective_step` | `0.003` | **verified** (QVM immediate; used when `Thrust < 0.8`) |

## Retail model mapping (from QVM script strings)

| Family | Body model | Rotor models |
|---|---|---|
| BELL | `709_01_1` | `711_01_1`, `711_02_1` |
| MIL  | `700_01_1` | `700_03_1`, `700_04_1`, `700_02_1` |

Status: **verified** — these model ids are literal strings embedded in the
retail `physicsobj` QVM scripts. The editor uses them to confirm that an ATTA
child is a rotor (`heli_preview::IsKnownRotorModel`) even when the geometric
main/tail heuristic misses (e.g. a mid-hull MIL rotor).

## Preview behavior mapping

| Editor behavior | Retail basis | Status |
|---|---|---|
| Authored `Original Thrust` read from decompiled QSC (name-resolved through the level's `Task_DeclareParameters`, space/case-insensitive like open-igi `RealNamed`) | `Heli::ReadChannels` restores collective from the authored parameter at tick zero | **verified** semantics |
| Rotor spin speed proportional to collective; `0` collective ⇒ rotors stopped | `RotorPhase += Thrust` per 30 Hz tick (`CutsceneRuntime.cs` ~1143) | **verified** semantics |
| Main rotor `15 rad/s`, tail `-25 rad/s` at full collective; tail spins opposite main | Baseline magnitudes reused from the pre-existing editor preview. Retail blade rate = `Collective * BladeSamples * PhaseStep * 30 Hz`, but the `BladeSamples`/`PhaseStep` immediates could not be decoded from the QVM bytecode offline | **inferred** magnitude |
| Main rotor classified as highest-local-Z ATTA child; tail as most-negative-local-Y | Geometric heuristic kept from the existing preview; open-igi classifies via the rotor task records instead | **inferred** heuristic, **verified** model-id cross-check |
| No lift/bob translation of the heli body | Full `Heli::Tick` rigid-body flight step is a gameplay-runtime feature; the editor previews placement only (issue #60 requirement 4, no #37 placement regression) | out of scope by design |

## Files

- `source/renderer/heli_preview.h` / `.cpp` — constants, model maps,
  collective normalization, authored-thrust QSC lookup
- `source/renderer/renderer_objects.cpp` — resolves the Heli's authored
  collective before the ATTA draw pass
- `source/renderer/renderer_objects_atta.cpp` — collective-driven spin speeds,
  known-rotor-model classification
- `tests/test_heli_preview.cpp` — fixture-independent unit tests (record
  values, model maps, collective clamp, speed proportionality, QSC lookup)
