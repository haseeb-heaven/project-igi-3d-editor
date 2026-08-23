// Round-trip integrity tests for menu editing + save-back (#74).
// THE requirement: decompile(original) -> parse -> edit -> serialize-back ->
// compile -> parse must reproduce identical screen/item structure.
#include <gtest/gtest.h>
#include <fstream>
#include <sstream>
#include "renderer/menu_qvm_edit.h"
#include "level/qvm_parser.h"

namespace {
std::string LoadReference() {
    std::ifstream f("docs/reference/ingamemenu_decompiled.qsc");
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Structural fingerprint: recursive type/head/args/children-count dump.
void Fingerprint(const igi::MenuRawCall& c, std::string& out) {
    if (c.removed) return;
    out += c.type + "|" + c.head_id;
    for (const auto& a : c.raw_args) out += "|" + a;
    out += "\n";
    for (const auto& ch : c.children) Fingerprint(ch, out);
}
} // namespace

TEST(MenuQvmEditTest, SerializeRoundTripPreservesStructure) {
    const std::string original = LoadReference();
    igi::MenuRawCall root;
    std::string preamble, error;
    ASSERT_TRUE(igi::ParseMenuRaw(original, root, preamble, error)) << error;

    const std::string regenerated = igi::MenuSerializeBack(preamble, root);

    // Re-parse the regeneration and compare fingerprints.
    igi::MenuRawCall root2;
    std::string preamble2, error2;
    ASSERT_TRUE(igi::ParseMenuRaw(regenerated, root2, preamble2, error2)) << error2;
    EXPECT_EQ(preamble, preamble2);  // Task_DeclareParameters verbatim

    std::string fp1, fp2;
    Fingerprint(root, fp1);
    Fingerprint(root2, fp2);
    EXPECT_EQ(fp1, fp2);
}

TEST(MenuQvmEditTest, PreamblePreservedVerbatim) {
    const std::string original = LoadReference();
    igi::MenuRawCall root;
    std::string preamble, error;
    ASSERT_TRUE(igi::ParseMenuRaw(original, root, preamble, error));
    const size_t first_task_new = original.find("Task_New(");
    EXPECT_EQ(preamble, original.substr(0, first_task_new));
}

TEST(MenuQvmEditTest, EightScreensWithRetailTitles) {
    const std::string original = LoadReference();
    igi::MenuRawCall root;
    std::string preamble, error;
    ASSERT_TRUE(igi::ParseMenuRaw(original, root, preamble, error));

    int screens = 0;
    bool saw_main = false, saw_graphics = false, saw_sound = false, saw_controls = false;
    for (const auto& sc : root.children) {
        if (sc.type != "MenuScreen" && sc.type != "DialogWindow") continue;
        ++screens;
        // Screen params: [0]=title, [1]=background
        if (sc.raw_args.size() > 1) {
            if (sc.raw_args[0] == "\"Main Menu\"") saw_main = true;
            if (sc.raw_args[0] == "\"Graphics Configuration\"") saw_graphics = true;
            if (sc.raw_args[0] == "\"Sound Configuration\"") saw_sound = true;
            if (sc.raw_args[0] == "\"Controls Configuration\"") saw_controls = true;
            if (sc.raw_args[0] == "\"Main Menu\"")
                EXPECT_EQ(sc.raw_args[1], "\"\"");  // transparent over game
        }
    }
    EXPECT_EQ(screens, 8);
    EXPECT_TRUE(saw_main);
    EXPECT_TRUE(saw_graphics);
    EXPECT_TRUE(saw_sound);
    EXPECT_TRUE(saw_controls);
}

TEST(MenuQvmEditTest, SetTextEditsOnlyTargetToken) {
    const std::string original = LoadReference();
    igi::MenuRawCall root;
    std::string preamble, error;
    ASSERT_TRUE(igi::ParseMenuRaw(original, root, preamble, error));
    std::string before;
    Fingerprint(root, before);

    // Screen 900 is child index 0; its second frame (child idx 1) holds the items;
    // find the "Restart Game" MenuText by scanning that frame's children.
    ASSERT_FALSE(root.children.empty());
    const igi::MenuRawCall& main_screen = root.children[0];
    ASSERT_GE(main_screen.children.size(), 2u);
    const igi::MenuRawCall& items_frame = main_screen.children[1];
    int restart_idx = -1;
    for (size_t i = 0; i < items_frame.children.size(); ++i) {
        const auto& c = items_frame.children[i];
        if (c.type == "MenuText" && c.raw_args.size() >= 2 &&
            c.raw_args[1] == "\"Restart Game\"")
            restart_idx = static_cast<int>(i);
    }
    ASSERT_NE(restart_idx, -1);

    EXPECT_TRUE(igi::MenuEditSetText(
        root, {0, 1, restart_idx}, "Restart Mission"));
    std::string after;
    Fingerprint(root, after);
    EXPECT_NE(before, after);
    EXPECT_NE(after.find("\"Restart Mission\""), std::string::npos);

    // Unmodified sibling text intact.
    EXPECT_NE(after.find("\"Resume Game\""), std::string::npos);
    EXPECT_NE(after.find("\"Game Paused\""), std::string::npos);
}

TEST(MenuQvmEditTest, AddFrameAndTextThenRemove) {
    const std::string original = LoadReference();
    igi::MenuRawCall root;
    std::string preamble, error;
    ASSERT_TRUE(igi::ParseMenuRaw(original, root, preamble, error));
    const size_t base_children = root.children[0].children.size();

    const int frame_idx =
        igi::MenuEditAddFrame(root, {0}, -1, "", 100, 300, 440, 60, 1, 4, 4, false);
    ASSERT_GE(frame_idx, 0);
    const int text_idx = igi::MenuEditAddText(
        root, {0, frame_idx}, -1, "custom", "Editor Added", "font3.fnt", 2,
        "MenuManager_LeaveMenus(900, MENUMANAGER_IDLE)", "");
    ASSERT_GE(text_idx, 0);

    const std::string out = igi::MenuSerializeBack(preamble, root);
    EXPECT_NE(out.find("\"Editor Added\""), std::string::npos);

    igi::MenuRawCall reparsed;
    std::string pre2, err2;
    ASSERT_TRUE(igi::ParseMenuRaw(out, reparsed, pre2, err2)) << err2;
    ASSERT_FALSE(reparsed.children.empty());
    EXPECT_EQ(reparsed.children[0].children.size(), base_children + 1);

    // Remove the added frame; structure returns to baseline count.
    EXPECT_TRUE(igi::MenuEditRemove(root, {0, frame_idx}));
    EXPECT_EQ(root.children[0].children.size(), base_children);
}

TEST(MenuQvmEditTest, RemoveRejectsBadPaths) {
    const std::string original = LoadReference();
    igi::MenuRawCall root;
    std::string preamble, error;
    ASSERT_TRUE(igi::ParseMenuRaw(original, root, preamble, error));
    EXPECT_FALSE(igi::MenuEditRemove(root, {}));                 // cannot remove root
    EXPECT_FALSE(igi::MenuEditRemove(root, {99}));               // out of range
    EXPECT_FALSE(igi::MenuEditSetText(root, {99}, "x"));         // bad path
}

// Full pipeline: serialize -> Lex/Parse/Compile -> QVM_Parse must yield a valid
// binary (the same path cli_tests.cpp exercises for objects.qsc).
TEST(MenuQvmEditTest, SaveToQvmCompilesAndReparses) {
    const std::string original = LoadReference();
    igi::MenuRawCall root;
    std::string preamble, error;
    ASSERT_TRUE(igi::ParseMenuRaw(original, root, preamble, error)) << error;
    const std::string regenerated = igi::MenuSerializeBack(preamble, root);

    const std::string out = "/tmp/menu_qvm_edit_test.qvm";
    std::string save_err;
    ASSERT_TRUE(igi::MenuSaveToQvm(regenerated, out, save_err)) << save_err;

    QVMFile parsed = QVM_Parse(out);
    EXPECT_TRUE(parsed.valid) << "compiled QVM failed to re-parse: " << parsed.error;
    EXPECT_FALSE(parsed.instructions.empty()) << "compiled QVM has no instructions";
}
