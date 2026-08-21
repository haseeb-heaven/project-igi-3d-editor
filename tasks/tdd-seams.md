# TDD Seams for the Gameplay Runtime

The implementation tests public behavior at these seams. Tests should not
inspect private fields or couple themselves to the chosen data structure.

| Seam | Observable contract |
| --- | --- |
| `GameClock` | Absolute 30 Hz deadlines, bounded catch-up, pause/exclusion, render boundaries, and reset behavior. |
| `PlayerCollision` | Ground/roof probes, wall blocking and sliding, stance clearance, and finite obstacle resolution. |
| `PlayerMotion` / `PlayerController` | Verified-reference gravity, ladder-slide damping, air-control basis, root-motion transform order, input-to-motion behavior, stance transitions, and collision-safe position. |
| `LadderPlacement` / `LadderTraversal` | Magic-vertex mount geometry, activation selection, rung boundaries, top transitions, sliding interruption, and deterministic dismount. |
| `MagicObjectRegistry` | First-definition-wins parsing of `DefineMagicObj` rows and named `TASKTYPE_LADDER` resolution without hard-coded process-local IDs. |
| `TaskTree` | Registration, parent ownership, lifecycle order, targeted messages, and safe destruction. |
| `QvmInterpreter` | Malformed-program rejection, bounded execution, native calls, stack safety, and deterministic reset. |
| `GameplayHost` / `RuntimeWorld` | Isolated session start/tick/restart/close, mission state, and no mutation of editor snapshots. |
| `GameplayHost` / `WindowInputRouter` | F6/F7 focus handoff changes input ownership without restarting or resetting the active runtime session. |
| `RenderTarget` | Gameplay-window presentation remains distinct from render-only editor repaints. |

The user-requested TDD workflow is applied as a vertical slice: each new test
is added before the smallest production change that makes that behavior pass.
