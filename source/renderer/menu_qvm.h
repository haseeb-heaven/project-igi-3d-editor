#pragma once
#include <string>
#include <vector>

// QVM-driven in-game menu (#74) — parses the retail menusystem/ingamemenu.qvm
// (via the editor's qvm_parser + qvm_decompiler + qsc_parser round-trip) into a
// typed menu model the renderer draws exactly as igi.exe does.
//
// Retail element reference (transcribed from a decompiled retail ingamemenu.qvm,
// preserved at docs/reference/ingamemenu_decompiled.qsc):
//
//   MenuManager(id, "resource path")                     — root; packs: LOCAL:menusystem/*.res
//   MenuScreen(id, title, background, x0,y0,x1,y1,
//              on-press-escape, on-init, on-change)      — one per menu page
//   MenuFrame(sprite, x,y,w,h, lineupMode, spacingX, spacingY, selectable)
//   MenuText(textResource, fontResource, colourIndex, onClickScript, clickSound)
//   MenuTextConditional(... + isEnabled/isVisible expressions)
//   SlideBar(sprite, horizontal, size, get/set/modify scripts, caret speed, options)
//   ToggleBox(getScript, setScript)
//   ListBox(sprite, font, width, collect/get/set/modify scripts)
//   DialogWindow(x, y, w, h, swapKeys)                    — modal popup screens
//
// Coordinates are 640x480 UI-space pixels (retail UI resolution).

namespace igi {

struct MenuRect { int x0 = 0, y0 = 0, x1 = 0, y1 = 0; };

struct MenuItemDef {
    std::string type;            // "MenuText", "MenuTextConditional", "SlideBar", ...
    std::string sprite;          // sprite resource ("slide", "win", "" = none)
    std::string text;            // display text / text resource
    std::string font;            // "font3.fnt" etc.
    int colour_index = 0;
    std::string click_script;    // bound action script (verbatim from QVM)
    std::string click_sound;     // "menu_ok", "menu_cancel", ""
    // conditional variants
    std::string enabled_expr;
    std::string visible_expr;
    // frame geometry / layout
    MenuRect rect;
    int lineup_mode = 0;
    int spacing_x = 0;
    int spacing_y = 0;
    bool selectable = false;
    bool is_frame = false;       // true for container frames
};

struct MenuScreenDef {
    int id = -1;
    std::string title;
    std::string background;      // "mainmenu.pic" etc.; "" = transparent over game
    MenuRect rect;               // Frame x0/y0/x1/y1
    std::string on_press_escape;
    std::string on_init_data;
    std::string on_change_data;
    bool is_dialog = false;      // DialogWindow
    std::vector<MenuItemDef> items;
};

struct MenuDef {
    bool valid = false;
    std::string error;
    int manager_id = -1;
    std::string resource_path;   // "LOCAL:menusystem/ingamemenu.res"
    std::vector<MenuScreenDef> screens;

    const MenuScreenDef* FindScreen(int id) const;
};

// Parse decompiled menu QSC text (as produced by QVM_DecompileToString on
// ingamemenu.qvm) into the model. Tolerant: unknown task types are skipped with
// their nested trees preserved as raw items where possible.
MenuDef ParseMenuQsc(const std::string& decompiled_qsc);

} // namespace igi
