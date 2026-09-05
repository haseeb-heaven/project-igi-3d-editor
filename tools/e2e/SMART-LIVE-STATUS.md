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
