#pragma once

namespace igi {

constexpr int kPauseMenuWidth = 460;
constexpr int kPauseMenuHeight = 520;
constexpr int kPauseMenuFirstRowOffset = 85;

constexpr int PauseMenuTop(int viewport_height) noexcept {
    return (viewport_height - kPauseMenuHeight) / 2;
}

constexpr int PauseMenuRowHeight(bool terrain_options_expanded) noexcept {
    return terrain_options_expanded ? 29 : 35;
}

constexpr int PauseMenuRowHitRadius(bool terrain_options_expanded) noexcept {
    return terrain_options_expanded ? 12 : 15;
}

constexpr int PauseMenuRowCenter(int viewport_height,
                                 bool terrain_options_expanded,
                                 int row_index) noexcept {
    return PauseMenuTop(viewport_height) + kPauseMenuFirstRowOffset +
           row_index * PauseMenuRowHeight(terrain_options_expanded);
}

constexpr bool IsPauseMenuRowHit(int viewport_height,
                                 bool terrain_options_expanded,
                                 int row_index,
                                 int mouse_y) noexcept {
    const int center = PauseMenuRowCenter(
        viewport_height, terrain_options_expanded, row_index);
    const int radius = PauseMenuRowHitRadius(terrain_options_expanded);
    return mouse_y >= center - radius && mouse_y <= center + radius;
}

constexpr bool IsPauseMenuInputActive(bool pause_menu_open) noexcept {
    return pause_menu_open;
}

constexpr bool IsEditorInteractionActive(bool pause_menu_open,
                                          bool in_game_mode) noexcept {
    return !pause_menu_open && !in_game_mode;
}

struct PauseLevelSelection {
    bool valid;
    bool leave_gameplay;
    int level;
};

constexpr PauseLevelSelection ResolvePauseLevelSelection(
    int level, bool in_game_mode) noexcept {
    return {level >= 1 && level <= 14, in_game_mode, level};
}

} // namespace igi
