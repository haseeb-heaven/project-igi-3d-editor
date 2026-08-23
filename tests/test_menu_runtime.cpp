// Unit tests for the QVM menu runtime core (#74): retail screen-stack
// navigation, layout, hit-testing, and click-script parsing.
#include <gtest/gtest.h>
#include "../source/renderer/menu_qvm_runtime.h"

using namespace igi;

namespace {
// Synthetic two-screen menu mirroring the retail 900/902 shape.
const char* kMenuQsc =
    "Task_New(-1, \"MenuManager\", \"mgr\", 900, \"LOCAL:menusystem/ingamemenu.res\", FALSE,\n"
    "  Task_New(900, \"MenuScreen\", \"Main Menu\", \"\", 85, 80, 555, 410, \"MenuManager_LeaveMenus(900, MENUMANAGER_IDLE)\", \"\", \"\",\n"
    "    Task_New(-1, \"MenuFrame\", \"\", \"\", 100, 97, 440, 200, 1, 4, 4, FALSE,\n"
    "      Task_New(-1, \"MenuText\", \"resume\", \"Resume Game\", \"font3.fnt\", 2, \"MenuManager_LeaveMenus(900, MENUMANAGER_IDLE)\", \"\"),\n"
    "      Task_New(-1, \"MenuText\", \"sound\", \"Sound\", \"font3.fnt\", 2, \"MenuManager_PushScreen(902)\\n\", \"\"))),\n"
    "  Task_New(902, \"MenuScreen\", \"Sound Configuration\", \"mainmenu.pic\", 85, 80, 555, 410, \"MenuManager_PopScreen(FALSE)\", \"\", \"\",\n"
    "    Task_New(-1, \"MenuFrame\", \"\", \"\", 100, 97, 440, 100, 1, 4, 4, FALSE,\n"
    "      Task_New(-1, \"MenuText\", \"ok\", \"OK\", \"font3.fnt\", 2, \"MenuManager_PopScreen(TRUE)\\n\", \"menu_ok\"),\n"
    "      Task_New(-1, \"MenuText\", \"cancel\", \"Cancel\", \"font3.fnt\", 2, \"MenuManager_PopScreen(FALSE)\\n\", \"menu_cancel\"))),\n"
    "  Task_New(931, \"DialogWindow\", \"quit to main\", 220, 180, 200, 120, TRUE,\n"
    "    Task_New(-1, \"MenuFrame\", \"\", \"\", 200, 200, 240, 20, 2, 4, 0, FALSE,\n"
    "      Task_New(-1, \"MenuText\", \"no\", \"No\", \"font3.fnt\", 2, \"MenuManager_DeactivatePopuScreen()\\n\", \"menu_cancel\"))));\n";
} // namespace

TEST(MenuRuntimeTest, LoadsAndOpensAtMainScreen) {
    MenuRuntimeCore rt;
    EXPECT_TRUE(rt.LoadFromQsc(kMenuQsc));
    EXPECT_TRUE(rt.MenuOpen());
    EXPECT_EQ(rt.ActiveScreenId(), 900);
}

TEST(MenuRuntimeTest, PushScreenViaClickScriptNavigates) {
    MenuRuntimeCore rt;
    rt.LoadFromQsc(kMenuQsc);
    rt.ExecuteScript("MenuManager_PushScreen(902)");
    EXPECT_EQ(rt.ActiveScreenId(), 902);
}

TEST(MenuRuntimeTest, PopScreenReturnsToCaller) {
    MenuRuntimeCore rt;
    rt.LoadFromQsc(kMenuQsc);
    rt.PushScreen(902);
    rt.ExecuteScript("MenuManager_PopScreen(TRUE)");
    EXPECT_EQ(rt.ActiveScreenId(), 900);
}

TEST(MenuRuntimeTest, PopupOverlaysAndDeactivates) {
    MenuRuntimeCore rt;
    rt.LoadFromQsc(kMenuQsc);
    rt.ActivatePopup(931);
    EXPECT_EQ(rt.ActiveScreenId(), 931); // dialog on top of 900
    rt.ExecuteScript("MenuManager_DeactivatePopuScreen()");
    EXPECT_EQ(rt.ActiveScreenId(), 900);
}

TEST(MenuRuntimeTest, LeaveMenusCloses) {
    MenuRuntimeCore rt;
    rt.LoadFromQsc(kMenuQsc);
    rt.ExecuteScript("MenuManager_LeaveMenus(900, MENUMANAGER_IDLE)");
    EXPECT_FALSE(rt.MenuOpen());
}

TEST(MenuRuntimeTest, LayoutPlacesTextsInsideTheirFrame) {
    MenuRuntimeCore rt;
    rt.LoadFromQsc(kMenuQsc);
    const auto layout = rt.LayoutActive(/*char_advance=*/8, /*line_height=*/16);
    ASSERT_GE(layout.size(), 2u);
    // First text is the frame's first entry: inside frame rect (100,97,440x200).
    for (const auto& l : layout) {
        EXPECT_GE(l.x, 100);
        EXPECT_GE(l.y, 97);
        EXPECT_LE(l.y, 297);
    }
    // Vertical list: second text below the first.
    EXPECT_GT(layout[1].y, layout[0].y);
}

TEST(MenuRuntimeTest, HitTestFindsClickableTextAndClickNavigates) {
    MenuRuntimeCore rt;
    rt.LoadFromQsc(kMenuQsc);
    const auto layout = rt.LayoutActive(8, 16);
    // Find the "Sound" row (second clickable).
    int sound_idx = -1;
    for (size_t i = 0; i < layout.size(); ++i)
        if (layout[i].item->text == "Sound") sound_idx = static_cast<int>(i);
    ASSERT_GE(sound_idx, 0);
    const int cx = layout[sound_idx].x + 2;
    const int cy = layout[sound_idx].y + 2;
    EXPECT_EQ(rt.HitTestText(layout, cx, cy), sound_idx);
    // Miss test.
    EXPECT_EQ(rt.HitTestText(layout, cx, cy + 5000), -1);

    // Click executes the bound script -> navigates to 902.
    const int hit = rt.HitTestText(layout, cx, cy);
    rt.ExecuteScript(layout[hit].item->click_script);
    EXPECT_EQ(rt.ActiveScreenId(), 902);
}

TEST(MenuRuntimeTest, ParseScriptSplitsStatements) {
    const auto fx = MenuRuntimeCore::ParseScript(
        "ControlsMenu_954.isReset = 1, \nToggleBox_957.isOn = FALSE ,\n"
        "MenuManager_DeactivatePopuScreen()");
    ASSERT_EQ(fx.size(), 3u);
    EXPECT_EQ(fx[0].action, MenuRuntimeCore::Action::None);   // editor TODO no-op
    EXPECT_EQ(fx[1].action, MenuRuntimeCore::Action::None);
    EXPECT_EQ(fx[2].action, MenuRuntimeCore::Action::DeactivatePopup);
}

TEST(MenuRuntimeTest, ParseScriptLeaveMenusWithArgs) {
    const auto fx = MenuRuntimeCore::ParseScript("MenuManager_LeaveMenus(900, MENUMANAGER_IDLE)");
    ASSERT_EQ(fx.size(), 1u);
    EXPECT_EQ(fx[0].action, MenuRuntimeCore::Action::LeaveMenus);
}

TEST(MenuRuntimeTest, ColourIndexMapping) {
    const auto title = MenuColourRgbaFn(1);
    EXPECT_FLOAT_EQ(title.g, 1.0f); // title green
    const auto item = MenuColourRgbaFn(2);
    EXPECT_FLOAT_EQ(item.r, 1.0f);  // item white
}
