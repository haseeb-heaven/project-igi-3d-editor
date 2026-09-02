#pragma once

namespace igi {

// RainEffect is authored at the level scope. Visual model bounds are not
// weather-volume data and must not suppress an enabled effect.
constexpr bool ShouldRenderAuthoredWeather(
    bool effectActive, bool visualBuildingBoundsOverlap) noexcept {
    (void)visualBuildingBoundsOverlap;
    return effectActive;
}

// Weather is level-authored, not object-authored: it remains visible when an
// editor filter hides objects, buildings, or props.
constexpr bool ShouldDrawWeatherForFrame(bool effectActive,
                                         bool rainRendererReady) noexcept {
    return effectActive && rainRendererReady;
}

} // namespace igi
