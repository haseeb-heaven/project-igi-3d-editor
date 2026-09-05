# Smart live testing: pilot gate

Scope: testing scripts only; do not change production rendering. Complete the
watchtower pilot before expanding to every object and workflow in the existing
human-workflow plan across levels 1–14.

## Current evidence (2026-09-05)

- Pilot: Level 1, Building 1105, WatchTower, model 405_01_1, identified from the
  installed QVM and matched against a source-hash-checked inventory.
- `test-smart-orbit.ps1` reproduced identical back/right input deltas; corrected
  horizontal quarter-turn and diagonal mappings pass the focused test.
- `New-SmartLivePilot.ps1` generates only one scenario. It records eight
  horizontal screenshots and checks the QVM hash before/after capture.
- The live capture completed, but acceptance FAILED: F11 selected a related
  graph origin instead of the watchtower. Inspected initial/front/right images
  do not contain a framed watchtower. Orbit logs report a radius near 185 million
  world units. Do not interpret the runner's PASS as visual correctness.
- Evidence: `artifacts/e2e/smart-watchtower-pilot-01/`, including a copied log,
  `pilot-acceptance.json`, the scenario report, and screenshots.
- `Test-SmartLivePilotEvidence.ps1` rejects this capture. It is an incomplete,
  fail-closed acceptance scaffold, not a finished all-object verifier.

## Next work

### Physical capture and state-evidence progress

The runner now supports opt-in `screenshot.client=true` with per-monitor DPI
awareness restored after capture. A live test confirmed 1920x1080 client pixels;
the former 1536x864 screenshots clipped the right/bottom of this desktop.

`SmartModelEvidence.ps1` checks fresh per-launch loader position/orientation and
texture assignment counts. All ten views in `artifacts/e2e/smart-watchtower-physical-10`
passed those checks (one matching transform and one 19/19 assignment record per
view), and source/QED restoration checks passed. The hand-authored camera fixture
removes the unit test's dependency on local game assets. Four focused script
tests pass, including negative cases for missing/ambiguous state, wrong transforms,
missing assignments, and invalid client-capture/draw-mask options.

The supplemental `artifacts/e2e/smart-watchtower-below-diagnostic` capture uses
the existing `-draw_parts -2` option. Terrain-hidden diagnostic intent is retained
in the manifest; the original terrain-occluded image is not replaced. The floor
and supports are visible underneath. Remaining diagonal images were inspected.
The extracted mesh dump has 19 material groups, slots 0–7 (slot 7 repeated), and
zero attachments. Its 19/8 count warning is not alone evidence of missing textures.

Pilot acceptance is still not automatic: individual live texture identities,
projected-bounds/occlusion checks, and a consolidated evidence acceptance report
remain to be implemented. No expansion to all objects has occurred.

### Saved-camera pilot progress

`New-SmartCameraPlan.ps1` now calculates eight horizontal poses and above/below
poses from exported mesh bounds and authored placement. `Invoke-SmartCameraPilot.ps1`
uses existing saved-camera settings, not F11, and restores QED QSC/QVM hashes.
The first pose exposed the all-zero-orientation spawn fallback; the harness uses
360 degrees for that equivalent explicit heading.

The ten-view capture completed in `artifacts/e2e/smart-watchtower-fixed-camera-10`.
Fresh logs confirmed all ten camera positions within 5 world units (float
rounding at large world coordinates), and all eight horizontal headings matched.
All original QED hashes were independently rechecked after the run. The pose
math and horizontal orbit mapping tests pass. Initial/front, side, opposite,
above, and below screenshots were inspected. The tower is framed in the
horizontal views, but a neighboring roof partly occludes one side and terrain
occludes the below view. These are capture results, not pilot acceptance.

Remaining: DPI-correct full-client capture (the target appears offset in the
current full-desktop images), pitch/pose evidence beyond settings, occlusion
handling, live material/attachment verification, authored transform verification,
and test fixtures independent of generated local assets. No all-object expansion.

1. Use existing camera configuration/control capabilities to frame the authored
   watchtower without F11 resolving a child graph. Restore any temporary camera
   configuration byte-for-byte. Do not modify production code.
2. Verify measured camera poses and projected object bounds; input deltas alone
   cannot prove a 360-degree orbit. Native orbit currently changes yaw only.
3. Verify live position/orientation, each material assignment and attachment,
   top/bottom coverage, source stability, and failure-injection checks.
4. Only after pilot acceptance, expand scripts to all applicable objects/LODs
   and remaining control, asset, graph/AI, environment, and persistence workflows.

Recovery record: wrong_result; capture command succeeded but camera target was
wrong; retained evidence and added explicit target rejection. No renderer fix
was attempted. The no-mistakes CLI was previously unavailable; no pipeline
success is claimed.
