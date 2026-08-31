#pragma once

namespace igi {

// The editor pause panel keeps QED's green visual language while using the
// compact centered framing of the retail menu system.
constexpr int kPauseMenuWidth = 460;
constexpr int kPauseMenuHeight = 380;
constexpr int kPauseMenuFirstRowOffset = 105;
constexpr int kPauseMenuRowHeight = 30;
constexpr int kPauseMenuRowHitRadius = 12;

constexpr int PauseMenuTop(int viewport_height) noexcept {
    return (viewport_height - kPauseMenuHeight) / 2;
}

enum class PauseMenuPage {
    Main,
};

enum class PauseMenuAction {
    None,
    Resume,
    ToggleGameMode,
    ToggleEditorFont,
    ShowLevelSelectionHint,
    SaveEditorLevel,
};

enum class PauseMenuOutcome {
    KeepOpen,
    Close,
};

struct PauseMenuTransition {
    PauseMenuPage page;
    PauseMenuOutcome outcome;
};

constexpr int PauseMenuRowCenter(int viewport_height, int row_index) noexcept {
    return PauseMenuTop(viewport_height) + kPauseMenuFirstRowOffset +
           row_index * kPauseMenuRowHeight;
}

constexpr bool IsPauseMenuRowHit(int viewport_height, int row_index,
                                 int mouse_y) noexcept {
    const int center = PauseMenuRowCenter(viewport_height, row_index);
    return mouse_y >= center - kPauseMenuRowHitRadius &&
           mouse_y <= center + kPauseMenuRowHitRadius;
}

constexpr int PauseMenuActionCount(PauseMenuPage page) noexcept {
    switch (page) {
    case PauseMenuPage::Main: return 5;
    }
    return 0;
}

constexpr PauseMenuAction PauseMenuActionForRow(PauseMenuPage page,
                                                int row) noexcept {
    if (row < 0 || row >= PauseMenuActionCount(page)) return PauseMenuAction::None;
    switch (page) {
    case PauseMenuPage::Main:
        switch (row) {
        case 0: return PauseMenuAction::Resume;
        case 1: return PauseMenuAction::ToggleGameMode;
        case 2: return PauseMenuAction::ToggleEditorFont;
        case 3: return PauseMenuAction::ShowLevelSelectionHint;
        case 4: return PauseMenuAction::SaveEditorLevel;
        }
        break;
    }
    return PauseMenuAction::None;
}

constexpr PauseMenuTransition ApplyPauseMenuAction(PauseMenuPage page,
                                                    PauseMenuAction action) noexcept {
    switch (action) {
    case PauseMenuAction::Resume:
        return {PauseMenuPage::Main, PauseMenuOutcome::Close};
    case PauseMenuAction::ToggleGameMode:
    case PauseMenuAction::ToggleEditorFont:
    case PauseMenuAction::ShowLevelSelectionHint:
    case PauseMenuAction::SaveEditorLevel:
        return {page, PauseMenuOutcome::KeepOpen};
    case PauseMenuAction::None:
        break;
    }
    return {page, PauseMenuOutcome::KeepOpen};
}

constexpr bool IsPauseMenuInputActive(bool pause_menu_open) noexcept {
    return pause_menu_open;
}

constexpr bool IsEditorInteractionActive(bool pause_menu_open,
                                          bool in_game_mode) noexcept {
    return !pause_menu_open && !in_game_mode;
}

} // namespace igi
