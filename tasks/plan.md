# Implementation Plan: IGI Gameplay Runtime Vertical Slice

## Overview

Bring the `editor-gameplay-mode` C++ branch from its current scaffold toward a
deterministic, isolated gameplay runtime that consumes the existing editor level
data. The first implementation checkpoint is a playable, testable foundation:
fixed-step scheduling, safe runtime ownership, faithful player collision
queries, bounded script execution, and a single mission/objective path. Retail
parity claims remain gated on IGI1 evidence.

## Architecture Decisions

- Keep editor/source level objects immutable to gameplay; runtime state is built
  from copied/adapted data and is destroyed on restart/close.
- Keep the simulation at 30 Hz with explicit catch-up, pause, exclusion, and
  reset semantics; rendering/window integration consumes simulation state.
- Treat OpenIGI as `verified-reference` evidence, not proof of IGI1 parity.
  Any behavior without retail evidence is labelled `inferred` or `placeholder`.
- Prefer narrow injected collision and visibility queries so gameplay tests do
  not depend on an OpenGL window or renderer globals.

## Task List

### Phase 1: Audit and baseline

- [x] Confirm the exact remote branch, specification, OpenIGI reference, and
  current runtime files.
- [x] Build a headless runtime harness and run the focused tests; the baseline
  slice passed 28/28 tests on the development host, and the final line-of-sight
  change passed a fresh runtime smoke harness.
- [x] Record the current placeholders and missing production components in
  `coding_guidelines.md` and the evidence boundary in `tasks/todo.md`.

### Phase 2: Runtime foundation

- [x] Replace the accumulator-only clock with the documented absolute-deadline
  schedule, pause/exclusion/reset handling, bounded catch-up, and tests.
- [x] Harden runtime task ownership, duplicate IDs/parents, lifecycle ordering,
  queued message timing, and teardown tests.
- [x] Add an explicit runtime session/world boundary and editor snapshot reset
  contract without mutating editor source objects.

### Checkpoint: Foundation

- [x] Focused runtime tests pass.
- [ ] Windows C++ build succeeds (not runnable on the current non-Windows host).
- [x] Runtime can start, tick deterministically, reset, and close without
  changing the editor/source representation.

### Phase 3: Gameplay systems

- [x] Port the OpenIGI-referenced ground/roof constants and multi-probe wall
  sweep into the C++ collision adapter; add ground, step, wall, corner, slope,
  ceiling, and missing-geometry tests.
- [x] Integrate player input, stance, gravity, air control, camera pose, and
  collision resolution into the runtime session.
- [x] Harden weapon cadence/ammunition/damage and world-occluded deterministic
  ballistics.
- [x] Connect AI perception/event queues/patrol/combat to runtime entities.
- [x] Execute one validated bounded normalized QVM program through an explicit native
  registry and connect task/message updates at the fixed tick boundary.

### Phase 4: Mission and presentation integration

- [x] Add one objective success/failure/restart path from runtime events.
- [x] Separate gameplay input focus/pause/restart from editor input and expose a
  gameplay host boundary; preserve editor rendering and save behavior.
- [x] Add the vertical-slice integration test and document unresolved retail
  differences and placeholders.

### Checkpoint: Vertical slice

- [x] One mission fixture reaches success and failure deterministically.
- [x] Player, AI, weapon, task/QVM, objective, and restart paths execute without
  crashing.
- [ ] Existing editor regression tests remain green in the authoritative Windows
  build.

## Risks and Mitigations

| Risk | Impact | Mitigation |
|---|---|---|
| Current branch contains plausible but incomplete stubs | High | Replace stubs only behind focused tests; search for no-op paths before claiming behavior. |
| OpenIGI behavior differs from IGI1 | High | Mark evidence class in code/tests and require retail comparison before parity claims. |
| Editor/runtime coupling corrupts authoring state | High | Runtime copies/adapters, explicit snapshots, no implicit source writes. |
| Windows-only C++/OpenGL build is platform-sensitive | Medium | Run headless subsystem tests here, then require the Windows CMake target/CI as the authoritative build. |
| Scope is larger than one implementation turn | High | Keep this plan active, deliver vertical slices, and preserve a runnable checkpoint. |

## Evidence Sources

- `PORTING_SPECIFICATION.md` in this branch.
- `/Users/haseeb-mir/Documents/Code/open-igi/Agents.md`.
- OpenIGI reference symbols under `src/OpenIGI.Engine`, `src/OpenIGI.Game`, and
  `src/OpenIGI.Scripting`.
- Existing C++ parser, terrain, renderer, and runtime tests in this branch.
