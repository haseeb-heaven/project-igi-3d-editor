#pragma once
#include <string>
#include <vector>
#include "menu_qvm.h"

// QVM-driven menu runtime — pure logic (no GL). Mirrors the retail menu-manager
// natives (igi.exe: MenuManager_PushScreen 0x417690 / PopScreen 0x417710 /
// ActivatePopupScreen 0x417900 / DeactivatePopupScreen 0x417960 /
// LeaveMenus 0x417790 / RequestScreen 0x4174A0): a stack of screens plus at most
// one active popup dialog on top.
//
// UI space is the retail 640x480; the GL layer scales/centres it.

namespace igi {

// Retail colour indices observed in the decompiled menu (colour 1 = title green,
// colour 2 = item white). Exposed for tests and the GL layer.
struct MenuColourRgba {
    float r, g, b, a;
};
// Free function (name differs from the struct to avoid shadowing).
MenuColourRgba MenuColourRgbaFn(int colour_index);

// One laid-out interactive text row (UI-space rect).
struct MenuItemLayout {
    const MenuItemDef* item = nullptr;
    int x = 0, y = 0, w = 0, h = 0;
    bool selected = false;
};

class MenuRuntimeCore {
public:
    // Parses decompiled menu QSC (QVM_DecompileToString output of
    // menusystem/ingamemenu.qvm). Opens the menu on the main screen (900).
    bool LoadFromQsc(const std::string& decompiled_qsc);
    bool Loaded() const { return loaded_; }

private:
    bool loaded_ = false;

public:
    const MenuDef& def() const { return def_; }

    // ── retail navigation natives ────────────────────────────────────────────
    void ResetToMain();                 // open at the main screen (lowest-id non-dialog)
    void PushScreen(int id);            // MenuManager_PushScreen
    void PopScreen();                   // MenuManager_PopScreen
    void ActivatePopup(int id);         // MenuManager_ActivatePopupScreen
    void DeactivatePopup();             // MenuManager_DeactivatePopupScreen
    void LeaveMenus();                  // MenuManager_LeaveMenus
    bool MenuOpen() const { return open_; }

    int ActiveScreenId() const;         // popup id when active, else stack top
    const MenuScreenDef* ActiveScreen() const;

    // ── layout + input ───────────────────────────────────────────────────────
    // Lays out the active screen's items into UI-space rects. Layout follows the
    // authored MenuFrame rects: text items stack inside their frame per the
    // frame's LineUpMode (1 = vertical list, 0 = single title row — documented
    // best-effort; the retail lineup semantics were not symbol-verified).
    // `char_advance`/`line_height` approximate font metrics in the pure core;
    // the GL layer passes measured values from the loaded FNT.
    std::vector<MenuItemLayout> LayoutActive(int char_advance = 8, int line_height = 16) const;

    // Returns the index into `layout` of the clicked text item with a click
    // script, or -1.
    static int HitTestText(const std::vector<MenuItemLayout>& layout, int x, int y);

    // ── retail click scripts ─────────────────────────────────────────────────
    enum class Action {
        None, PushScreen, PopScreen, ActivatePopup, DeactivatePopup, LeaveMenus
    };
    struct ScriptEffect { Action action = Action::None; int arg = 0; };

    // Parses one retail menu script (statements separated by ',' or newlines).
    // Unknown statements (Config_*, ControlsMenu_*, variable assignments) yield
    // None effects — the editor logs them as TODO no-ops at run time.
    static std::vector<ScriptEffect> ParseScript(const std::string& script);
    void ExecuteScript(const std::string& script);

private:
    MenuDef def_;
    std::vector<int> stack_;   // screen ids, base first
    int popup_ = -1;
    bool open_ = false;
    int main_screen_id_ = -1;
};

} // namespace igi
