#include "mission_state_loader.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <string>
#include <utility>

namespace igi {
namespace {

constexpr size_t kPositionArgumentIndex = 3;
constexpr size_t kOrientationArgumentIndex = 6;
constexpr size_t kAreaDimensionsArgumentIndex = 9;
constexpr size_t kAreaCriteriaArgumentIndex = 12;
constexpr size_t kEditInitialValueArgumentIndex = 6;
constexpr size_t kEditAddArgumentIndex = 7;
constexpr size_t kEditSubtractArgumentIndex = 8;
constexpr size_t kLevelTimerOnArgumentIndex = 9;
constexpr size_t kLevelTimerResetArgumentIndex = 10;
constexpr size_t kLevelTimerInitialRunArgumentIndex = 11;
constexpr size_t kCutSceneRunArgumentIndex = 9;
constexpr size_t kCutSceneResetArgumentIndex = 10;
constexpr size_t kCutSceneTimeDeltaArgumentIndex = 11;
constexpr size_t kCutSceneStartTimeArgumentIndex = 12;
constexpr size_t kCutSceneInitialRunArgumentIndex = 13;
constexpr size_t kCutSceneTimeScaleArgumentIndex = 14;
constexpr size_t kCutSceneViewportHeightArgumentIndex = 15;
constexpr size_t kCutSceneViewportFadeInArgumentIndex = 16;
constexpr size_t kCutSceneViewportFadeOutArgumentIndex = 17;
constexpr size_t kCutSceneTimeOfDayArgumentIndex = 18;
constexpr size_t kCutSceneStartExpressionArgumentIndex = 19;
constexpr size_t kCutSceneStopExpressionArgumentIndex = 20;
constexpr size_t kStatusMessageSendArgumentIndex = 9;
constexpr size_t kStatusMessageTextArgumentIndex = 10;
constexpr size_t kStatusMessageSoundArgumentIndex = 12;
constexpr size_t kStatusMessageSendOnceArgumentIndex = 13;
constexpr size_t kStatusMessageCutsceneArgumentIndex = 14;
constexpr size_t kStatusMessageDurationArgumentIndex = 15;

std::string UnquoteToken(std::string token) {
    if (token.size() < 2 || token.front() != '"' || token.back() != '"') {
        return token;
    }

    token = token.substr(1, token.size() - 2);
    std::string unquoted;
    unquoted.reserve(token.size());
    bool escaped = false;
    for (const char character : token) {
        if (escaped) {
            unquoted.push_back(character);
            escaped = false;
        } else if (character == '\\') {
            escaped = true;
        } else {
            unquoted.push_back(character);
        }
    }
    if (escaped) {
        unquoted.push_back('\\');
    }
    return unquoted;
}

bool TryParseNumber(const std::string& token, float& value) {
    const std::string unquoted_token = UnquoteToken(token);
    size_t parsed_characters = 0;
    try {
        value = std::stof(unquoted_token, &parsed_characters);
    } catch (...) {
        return false;
    }
    return parsed_characters == unquoted_token.size() && std::isfinite(value);
}

bool TryParseInteger(const std::string& token, int& value) {
    const std::string unquoted_token = UnquoteToken(token);
    const char* begin = unquoted_token.data();
    const char* end = begin + unquoted_token.size();
    const auto result = std::from_chars(begin, end, value);
    return result.ec == std::errc() && result.ptr == end;
}

bool TryParseBoolean(const std::string& token, bool& value) {
    const std::string unquoted_token = UnquoteToken(token);
    if (unquoted_token == "TRUE" || unquoted_token == "true" ||
        unquoted_token == "1") {
        value = true;
        return true;
    }
    if (unquoted_token == "FALSE" || unquoted_token == "false" ||
        unquoted_token == "0") {
        value = false;
        return true;
    }
    return false;
}

std::string TokenAt(
    const std::vector<std::string>& tokens,
    size_t index) {
    return index < tokens.size() ? UnquoteToken(tokens[index]) : std::string();
}

bool ReadVector3(
    const std::vector<std::string>& tokens,
    size_t first_index,
    glm::vec3& value) {
    float components[3] = {};
    for (size_t component_index = 0; component_index < 3; ++component_index) {
        if (first_index + component_index >= tokens.size() ||
            !TryParseNumber(tokens[first_index + component_index], components[component_index])) {
            return false;
        }
    }
    value = glm::vec3(components[0], components[1], components[2]);
    return true;
}

} // namespace

AuthoredMissionStateDefinitions LoadAuthoredMissionStateDefinitions(
    const std::vector<MissionStateTaskSource>& task_sources) {
    AuthoredMissionStateDefinitions definitions;

    for (const MissionStateTaskSource& task_source : task_sources) {
        if (task_source.task_id.empty() || task_source.task_id == "-1") {
            continue;
        }

        if (task_source.task_type == "AreaActivate") {
            AuthoredMissionAreaActivation area;
            area.task_id = task_source.task_id;
            if (!ReadVector3(
                    task_source.argument_tokens,
                    kPositionArgumentIndex,
                    area.position) ||
                !ReadVector3(
                    task_source.argument_tokens,
                    kOrientationArgumentIndex,
                    area.orientation) ||
                !ReadVector3(
                    task_source.argument_tokens,
                    kAreaDimensionsArgumentIndex,
                    area.dimensions)) {
                continue;
            }
            area.criteria = TokenAt(
                task_source.argument_tokens,
                kAreaCriteriaArgumentIndex);
            definitions.area_activations.push_back(std::move(area));
            continue;
        }

        if (task_source.task_type == "EditVariable") {
            AuthoredMissionEditVariable edit_variable;
            edit_variable.task_id = task_source.task_id;
            if (task_source.argument_tokens.size() <= kEditSubtractArgumentIndex ||
                !TryParseInteger(
                    task_source.argument_tokens[kEditInitialValueArgumentIndex],
                    edit_variable.initial_value)) {
                continue;
            }
            edit_variable.add_expression = TokenAt(
                task_source.argument_tokens,
                kEditAddArgumentIndex);
            edit_variable.subtract_expression = TokenAt(
                task_source.argument_tokens,
                kEditSubtractArgumentIndex);
            definitions.edit_variables.push_back(std::move(edit_variable));
            continue;
        }

        if (task_source.task_type == "LevelTimer") {
            if (task_source.argument_tokens.size() <=
                    kLevelTimerInitialRunArgumentIndex) {
                continue;
            }

            AuthoredMissionLevelTimer level_timer;
            level_timer.task_id = task_source.task_id;
            level_timer.on_expression = TokenAt(
                task_source.argument_tokens,
                kLevelTimerOnArgumentIndex);
            level_timer.reset_expression = TokenAt(
                task_source.argument_tokens,
                kLevelTimerResetArgumentIndex);
            if (!TryParseBoolean(
                    task_source.argument_tokens[kLevelTimerInitialRunArgumentIndex],
                    level_timer.initial_run)) {
                continue;
            }
            definitions.level_timers.push_back(std::move(level_timer));
            continue;
        }

        if (task_source.task_type == "CutScene") {
            if (task_source.argument_tokens.size() <=
                    kCutSceneStopExpressionArgumentIndex) {
                continue;
            }

            AuthoredMissionCutScene cut_scene;
            cut_scene.task_id = task_source.task_id;
            cut_scene.run_expression = TokenAt(
                task_source.argument_tokens,
                kCutSceneRunArgumentIndex);
            cut_scene.reset_expression = TokenAt(
                task_source.argument_tokens,
                kCutSceneResetArgumentIndex);
            cut_scene.time_delta_expression = TokenAt(
                task_source.argument_tokens,
                kCutSceneTimeDeltaArgumentIndex);
            cut_scene.start_expression = TokenAt(
                task_source.argument_tokens,
                kCutSceneStartExpressionArgumentIndex);
            cut_scene.stop_expression = TokenAt(
                task_source.argument_tokens,
                kCutSceneStopExpressionArgumentIndex);
            if (!TryParseNumber(
                    task_source.argument_tokens[kCutSceneStartTimeArgumentIndex],
                    cut_scene.start_time_seconds) ||
                !TryParseBoolean(
                    task_source.argument_tokens[kCutSceneInitialRunArgumentIndex],
                    cut_scene.initial_run) ||
                !TryParseNumber(
                    task_source.argument_tokens[kCutSceneTimeScaleArgumentIndex],
                    cut_scene.time_scale) ||
                !TryParseNumber(
                    task_source.argument_tokens[kCutSceneViewportHeightArgumentIndex],
                    cut_scene.viewport_height_factor) ||
                !TryParseNumber(
                    task_source.argument_tokens[kCutSceneViewportFadeInArgumentIndex],
                    cut_scene.viewport_fade_in_seconds) ||
                !TryParseNumber(
                    task_source.argument_tokens[kCutSceneViewportFadeOutArgumentIndex],
                    cut_scene.viewport_fade_out_seconds) ||
                !TryParseNumber(
                    task_source.argument_tokens[kCutSceneTimeOfDayArgumentIndex],
                    cut_scene.time_of_day)) {
                continue;
            }
            cut_scene.start_time_seconds = std::max(
                0.0f,
                cut_scene.start_time_seconds);
            cut_scene.time_scale = std::max(0.0f, cut_scene.time_scale);
            cut_scene.viewport_height_factor = std::max(
                0.0f,
                cut_scene.viewport_height_factor);
            cut_scene.viewport_fade_in_seconds = std::max(
                0.0f,
                cut_scene.viewport_fade_in_seconds);
            cut_scene.viewport_fade_out_seconds = std::max(
                0.0f,
                cut_scene.viewport_fade_out_seconds);
            cut_scene.duration_seconds = std::max(
                0.0f,
                task_source.authored_duration_seconds);
            cut_scene.camera_shots = task_source.authored_camera_shots;
            definitions.cut_scenes.push_back(std::move(cut_scene));
            continue;
        }

        if (task_source.task_type == "StatusMessage") {
            if (task_source.argument_tokens.size() <=
                    kStatusMessageDurationArgumentIndex) {
                continue;
            }

            AuthoredMissionStatusMessage status_message;
            status_message.task_id = task_source.task_id;
            status_message.send_expression = TokenAt(
                task_source.argument_tokens,
                kStatusMessageSendArgumentIndex);
            status_message.text_resource = TokenAt(
                task_source.argument_tokens,
                kStatusMessageTextArgumentIndex);
            status_message.sound_name = TokenAt(
                task_source.argument_tokens,
                kStatusMessageSoundArgumentIndex);
            if (!TryParseBoolean(
                    task_source.argument_tokens[kStatusMessageSendOnceArgumentIndex],
                    status_message.send_once) ||
                !TryParseBoolean(
                    task_source.argument_tokens[kStatusMessageCutsceneArgumentIndex],
                    status_message.cutscene_message) ||
                !TryParseNumber(
                    task_source.argument_tokens[kStatusMessageDurationArgumentIndex],
                    status_message.duration_seconds)) {
                continue;
            }
            status_message.duration_seconds = std::max(
                0.0f,
                status_message.duration_seconds);
            definitions.status_messages.push_back(std::move(status_message));
        }
    }

    return definitions;
}

} // namespace igi
