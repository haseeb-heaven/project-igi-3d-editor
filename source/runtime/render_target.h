#pragma once

namespace igi {

enum class RenderTarget {
    Editor,
    Gameplay,
};

// An active gameplay session can still receive editor-window paint messages.
// Those messages must render the authoring surface without becoming a second
// gameplay update/render target.
constexpr RenderTarget ResolveRenderTarget(
    bool gameplay_mode_active,
    bool rendering_editor_window) noexcept {
    return gameplay_mode_active && !rendering_editor_window
        ? RenderTarget::Gameplay
        : RenderTarget::Editor;
}

} // namespace igi
