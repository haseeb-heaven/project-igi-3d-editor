#include "menu_qvm_runtime.h"
#include "../logger.h"

#include <algorithm>
#include <cctype>

namespace igi {

MenuColourRgba MenuColourRgbaFn(int colour_index) {
    // Observed in the retail decompile: colour 1 = title (green), 2 = items
    // (white). Other indices fall back to white; refine when symbol data lands.
    switch (colour_index) {
        case 1: return {0.0f, 1.0f, 0.0f, 1.0f};
        case 2: default: return {1.0f, 1.0f, 1.0f, 1.0f};
    }
}

bool MenuRuntimeCore::LoadFromQsc(const std::string& decompiled_qsc) {
    def_ = ParseMenuQsc(decompiled_qsc);
    loaded_ = def_.valid;
    if (loaded_) {
        ResetToMain();
    } else {
        Logger::Get().Log(LogLevel::WARNING,
            "[MenuRuntime] menu QSC parse failed: " + def_.error);
    }
    return loaded_;
}

void MenuRuntimeCore::ResetToMain() {
    stack_.clear();
    popup_ = -1;
    open_ = true;
    // Main screen = lowest-id non-dialog screen (retail 900).
    int best = -1;
    for (const auto& sc : def_.screens) {
        if (sc.is_dialog) continue;
        if (best == -1 || sc.id < best) best = sc.id;
    }
    main_screen_id_ = best;
    if (best >= 0) stack_.push_back(best);
}

void MenuRuntimeCore::PushScreen(int id) {
    if (!def_.FindScreen(id)) {
        Logger::Get().Log(LogLevel::WARNING,
            "[MenuRuntime] PushScreen: unknown screen id " + std::to_string(id));
        return;
    }
    stack_.push_back(id);
    open_ = true;
}

void MenuRuntimeCore::PopScreen() {
    if (popup_ < 0 && stack_.size() > 1) stack_.pop_back();
    popup_ = -1;
    if (stack_.empty()) open_ = false;
}

void MenuRuntimeCore::ActivatePopup(int id) {
    if (!def_.FindScreen(id)) {
        Logger::Get().Log(LogLevel::WARNING,
            "[MenuRuntime] ActivatePopup: unknown dialog id " + std::to_string(id));
        return;
    }
    popup_ = id;
}

void MenuRuntimeCore::DeactivatePopup() { popup_ = -1; }

void MenuRuntimeCore::LeaveMenus() {
    stack_.clear();
    popup_ = -1;
    open_ = false;
}

int MenuRuntimeCore::ActiveScreenId() const {
    if (popup_ >= 0) return popup_;
    return stack_.empty() ? -1 : stack_.back();
}

const MenuScreenDef* MenuRuntimeCore::ActiveScreen() const {
    return def_.FindScreen(ActiveScreenId());
}

std::vector<MenuItemLayout> MenuRuntimeCore::LayoutActive(
    int char_advance, int line_height) const {
    std::vector<MenuItemLayout> out;
    const MenuScreenDef* sc = ActiveScreen();
    if (!sc) return out;

    // Walk items; frames set the layout cursor, texts consume it. The flat item
    // list is in draw order (background frames first), so a text always follows
    // its owning frame.
    int cursor_x = sc->rect.x0 + 16;
    int cursor_y = sc->rect.y0 + 24;
    int frame_right = sc->rect.x1 - 16;
    int frame_bottom = sc->rect.y1 - 8;
    bool first_in_frame = true;
    int line_h = line_height;

    for (const auto& it : sc->items) {
        if (it.is_frame) {
            cursor_x = it.rect.x0 + 12;
            cursor_y = it.rect.y0 + (it.lineup_mode == 0 ? 4 : 8);
            frame_right = it.rect.x1 - 12;
            frame_bottom = it.rect.y1 - 6;
            first_in_frame = true;
            continue;
        }
        if (it.type != "MenuText" && it.type != "MenuTextConditional") continue;
        const int w = static_cast<int>(it.text.size()) * char_advance;
        MenuItemLayout l;
        l.item = &it;
        if (it.lineup_mode == 2 || (!first_in_frame && it.lineup_mode == 0)) {
            // horizontal continuation
            l.x = cursor_x;
            l.y = cursor_y;
            cursor_x += w + std::max(it.spacing_x, 8);
        } else {
            // vertical list entry
            l.x = cursor_x;
            l.y = cursor_y;
            cursor_y += line_h + std::max(it.spacing_y, 4);
        }
        l.w = w;
        l.h = line_h;
        out.push_back(l);
        first_in_frame = false;
        (void)frame_bottom;
    }
    (void)frame_right;
    return out;
}

int MenuRuntimeCore::HitTestText(const std::vector<MenuItemLayout>& layout,
                                 int x, int y) {
    for (size_t i = 0; i < layout.size(); ++i) {
        const auto& l = layout[i];
        if (!l.item || l.item->click_script.empty()) continue;
        if (x >= l.x && x < l.x + l.w && y >= l.y && y < l.y + l.h) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

std::vector<MenuRuntimeCore::ScriptEffect> MenuRuntimeCore::ParseScript(
    const std::string& script) {
    std::vector<ScriptEffect> effects;
    // Statements separate on ',' or newlines (retail decompile style).
    std::string stmt;
    auto flush_stmt = [&]() {
        size_t b = stmt.find_first_not_of(" \t\r\n");
        if (b == std::string::npos) { stmt.clear(); return; }
        // trim end
        size_t e = stmt.find_last_not_of(" \t\r\n");
        stmt = stmt.substr(b, e - b + 1);

        auto begins = [&](const char* p) { return stmt.rfind(p, 0) == 0; };
        ScriptEffect fx;
        if (begins("MenuManager_PushScreen(")) {
            fx.action = Action::PushScreen;
            const size_t o = stmt.find('(');
            fx.arg = std::atoi(stmt.c_str() + o + 1);
        } else if (begins("MenuManager_PopScreen(")) {
            fx.action = Action::PopScreen;
        } else if (begins("MenuManager_ActivatePopupScreen(")) {
            fx.action = Action::ActivatePopup;
            const size_t o = stmt.find('(');
            fx.arg = std::atoi(stmt.c_str() + o + 1);
        } else if (begins("MenuManager_DeactivatePopu") ||
                   begins("MenuManager_DeactivatePopup")) {
            fx.action = Action::DeactivatePopup;
        } else if (begins("MenuManager_LeaveMenus(")) {
            fx.action = Action::LeaveMenus;
        }
        effects.push_back(fx);
        stmt.clear();
    };
    int paren_depth = 0;
    for (const char ch : script) {
        if (ch == '(') ++paren_depth;
        if (ch == ')') --paren_depth;
        // Split only on statement separators OUTSIDE call parens — retail scripts
        // carry multi-arg calls like MenuManager_LeaveMenus(900, MENUMANAGER_IDLE).
        if ((ch == ',' && paren_depth <= 0) || ch == '\n') flush_stmt();
        else stmt += ch;
    }
    flush_stmt();
    return effects;
}

void MenuRuntimeCore::ExecuteScript(const std::string& script) {
    for (const auto& fx : ParseScript(script)) {
        switch (fx.action) {
            case Action::PushScreen:     PushScreen(fx.arg); break;
            case Action::PopScreen:      PopScreen(); break;
            case Action::ActivatePopup:  ActivatePopup(fx.arg); break;
            case Action::DeactivatePopup: DeactivatePopup(); break;
            case Action::LeaveMenus:     LeaveMenus(); break;
            case Action::None:
                Logger::Get().Log(LogLevel::DEBUG,
                    "[MenuRuntime] TODO no-op script statement (editor config "
                    "equivalent pending): " + script.substr(0, 64));
                break;
        }
    }
}

} // namespace igi
