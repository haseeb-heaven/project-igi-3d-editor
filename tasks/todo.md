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
- [x] Replace the inferred landing-impact rule with the verified OpenIGI
      vanilla speed-to-health formula, direct-health damage path, authored fall
      sound selection, guard hearing-radius event, and regression coverage.
- [x] Port the verified-reference HumanMotion airborne gravity, ladder-slide
      gravity/drag, air-control basis, and root-motion scale/rotation order;
      expose animation-local deltas at the fixed-step player boundary.
- [x] Port the verified-reference ladder climb line, mount offsets, activation
      geometry, and four-phase traversal state machine as a renderer-free seam.
- [x] Replace the renderer's ad-hoc magic-object scan with a shared parser that
      resolves `TASKTYPE_LADDER` by name rather than by a process-local integer.
- [x] Discover attached ladder models during gameplay setup and copy their
      transformed magic-vertex placement data into RuntimeWorld.
- [x] Bind ladder mount, climb, top transition, slide, and dismount input to
      RuntimeWorld; the no-animation path uses a deterministic `inferred`
      fixed-step fallback so Play mode remains traversable.
- [ ] Connect authored ladder animation root-motion and completion events to
      `PlayerInputCmd`; the production seam exists, but the animation presenter
      still needs to supply the event edges.
- [x] Gameplay host/input focus, pause, restart, and editor restore integration.
- [x] Extract and test an explicit `RuntimeSession` state machine with
      editor-snapshot isolation and deterministic restart/close behavior.
- [ ] Finish the separate native gameplay window boundary. `GameplayWindowHost`
      now creates the Windows/FreeGLUT window, routes gameplay callbacks,
      focus, viewport, and close recovery; Windows interactive verification and
      independent renderer ownership remain pending.
- [ ] Move gameplay presentation/HUD/camera ownership behind the gameplay host;
      `GameplayHost` now owns an OpenGL-free `RuntimeRenderer` snapshot and the
      gameplay HUD/weapon/projectile paths consume it, while the actual scene
      draw and GL asset cache still run through `App::Frame()`. The editor now
      repaints its authoring scene through a render-only target while gameplay
      remains active.
- [x] Add explicit `F5` apply/restart semantics for a live gameplay session;
      the session captures the replacement snapshot, rebuilds copied runtime
      objects, resets mission/AI state, and never silently mutates authoring
      data. `F6` focuses the editor for authoring changes and `F7` returns to
      gameplay; switching focus does not restart simulation. Auto-save,
      terrain editing, and level switching stay gated during an active run
      until immutable terrain/asset snapshots are complete.
- [ ] Bind/verify selected-vanilla-fixture traversal (animation events and
      player input), authored mission patrol routes, and
      non-demo extraction conditions from OpenIGI/retail evidence.
- [ ] Final Windows verification and evidence/limitations report.

## Current evidence boundary

- `verified-reference`: fixed-step scheduling and player collision constants
  plus the landing-impact speed, damage, sound-boundary, and direct-health
  rules traced to OpenIGI source; the branch preserves the evidence labels in
  code.
- `implemented-slice`: player movement/jump/crouch/health, weapon states,
  world-occluded hits, guard patrol/perception/combat, authored weapon
  selection, projectile simulation, flash exposure, zoom, landing audio,
  health/armor HUD, and objective/extraction flow.
- `verified-reference` motion seam: OpenIGI HumanMotion's airborne gravity,
  ladder-slide integrator, movement-slot air control, and root-motion transform
  are implemented and covered; no player animation stream currently feeds the
  new command field, so keyboard movement remains the runtime fallback.
- `verified-reference` ladder seam: placement, activation, traversal phases,
  ladder-slide integration, and mount geometry are covered; interactive
  RuntimeWorld input is implemented, while no-animation rung/top movement is
  explicitly labelled `inferred` until authored animation events are wired.
- `architecture-gap`: a controlled gameplay window now owns gameplay focus,
  input callbacks, relative mouse recentering, viewport, and close recovery,
  and `GameplayHost` owns an OpenGL-free presentation snapshot. The actual
  scene draw and GL asset cache still run through `App::Frame()`. The editor
  repaints its authoring scene through a render-only target during active
  gameplay; a fully independent GL `RuntimeRenderer` remains open.
- `inferred` or `placeholder`: some root-motion speeds, fallback guard patrol,
  fall-impact camera/hearing presentation, demo extraction placement, and the
  normalized QVM seam are not proof of full retail IGI1 behavior. Actual
  Windows execution against the supplied vanilla assets remains a required
  verification step.
