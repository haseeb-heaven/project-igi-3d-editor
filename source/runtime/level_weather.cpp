#include "level_weather.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <optional>

namespace igi {
namespace {

std::string NormalizeToken(std::string token) {
    while (!token.empty() && std::isspace(static_cast<unsigned char>(token.front())))
        token.erase(token.begin());
    while (!token.empty() && std::isspace(static_cast<unsigned char>(token.back())))
        token.pop_back();
    if (token.size() >= 2 && token.front() == '"' && token.back() == '"')
        token = token.substr(1, token.size() - 2);
    while (!token.empty() && std::isspace(static_cast<unsigned char>(token.back())))
        token.pop_back();
    std::transform(token.begin(), token.end(), token.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return token;
}

std::optional<bool> ParseBooleanToken(const std::string& raw) {
    const std::string token = NormalizeToken(raw);
    if (token == "TRUE" || token == "1") return true;
    if (token == "FALSE" || token == "0") return false;
    return std::nullopt;
}

std::optional<float> ParseFiniteFloat(const std::string& raw) {
    try {
        size_t consumed = 0;
        const float value = std::stof(raw, &consumed);
        while (consumed < raw.size() &&
               std::isspace(static_cast<unsigned char>(raw[consumed]))) {
            ++consumed;
        }
        if (consumed != raw.size() || !std::isfinite(value)) return std::nullopt;
        return value;
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace

LevelWeatherSettings ResolveLevelWeather(
    const std::vector<WeatherEffectObject>& objects) {
    for (const WeatherEffectObject& object : objects) {
        if (object.type != "RainEffect" || object.arg_tokens.size() < 8)
            continue;

        const std::optional<bool> is_rain = ParseBooleanToken(object.arg_tokens[3]);
        const std::optional<bool> is_active = ParseBooleanToken(object.arg_tokens[6]);
        const std::optional<float> start = ParseFiniteFloat(object.arg_tokens[4]);
        const std::optional<float> end = ParseFiniteFloat(object.arg_tokens[5]);
        const std::optional<float> alpha = ParseFiniteFloat(object.arg_tokens[7]);
        if (!is_rain || !is_active || !start || !end || !alpha ||
            *start < 0.0f || *end < 0.0f || *start <= *end || *alpha < 0.0f)
            continue;

        if (!*is_active) return {};

        return LevelWeatherSettings{true, !*is_rain, *start, *end, *alpha};
    }
    return {};
}

} // namespace igi
