# Smart live testing: pilot gate

## Single-session modular runner

`Invoke-SmartSingleSession.ps1` is now the default path behind
`Run-SmartTest.ps1` and `tests_run.cmd`. Each level invocation creates one
scenario with exactly one editor launch and one close; the scenario then loops
over the selected model objects and captures ten orbit views per selectable
object. Position/orientation, required DAT texture identities, live texture
assignment records, and screenshot counts are written to `batch.json`.

Examples:

```text
tests_run --level 5 --objects rigid --maximum 3
tests_run --level 1-14 --objects any --all-objects
tests_run --level 12 --object-type Building --maximum 10
```

The `-1#...` synthetic inventory records remain explicitly skipped because
the editor find-by-ID UI accepts only named numeric task IDs. They are not
silently accepted as visual captures. Use `--prepare-only` to inspect the
selected/skipped inventory before a live run.

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

## Bounded three-object trial — 2026-09-05

At the user's request, ran only L1 Building task 1105 (405_01_1), L2 Car
task 778 (622_01_1), and L3 EditRigidObj task 777 (301_01_1).
Artifacts: `artifacts/e2e/three-object-trial/`.
All 30 views completed (eight horizontal headings plus above/below per object).
All 30 fresh-log checks passed authored position/orientation, required DAT
texture successful loads with positive dimensions, and assignment counts.
All three runs restored original QED files and verified their hashes.

The four focused script test suites passed, including negative tests for
missing texture identities and zero dimensions. The reusable
`Invoke-SmartLiveTrial.ps1` preparation run resolved all three targets and
exported their actual meshes successfully. Its live orchestration was not
rerun: the completed trial used the same camera runner serially.

Visual acceptance remains UNVERIFIED: the image viewer rejected the saved
screenshots during review. Runtime logs do not prove per-draw GPU bindings,
unoccluded surfaces, or correct-looking textures. No production renderer
changes and no expansion to every object or all 14 levels.

Recovery record: wrong_result; capture command succeeded but camera target was
wrong; retained evidence and added explicit target rejection. No renderer fix
was attempted. The no-mistakes CLI was previously unavailable; no pipeline
success is claimed.

## Modular matrix runner — 2026-09-05

`Invoke-SmartVerificationMatrix.ps1` accepts `-Levels`, `-AllLevels`,
`-AllObjects`, `-Categories Buildings,RigidObjects,Vehicles,AI`,
`-ObjectTypes`, `-MaxObjects`, `-PrepareOnly`, `-Resume`, and
`-AllowConfigMutation`. The per-level runner keeps one recovery directory per
object, reuses verified mesh-derived plans on resume, and uses the
location-common model archive when a referenced model is not in the level
archive.

Selected tasks without a model, authored position, or authored rotation remain
enumerated as not applicable to this 3D model check. Synthetic `-1#...` model
instances use direct saved-camera capture and never receive fabricated task
IDs. One synthetic pilot passed all ten views.

Level 1 preparation completed 474/474 renderable instances and recorded 886
non-model tasks. The first full live Level 1 attempt exposed a serial-editor
cleanup failure and finished with 17 passes plus 457 lifecycle failures; it is
not accepted as a full-level result. The pilot now force-closes a lingering
editor after a failed capture before restoring QED files. An elevated retry of
one synthetic Level 1 object passed all ten views and ten model-evidence files,
with deployed QED hashes restored exactly. The all-object/all-level live
matrix remains unclaimed pending a clean resumable run.

### Runtime crash and evidence recovery — 2026-09-05

Windows WER recorded repeated `igi1ed.exe` crashes in `atioglxx.dll`
(`0xc0000005`, offset `0x007090d2`) during the Level 1 live matrix. The
corresponding editor dump is retained under the user CrashDumps directory.
This is an AMD OpenGL-driver failure during repeated editor startup/render
cycles, not evidence of a valid model transform or texture pass. The smart
pilot now waits five seconds at startup and between views; the cooldown
pilot's first view passed screenshot, consistent transform, runtime
texture-load, assignment, and graceful-close checks. Its ten-view run was
interrupted before completion, so no ten-view cooldown pass is claimed yet.
