#pragma once

namespace igi {

// Describes one synchronous progress-overlay presentation pass. Loading a
// level or lightmaps runs synchronously on the main loop thread; the overlay
// repaints + presents the bar at each milestone and then pumps window events
// so the OS does not flag the window as unresponsive.
struct ProgressOverlayState {
    bool overlay_visible = false;   // overlay currently owns the GL surface
    bool redisplay_pending = false; // a pumped event queued a scene repaint
};

// A pending scene repaint must be suppressed while the overlay owns the
// surface: App::OnDisplay would otherwise draw the level scene over the
// overlay's back buffer AND swap it, so the bar is never seen.
inline bool ShouldSuppressSceneRepaint(const ProgressOverlayState& state) {
    return state.overlay_visible;
}

// The overlay frame must be presented (swap) BEFORE pumping window events.
// Pumping first lets a queued scene repaint overwrite the overlay between the
// draw and the swap, which is why the bar appeared to never show up.
inline bool ShouldPresentOverlayBeforePumpingEvents(const ProgressOverlayState& state) {
    return state.overlay_visible;
}

} // namespace igi
