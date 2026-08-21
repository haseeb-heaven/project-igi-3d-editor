#pragma once

#include <glm/glm.hpp>

#include <cstdint>
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

// Authored LevelTimer state published to the mission expression namespace.
// Tick advancement is owned by RuntimeWorld and reset per gameplay session.
struct AuthoredMissionLevelTimer {
    std::string task_id;
    std::string on_expression;
    std::string reset_expression;
    bool initial_run = false;
};

// Authored StatusMessage definition. display_text is resolved from the
// localized resource by the Windows application before entering the runtime.
struct AuthoredMissionStatusMessage {
    std::string task_id;
    std::string send_expression;
    std::string text_resource;
    std::string display_text;
    std::string sound_name;
    bool send_once = false;
    bool cutscene_message = false;
    float duration_seconds = 2.0f;
};

// Immutable render-facing view of one authored status message currently in a
// retail message slot. The renderer owns no mission state and only receives
// this presentation snapshot.
struct MissionStatusMessageDisplay {
    std::string text;
    uint32_t revealed_characters = 0;
    uint32_t display_frame = 0;
    bool cutscene_message = false;
};

} // namespace igi
