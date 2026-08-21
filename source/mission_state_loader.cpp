#include "mission_state_loader.h"

#include <charconv>
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
    return parsed_characters == unquoted_token.size();
}

bool TryParseInteger(const std::string& token, int& value) {
    const std::string unquoted_token = UnquoteToken(token);
    const char* begin = unquoted_token.data();
    const char* end = begin + unquoted_token.size();
    const auto result = std::from_chars(begin, end, value);
    return result.ec == std::errc() && result.ptr == end;
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
        }
    }

    return definitions;
}

} // namespace igi
