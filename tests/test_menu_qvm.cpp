// Tests for the QVM-driven menu model (#74) against the retail decompile
// preserved at docs/reference/ingamemenu_decompiled.qsc.
#include <gtest/gtest.h>
#include <fstream>
#include <sstream>
#include "renderer/menu_qvm.h"

namespace {
std::string LoadDecompiled() {
    std::ifstream f("docs/reference/ingamemenu_decompiled.qsc");
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
} // namespace

TEST(MenuQvmTest, ParsesRetailDecompile) {
    const igi::MenuDef def = igi::ParseMenuQsc(LoadDecompiled());
    ASSERT_TRUE(def.valid) << def.error;
    EXPECT_EQ(def.screens.size(), 8u);          // 900-903 + 930-933
    EXPECT_EQ(def.manager_id, 900);
    EXPECT_NE(def.resource_path.find("ingamemenu.res"), std::string::npos);
}

TEST(MenuQvmTest, MainMenuScreenMatchesRetailLayout) {
    const igi::MenuDef def = igi::ParseMenuQsc(LoadDecompiled());
    const igi::MenuScreenDef* main = def.FindScreen(900);
    ASSERT_NE(main, nullptr);
    EXPECT_EQ(main->title, "Main Menu");
    EXPECT_TRUE(main->background.empty());      // transparent over game
    EXPECT_EQ(main->rect.x0, 85);
    EXPECT_EQ(main->rect.y0, 80);
    EXPECT_EQ(main->rect.x1, 555);
    EXPECT_EQ(main->rect.y1, 410);
    // Retail items in order.
    int text_count = 0;
    bool has_resume = false, has_quit = false;
    for (const auto& it : main->items) {
        if (it.is_frame || it.text.empty()) continue;
        ++text_count;
        if (it.text == "Resume Game") {
            has_resume = true;
            EXPECT_EQ(it.font, "font3.fnt");
            EXPECT_EQ(it.colour_index, 2);
            EXPECT_NE(it.click_script.find("MenuManager_LeaveMenus"), std::string::npos);
        }
        if (it.text == "Quit to Main Menu") has_quit = true;
    }
    EXPECT_GE(text_count, 5);                   // title + 5 items
    EXPECT_TRUE(has_resume);
    EXPECT_TRUE(has_quit);
}

TEST(MenuQvmTest, GraphicsScreenUsesTiledBackground) {
    const igi::MenuDef def = igi::ParseMenuQsc(LoadDecompiled());
    const igi::MenuScreenDef* gfx = def.FindScreen(901);
    ASSERT_NE(gfx, nullptr);
    EXPECT_EQ(gfx->background, "mainmenu.pic"); // 150-tile sheet reassembled by loader
}

TEST(MenuQvmTest, DialogsAreMarkedAsDialogs) {
    const igi::MenuDef def = igi::ParseMenuQsc(LoadDecompiled());
    for (const int id : {930, 931, 932, 933}) {
        const igi::MenuScreenDef* dlg = def.FindScreen(id);
        ASSERT_NE(dlg, nullptr) << id;
        EXPECT_TRUE(dlg->is_dialog) << id;
    }
}
