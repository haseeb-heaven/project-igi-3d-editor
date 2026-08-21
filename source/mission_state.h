#pragma once

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace igi {

// Immutable authored mission volume copied into the runtime session. The
// runtime computes nActive from the player's fixed-step position; editor
// objects never become mutable simulation state.
struct AuthoredMissionAreaActivation {
    std::string task_id;
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 orientation = glm::vec3(0.0f);
    glm::vec3 dimensions = glm::vec3(0.0f);
    std::string criteria;
};

// Immutable authored EditVariable task. Its current integer value belongs to
// RuntimeWorld and is reset from initial_value for every new gameplay session.
struct AuthoredMissionEditVariable {
    std::string task_id;
    int initial_value = 0;
    std::string add_expression;
    std::string subtract_expression;
};

} // namespace igi
