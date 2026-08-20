# Gameplay Runtime TODO

- [ ] Windows CMake configure/build (authoritative environment still required).
- [x] Headless focused runtime test harness: 28/28 passing on the development host.
- [x] Absolute-deadline 30 Hz clock and edge-case tests.
- [x] Runtime task ownership and lifecycle safety.
- [x] Runtime session/world ownership and editor snapshot isolation.
- [x] Ground/roof queries and multi-height wall sweep.
- [x] Player controller integration, authored tuning, and collision regression tests.
- [x] Weapon cadence/ammunition, world-occluded damage, AI patrol/combat, bounded
      QVM execution, and mission vertical-slice wiring.
- [x] Gameplay host/input focus, pause, restart, and editor restore integration.
- [ ] Final Windows verification and evidence/limitations report.

## Current evidence boundary

- `verified-reference`: fixed-step scheduling and player collision constants
  traced to OpenIGI source; the branch preserves the evidence labels in code.
- `implemented-slice`: player movement/jump/crouch/health, six weapon states,
  world-occluded hits, guard patrol/perception/combat, audio hooks, HUD, and
  objective/extraction flow.
- `inferred` or `placeholder`: some root-motion speeds, fallback guard patrol,
  demo extraction placement, and the normalized QVM seam are not proof of full
  retail IGI1 behavior. Actual Windows execution against the supplied vanilla
  assets remains a required verification step.
