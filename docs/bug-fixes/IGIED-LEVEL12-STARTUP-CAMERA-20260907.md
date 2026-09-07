# IGIED-LEVEL12-STARTUP-CAMERA-20260907

## Symptom

Opening Level 12 could place the editor camera at a stale position saved from
another level/session. The editor then opened away from the authored game start
and the WinchHouse scene was not visible.

## Resolution

`App::LoadLevel` now uses the level-authored start pose for a fresh process and
after a level switch. A persisted camera is used only for an in-session reload
of the same level. The loader records the selected pose source in the log so
the E2E check can distinguish an authored start from a stale saved camera.

## Verification

- Before fix: Level 12 startup used `(127000280,-37829456,175303728)` and the
  startup scene oracle failed (`uniqueRatio 0.004827...`).
- After fix: Level 12 startup used authored `HumanPlayer` position
  `(112255288,-42206448,179544736)`, logged
  `Viewer start source=level-authored-start`, and passed the visible Session 1
  E2E scenario.
- Focused C++ tests: 8/8 passed.
- E2E manifest contract: passed.
