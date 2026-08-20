# Gameplay Runtime TODO

- [ ] Windows CMake configure/build (authoritative environment still required).
- [x] Headless focused runtime test harness and isolated runtime smokes for the
      current gameplay slices; the authoritative GoogleTest/Windows run still
      requires the Windows dependency environment.
- [x] Final static-geometry line-of-sight smoke: wall-occluded player and
      guard damage was blocked; open combat and mission extraction passed.
- [x] Absolute-deadline 30 Hz clock and edge-case tests.
- [x] Runtime task ownership and lifecycle safety.
- [x] Runtime session/world ownership and editor snapshot isolation.
- [x] Ground/roof queries and multi-height wall sweep.
- [x] Player controller integration, authored tuning, and collision regression tests.
- [x] Weapon cadence/ammunition, world-occluded damage, AI patrol/combat, bounded
      QVM execution, and mission vertical-slice wiring.
- [x] Vanilla thrown-weapon categories with deterministic projectile collision,
      bounce/fuse/impact detonation, blast line-of-sight, flash exposure, and
      live Play-mode projectile presentation.
- [x] Authored guard weapon selection, scripted/patrol animation request
      delivery, and fixed-step weapon zoom state.
- [x] Gameplay host/input focus, pause, restart, and editor restore integration.
- [x] Extract and test an explicit `RuntimeSession` state machine with
      editor-snapshot isolation and deterministic restart/close behavior.
- [ ] Finish the separate native gameplay window boundary. `GameplayWindowHost`
      now creates the Windows/FreeGLUT window, routes gameplay callbacks,
      focus, viewport, and close recovery; Windows interactive verification and
      independent renderer ownership remain pending.
- [ ] Move gameplay presentation/HUD/camera ownership behind the gameplay host;
      the current gameplay scene/HUD still render through `App::Frame()`, while
      the editor retains its last authoring frame during active gameplay.
- [ ] Add explicit apply/restart semantics for editor changes made while a
      gameplay session exists; never silently mutate authoring data.
- [ ] Port/verify remaining selected-vanilla-fixture traversal (fall damage,
      ladder/root-motion where exercised), authored mission patrol routes, and
      non-demo extraction conditions from OpenIGI/retail evidence.
- [ ] Final Windows verification and evidence/limitations report.

## Current evidence boundary

- `verified-reference`: fixed-step scheduling and player collision constants
  traced to OpenIGI source; the branch preserves the evidence labels in code.
- `implemented-slice`: player movement/jump/crouch/health, weapon states,
  world-occluded hits, guard patrol/perception/combat, authored weapon
  selection, projectile simulation, flash exposure, zoom, audio hooks, HUD,
  and objective/extraction flow.
- `architecture-gap`: a controlled gameplay window now owns gameplay focus,
  input callbacks, viewport, and close recovery, but scene/HUD presentation
  still runs through `App::Frame()` and the editor is retained as its last
  authoring frame during active gameplay. Full runtime renderer ownership is
  not implemented yet.
- `inferred` or `placeholder`: some root-motion speeds, fallback guard patrol,
  demo extraction placement, and the normalized QVM seam are not proof of full
  retail IGI1 behavior. Actual Windows execution against the supplied vanilla
  assets remains a required verification step.
