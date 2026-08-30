#pragma once

namespace igi {

constexpr int kPauseMenuWidth = 460;
constexpr int kPauseMenuHeight = 714;
constexpr int kPauseMenuFirstRowOffset = 90;

constexpr int PauseMenuTop(int viewportHeight) noexcept {
    return (viewportHeight - kPauseMenuHeight) / 2;
}

constexpr int PauseMenuRowHeight(bool terrainOptionsExpanded) noexcept {
    return terrainOptionsExpanded ? 29 : 38;
}

constexpr int PauseMenuRowHitRadius(bool terrainOptionsExpanded) noexcept {
    return terrainOptionsExpanded ? 12 : 16;
}

constexpr int PauseMenuRowCenter(int viewportHeight, bool terrainOptionsExpanded,
                                 int rowIndex) noexcept {
    return PauseMenuTop(viewportHeight) + kPauseMenuFirstRowOffset +
           rowIndex * PauseMenuRowHeight(terrainOptionsExpanded);
}

constexpr bool IsPauseMenuRowHit(int viewportHeight, bool terrainOptionsExpanded,
                                 int rowIndex, int mouseY) noexcept {
    const int center = PauseMenuRowCenter(viewportHeight, terrainOptionsExpanded, rowIndex);
    const int radius = PauseMenuRowHitRadius(terrainOptionsExpanded);
    return mouseY >= center - radius && mouseY <= center + radius;
}

} // namespace igi
