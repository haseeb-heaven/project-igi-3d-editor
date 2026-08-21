# Implementation Plan: IGI Gameplay Runtime and Twin-Window Port

## Overview

Bring the `editor-gameplay-mode` C++ branch from its current scaffold toward a
deterministic, isolated Windows gameplay runtime that consumes the existing
vanilla editor level data. The immediate target is a convincing playable
vertical slice: fixed-step simulation, real player collision, weapons/projectiles,
moving enemies, sound, HUD, and one mission path. The production boundary must
also keep the editor alive in its own window while gameplay is focused. Retail
parity claims remain gated on IGI1 evidence.

## Architecture Decisions

- Keep editor/source level objects immutable to gameplay; runtime state is built
  from copied/adapted data and is destroyed on restart/close.
- Keep the simulation at 30 Hz with explicit catch-up, pause, exclusion, and
  reset semantics; rendering/window integration consumes simulation state.
- Treat the runtime session as the owner of mutable world state. The editor host
  owns transition policy; the gameplay host owns presentation/window policy; no
  gameplay renderer may mutate the authoring level representation.
- Follow OpenIGI's `GameLoop`/`IGameWindow` split: the host pumps window events,
  the fixed-step session advances simulation, and presentation consumes a
  snapshot. Do not hide gameplay rendering inside the editor display callback.
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

### Phase 5: Production runtime boundary (current)

- [x] Extract an explicit `RuntimeSession` lifecycle (`Created`, `Running`,
  `Paused`, `Stopped`, `Failed`) from `GameplayHost`, with direct lifecycle
  tests and no editor/OpenGL dependency.
- [ ] Add a controlled gameplay window owned by `GameplayHost`; the current
  slice creates the Windows/FreeGLUT window, routes callbacks, focus, cursor,
  relative mouse look, viewport, and close recovery. The editor now repaints
  its authoring scene through a render-only target while gameplay remains
  active. Full independent camera/HUD/render ownership remains pending because
  scene presentation still passes through the shared `App::Frame()` renderer.
- [ ] Move gameplay drawing out of `App::Frame()` into a runtime presentation
  path. `GameplayHost` now owns the OpenGL-free `RuntimeRenderer` snapshot used
  by HUD/weapon/projectile presentation; keep shared asset caches read-only and
  make restart/close destroy the mutable runtime session before moving the GL
  draw/resource ownership.
- [x] Add an explicit `F5` apply/restart boundary: the host replaces the
  captured editor snapshot, rebuilds mutable runtime adapters from copied
  level objects, and never silently writes source files or mutates the running
  world.
- [x] Add deterministic `F6` editor / `F7` gameplay focus routing. The editor
  can receive authoring input and repaint its source view while the active
  runtime continues through the fixed-step scheduler; focus changes do not
  restart the session. Terrain editing and level switching remain blocked
  while gameplay is active because their immutable asset snapshot boundary is
  not yet complete.

### Phase 6: Fidelity and fixture coverage

- [x] Replace the inferred landing-impact threshold with the verified OpenIGI
  vanilla speed-to-health formula, direct-health damage path, authored fall
  sound selection, and regression coverage.
- [x] Port the OpenIGI-referenced airborne and ladder-slide motion integrators,
  root-motion transform order, and the fixed-step player-controller seam.
- [x] Port the OpenIGI-referenced ladder placement offsets, activation geometry,
  rung discretization, and traversal state transitions as a headless seam.
- [ ] Bind ladder discovery to the runtime magic-object registry, attachment
  transforms, animation events, and player input in the selected fixture.
- [ ] Replace fallback patrol/extraction behavior with authored mission data in
  the selected fixture; scan level/Common/Weapons QVMs before adding natives.
- [ ] Add repeatable vanilla fixture captures for player traversal, AI combat,
  weapon/projectile timing, objective progression, audio, and restart.
- [ ] Run the authoritative Windows configure/build/test/play checks and record
  every remaining `inferred` or `placeholder` behavior.

### Checkpoint: Vertical slice

- [x] One mission fixture reaches success and failure deterministically.
- [x] Player, AI, weapon, task/QVM, objective, and restart paths execute without
  crashing.
- [ ] Existing editor regression tests remain green in the authoritative Windows
  build.

## Checkpoints and acceptance criteria

### Checkpoint: Runtime session

- [x] A session can be created, opened, paused, restarted, and closed
  without changing the editor snapshot or source-level object data.
- [x] Duplicate open/close and failed transition paths are deterministic and
  covered by focused tests.

### Checkpoint: Twin-window gameplay

- The editor and gameplay windows have separate focus/input/camera/HUD/render
  paths; switching focus does not restart simulation.
- Pausing or closing gameplay leaves the editor visible and interactive.
- A gameplay-window failure tears down only the runtime presentation and returns
  the editor to a usable state.

### Checkpoint: Playable vanilla fixture

- One selected vanilla mission can be entered, played for multiple seconds at
  the fixed simulation rate, fought through, interacted with, completed or
  failed, and restarted without process termination.
- Windows build/tests and an interactive smoke are recorded; local headless
  tests do not count as Windows proof.

## Risks and Mitigations

| Risk | Impact | Mitigation |
|---|---|---|
| Current branch contains plausible but incomplete stubs | High | Replace stubs only behind focused tests; search for no-op paths before claiming behavior. |
| OpenIGI behavior differs from IGI1 | High | Mark evidence class in code/tests and require retail comparison before parity claims. |
| Editor/runtime coupling corrupts authoring state | High | Runtime copies/adapters, explicit snapshots, no implicit source writes. |
| Windows-only C++/OpenGL build is platform-sensitive | Medium | Run headless subsystem tests here, then require the Windows CMake target/CI as the authoritative build. |
| FreeGLUT/OpenGL context ownership can couple two windows accidentally | High | Keep window creation behind a host adapter, use FreeGLUT's current-context option so the renderer's loaded resources remain visible, verify current-window routing on Windows, and test session state independently before wiring presentation. |
| Scope is larger than one implementation turn | High | Keep this plan active, deliver vertical slices, and preserve a runnable checkpoint. |

## Evidence Sources

- `PORTING_SPECIFICATION.md` in this branch.
- `/Users/haseeb-mir/Documents/Code/open-igi/Agents.md`.
- OpenIGI reference symbols under `src/OpenIGI.Engine`, `src/OpenIGI.Game`, and
  `src/OpenIGI.Scripting`.
- OpenIGI window/loop contracts: `src/OpenIGI.Platform/IGameWindow.cs`,
  `src/OpenIGI.Platform.Desktop/SilkGameWindow.cs`,
  `src/OpenIGI.Engine/Flow/GameLoop.cs`, and
  `src/OpenIGI.Engine/Time/GameClock.cs`.
- Existing C++ parser, terrain, renderer, and runtime tests in this branch.
