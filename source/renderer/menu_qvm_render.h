#pragma once
#include <string>
#include "menu_qvm_runtime.h"

// GL layer for the QVM-driven menu (#74): asset loading (sprites via MenuAssets,
// fonts via FNT_Parse from the menusystem packs) plus immediate-mode drawing of
// the active screen at the retail 640x480 UI resolution scaled/centred into the
// viewport. Navigation/input delegate to MenuRuntimeCore.
//
// Integration contract for renderer_draw.cpp:
//   if (MenuRender::Get().Available()) {
//     MenuRender::Get().DrawAndInput(vw, vh, mouse_x, mouse_y, click_pending);
//     // click_pending: pass the legacy pause_active_input_ flag when a click
//     // landed inside the menu; it is consumed (set to -1) here.
//   }
// When !Available() (no game root / parse failure), the hand-drawn fallback menu
// stays in charge — byte-identical behaviour.

namespace igi {

class MenuRender {
public:
    static MenuRender& Get();

    // True once the retail menu QVM + required fonts loaded successfully.
    bool Available() const { return available_; }

    // Attempts lazy load from the given game root (Utils::GetIGIRootPath()).
    // Safe to call repeatedly; only the first successful call loads.
    bool EnsureLoaded(const std::string& game_root);

    // Draws the menu (if visible). Returns true when it drew (caller skips fallback).
    bool Draw(int viewport_w, int viewport_h, int mouse_x, int mouse_y);

    // Hit-tests a viewport-space click against the active screen's texts and runs
    // the bound retail script. Returns true when the click was consumed.
    bool OnClick(int viewport_w, int viewport_h, int x, int y);

    // Exposed for tests / future key handling.
    MenuRuntimeCore& core() { return core_; }

private:
    MenuRender() = default;

    bool LoadFonts(const std::string& game_root);

    MenuRuntimeCore core_;
    bool available_ = false;
    bool load_attempted_ = false;
    int last_mouse_x_ = -1, last_mouse_y_ = -1;
};

} // namespace igi
