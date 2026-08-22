# Weather Parity — editor vs open-igi (issues #57, #58)

The reference is [open-igi](https://github.com/OpenIGI) (`/Users/haseeb-mir/Documents/Code/open-igi`),
written from igi2.pdb symbols and validated against retail igi.exe rendering.
Editor files: `source/renderer/renderer_rain.cpp/.h`, `source/renderer/weather_math.h`,
`source/renderer/renderer_snow*` (folded into renderer_rain), menu wiring in
`source/renderer/renderer_draw.cpp` + `source/app_input_mouse.cpp`, persistence in
`source/config.*`.

## Authored descriptor (both styles)

| Behavior | open-igi | Editor | Status |
|---|---|---|---|
| Task source | `RainEffect` task via `FindRainEffect` | `RainEffect` re entry in objects.qsc parse | match |
| Snowfall encoding | `Is Rain=FALSE` + `Is Active=TRUE` → snow | same flags parsed; `isRain=false && active` selects snow | **fixed (#57)** — editor previously ignored snow levels entirely |
| Band params | `Traceline start` / `Traceline end`, camera-anchored each frame | same | match |
| Indoors suppression | building-shell box test (`SetIndoors`) | existing mesh-bbox indoor test (`SetIndoors`) | match |

## Rain audit (#58)

| Item | open-igi value | Old editor value | Resolution |
|---|---|---|---|
| Drop count | 1200 (`DefaultParticleCount`) | 4000 | **fixed** → 1200 |
| Streak length | 0.08 m (`StreakLengthMeters`) | 0.35 m | **fixed** → 0.08 m |
| Fall speed | `(0.08 + seed·0.10) · band` /s | `(0.6 + seed·0.8) · band` /s (~5× too fast) | **fixed** → open-igi formula |
| Streak alpha | `clamp(authored·1.25, 0, 0.28)` | `clamp(authored·4, 0, 0.85)` (~3× too strong) | **fixed** → open-igi response |
| Streak color | RGB (0.8, 0.85, 0.9) | same | match |
| Wrap box | 50 m around camera | 50 m | match |
| Phase seed convention | seed.z → speed, seed.y → phase | same | match |
| Depth state | test-only, no depth write, alpha blend | same | match |

Justified divergence: open-igi draws camera-facing thin quads; the editor keeps its
original GL_LINES streak primitive with the retuned constants above. At 0.006 m
half-width quad vs 1.5 px line the on-screen footprint at gameplay distances is
equivalent; switching primitives would add no parity value.

## Snow implementation (#57)

Ported from open-igi `SnowRenderer.cs`:

- 900 flakes, seeded `mt19937(271828)` (matches `Random(271828)` sequence role)
- Fall speed: `(0.025 + seed·0.04) · band` /s — "slow drifting"
- Sway: `sin(t·0.45 + sx·17)·0.35 m` on X, `cos(t·0.35 + sy·19)·0.35 m` on Y
- Flake footprint 0.045 m; drawn as round point sprites sized perspective-correctly
  from the projection matrix (open-igi uses square billboards of the same footprint —
  visually identical at retail flake size)
- Color RGB (0.92, 0.97, 1.0); alpha `clamp(authored·1.75, 0.10, 0.42)`
  (floor keeps flakes visible over white terrain, per open-igi comment for level 7)
- Same authored band, speed multiplier, and indoors suppression as rain

## User settings (r_weather_* semantics)

| Editor control | open-igi cvar | Default |
|---|---|---|
| `[X] Weather` checkbox (pause menu) | `r_weather_enabled` | On |
| `Weather: [Auto/Rain/Snow]` row | `r_weather_kind` | Auto* |
| `Weather Speed [-] N% [+]` spinner (step 25%, clamp 0–200) | `r_weather_speed` | 100 % |

\* The editor adds an `Auto` default that follows the level's authored `Is Rain`
flag, so rain levels show rain and snow levels show snow without manual switching;
explicit Rain/Snow overrides are available as in open-igi. Settings persist through
the existing QED config file (`QEDWeatherEnabled/Style/Speed`).

## Tests

`tests/test_weather_params.cpp` pins every ported constant against drift and
verifies the speed/alpha/drift/wrap math against values derived from
open-igi's `RainRendererTests.cs` / `SnowRendererTests.cs` expectations
(fixture-independent, no game assets required).
