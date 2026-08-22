#pragma once
#include <string>
#include <vector>

// Menu editing + save-back for the QVM-driven in-game menu (#74).
//
// Design: byte-faithful round-trip. Edits operate on the RAW token tree of the
// decompiled ingamemenu.qvm QSC (not the lossy MenuDef model), so every argument
// the editor does not touch is replayed verbatim — same quoting, same order,
// Task_DeclareParameters preamble preserved byte-for-byte. SerializeBack()
// regenerates Task_New nested form matching the decompiler's layout, and
// SaveToQvm() compiles via the editor's qsc::Lex -> qsc::Parse -> qvm::Compile
// pipeline (same path cli_tests.cpp exercises).
//
// Safety: SaveToQvm never overwrites an existing target without leaving a .bak
// copy alongside it first.

namespace igi {

// One node of the raw menu script tree. Raw args keep their source formatting
// (quotes included); index 0 is the instance-name/title token, matching the
// decompiler output.
struct MenuRawCall {
    std::string head_id;                 // verbatim head token ("900", "-1", ...)
    std::string type;                    // unquoted task type ("MenuScreen", ...)
    std::vector<std::string> raw_args;   // remaining tokens, verbatim
    std::vector<MenuRawCall> children;
    bool removed = false;
};

// Parse decompiled menu QSC into the raw tree. Returns false when no Task_New
// exists (error says why).
bool ParseMenuRaw(const std::string& decompiled_qsc, MenuRawCall& out_root,
                  std::string& preamble, std::string& error);

// ── Edit operations (mutate the raw tree; all indices are child indexes) ────

// Rename/edit an item's display text. For MenuText/MenuTextConditional this is
// raw_args[1] (the Text Resource slot after the instance name); returns false
// when the path/type combination does not exist.
bool MenuEditSetText(MenuRawCall& root, const std::vector<int>& path,
                     const std::string& new_text);

// Edit a raw argument by slot index (post-type tokens). Generic escape hatch.
bool MenuEditSetArg(MenuRawCall& root, const std::vector<int>& path, int arg_index,
                    const std::string& new_raw_token);

// Remove a subtree (frame or any item) at `path` (path addresses the item to
// remove relative to root's children chain).
bool MenuEditRemove(MenuRawCall& root, const std::vector<int>& path);

// Add a MenuFrame under the screen/container at `parent_path`.
// Returns the child index of the newly added frame, or -1 on failure.
int MenuEditAddFrame(MenuRawCall& root, const std::vector<int>& parent_path,
                     int head_id, const std::string& sprite,
                     int x, int y, int w, int h,
                     int lineup_mode, int spacing_x, int spacing_y,
                     bool selectable);

// Add a MenuText row inside the frame at `parent_path`.
int MenuEditAddText(MenuRawCall& root, const std::vector<int>& parent_path,
                    int head_id, const std::string& instance_name,
                    const std::string& text, const std::string& font,
                    int colour_index, const std::string& click_script,
                    const std::string& click_sound);

// ── Serialization / save ─────────────────────────────────────────────────────

// Regenerate the full QSC text: verbatim preamble + Task_New nested form.
std::string MenuSerializeBack(const std::string& preamble, const MenuRawCall& root);

// Compile `qsc_text` and write to `out_qvm_path`. If the target already exists,
// copies it to `<target>.bak` first (explicit-backup rule, #74). Returns false
// with `error` on lex/parse/compile failure — the original file is untouched.
bool MenuSaveToQvm(const std::string& qsc_text, const std::string& out_qvm_path,
                   std::string& error);

} // namespace igi
