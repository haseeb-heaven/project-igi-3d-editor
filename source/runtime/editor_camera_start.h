#pragma once

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

namespace igi {

struct EditorCameraPose {
    glm::vec3 position{0.0f};
    float yaw = 0.0f;
    float pitch = 0.0f;
};

// Place the editor outside a selected model and aim toward its center.  The
// distance is derived from the model's transformed bounds so large authored
// buildings are not entered by the navigation command.
inline EditorCameraPose FocusCameraOnModel(
    const glm::vec3& target, float bound_radius) noexcept {
    const float radius = std::clamp(bound_radius * 1.4f, 500.0f, 500000.0f);
    const float height = radius * 0.3f;
    return {target + glm::vec3(0.0f, -radius, height),
            0.0f,
            -std::atan2(height, radius) * 57.29577951308232f};
}

// Returns whether a persisted editor camera may be reused for this load.
inline bool ShouldUseSavedEditorCamera(
    int last_loaded_level,
    int requested_level,
    const glm::vec3& saved_position) noexcept {
    const bool has_saved_position = saved_position.x != 0.0f ||
        saved_position.y != 0.0f || saved_position.z != 0.0f;
    return has_saved_position && last_loaded_level == requested_level;
}

} // namespace igi
