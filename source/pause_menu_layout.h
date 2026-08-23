#pragma once

// Pause-menu layout shared between the renderer (what is drawn) and the mouse
// handler (what is hit-tested). These MUST stay a single source of truth —
// when they diverge, click zones drift away from drawn rows (#64 round-1
// finding: hit-test height 676 vs drawn 790 left every row ~57px off).

namespace igi {

inline constexpr int kPauseMenuWidth = 460;
inline constexpr int kPauseMenuHeight = 790; // +38*3 Weather rows after Lightmaps (+ prior Fog/Lightmaps additions)

} // namespace igi
