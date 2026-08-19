// window_input_router.cpp - Deterministic input routing between Editor and Gameplay windows implementation
#include "window_input_router.h"

namespace igi {

WindowInputRouter::WindowInputRouter() = default;

void WindowInputRouter::SetFocus(WindowFocusTarget target) {
    focus_target_ = target;
    ResetInputState();
}

void WindowInputRouter::ResetInputState() {
    current_cmd_ = PlayerInputCmd();
}

void WindowInputRouter::OnKeyboardKey(int key, bool is_down) {
    if (focus_target_ != WindowFocusTarget::GameplayWindow) return;

    // Standard WASD / Space / C mapping
    switch (key) {
        case 'w': case 'W': current_cmd_.forward = is_down ? 1.0f : (current_cmd_.forward > 0.0f ? 0.0f : current_cmd_.forward); break;
        case 's': case 'S': current_cmd_.forward = is_down ? -1.0f : (current_cmd_.forward < 0.0f ? 0.0f : current_cmd_.forward); break;
        case 'd': case 'D': current_cmd_.strafe = is_down ? 1.0f : (current_cmd_.strafe > 0.0f ? 0.0f : current_cmd_.strafe); break;
        case 'a': case 'A': current_cmd_.strafe = is_down ? -1.0f : (current_cmd_.strafe < 0.0f ? 0.0f : current_cmd_.strafe); break;
        case ' ':           current_cmd_.jump = is_down; break;
        case 'c': case 'C': current_cmd_.crouch = is_down; break;
        case 'r': case 'R': current_cmd_.reload = is_down; break;
        default: break;
    }
}

void WindowInputRouter::OnMouseMove(float delta_x, float delta_y) {
    if (focus_target_ != WindowFocusTarget::GameplayWindow) return;

    float sensitivity = 0.15f;
    current_cmd_.yaw_delta += delta_x * sensitivity;
    current_cmd_.pitch_delta -= delta_y * sensitivity;
}

void WindowInputRouter::OnMouseButton(int button, bool is_down) {
    if (focus_target_ != WindowFocusTarget::GameplayWindow) return;

    if (button == 0) { // Left mouse button
        current_cmd_.fire = is_down;
    }
}

PlayerInputCmd WindowInputRouter::ConsumeGameplayInput() {
    PlayerInputCmd cmd = current_cmd_;
    // Clear momentary deltas
    current_cmd_.yaw_delta = 0.0f;
    current_cmd_.pitch_delta = 0.0f;
    current_cmd_.jump = false;
    current_cmd_.reload = false;
    return cmd;
}

} // namespace igi
