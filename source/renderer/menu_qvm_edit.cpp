#include "menu_qvm_edit.h"

#include "../level/qsc_lexer.h"
#include "../level/qsc_parser.h"
#include "../level/qvm_compiler.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

namespace igi {

namespace {

bool IsWs(char ch) { return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n'; }

void SkipWs(const std::string& s, size_t& i) {
    while (i < s.size() && IsWs(s[i])) ++i;
}

// Read one verbatim token (quoted string kept intact incl. escapes; bare token
// up to the next delimiter). Returns empty at ')' / end.
std::string ReadRawToken(const std::string& s, size_t& i) {
    SkipWs(s, i);
    if (i >= s.size()) return "";
    std::string tok;
    if (s[i] == '"') {
        tok += s[i++];
        while (i < s.size()) {
            tok += s[i];
            if (s[i] == '\\' && i + 1 < s.size()) tok += s[++i];
            else if (s[i] == '"') { ++i; break; }
            ++i;
        }
        return tok;
    }
    while (i < s.size() && !IsWs(s[i]) && s[i] != ',' && s[i] != ')') tok += s[i++];
    return tok;
}

// Parse one Task_New(...) call starting at `i` (which must sit on "Task_New(").
// Advances `i` past the closing paren. Children are nested Task_New calls found
// among the trailing arguments.
bool ParseRawCall(const std::string& s, size_t& i, MenuRawCall& out) {
    SkipWs(s, i);
    if (s.compare(i, 9, "Task_New(") != 0) return false;
    i += 9;

    // head id
    std::string head = ReadRawToken(s, i);
    out.head_id = head;
    // type
    SkipWs(s, i);
    if (i < s.size() && s[i] == ',') ++i;
    std::string type = ReadRawToken(s, i);
    if (type.size() >= 2 && type.front() == '"' && type.back() == '"')
        type = type.substr(1, type.size() - 2);
    out.type = type;

    // remaining args until the matching close — scalars collected verbatim,
    // nested Task_New calls recursed into children.
    int depth = 1;
    std::vector<std::string> tokens;   // scalar args in order
    bool have_pending_child = false;
    while (true) {
        SkipWs(s, i);
        if (i >= s.size()) break;
        const char ch = s[i];
        if (ch == ')') {
            --depth;
            ++i;
            break;
        }
        if (ch == ',') {
            if (depth == 1) {
                // argument boundary inside this call: a following Task_New means
                // children start here.
                size_t probe = i + 1;
                SkipWs(s, probe);
                if (s.compare(probe, 9, "Task_New(") == 0) {
                    have_pending_child = true;
                    ++i;
                    continue;
                }
            }
            ++i;
            continue;
        }
        if (s.compare(i, 9, "Task_New(") == 0) {
            MenuRawCall child;
            if (!ParseRawCall(s, i, child)) return false;
            out.children.push_back(std::move(child));
            continue;
        }
        tokens.push_back(ReadRawToken(s, i));
    }
    (void)have_pending_child;
    out.raw_args = std::move(tokens);
    return true;
}

std::string Tabs(int depth) { return std::string(static_cast<size_t>(std::max(depth, 0)), '\t'); }

std::string EmitCall(const MenuRawCall& call, int depth) {
    if (call.removed) return "";
    std::string out = "Task_New(" + call.head_id + ", \"" + call.type + "\"";
    for (const auto& a : call.raw_args) out += ", " + a;
    for (const auto& ch : call.children) {
        const std::string emitted = EmitCall(ch, depth + 1);
        if (emitted.empty()) continue;
        out += ", \n" + Tabs(depth + 1) + emitted;
    }
    out += ")";
    return out;
}

MenuRawCall* Navigate(MenuRawCall& root, const std::vector<int>& path) {
    MenuRawCall* cur = &root;
    for (int idx : path) {
        if (idx < 0 || idx >= static_cast<int>(cur->children.size())) return nullptr;
        cur = &cur->children[static_cast<size_t>(idx)];
    }
    return cur;
}

} // namespace

bool ParseMenuRaw(const std::string& decompiled_qsc, MenuRawCall& out_root,
                  std::string& preamble, std::string& error) {
    const size_t first = decompiled_qsc.find("Task_New(");
    if (first == std::string::npos) {
        error = "no Task_New found";
        return false;
    }
    preamble = decompiled_qsc.substr(0, first);

    size_t i = first;
    if (!ParseRawCall(decompiled_qsc, i, out_root)) {
        error = "malformed Task_New tree";
        return false;
    }

    // Trailing text after the root must be whitespace/semicolon only.
    std::string tail = decompiled_qsc.substr(i);
    tail.erase(std::remove_if(tail.begin(), tail.end(), IsWs), tail.end());
    for (const char ch : tail) {
        if (ch != ';') {
            error = "unexpected trailing content after root Task_New";
            return false;
        }
    }
    return true;
}

bool MenuEditSetText(MenuRawCall& root, const std::vector<int>& path,
                     const std::string& new_text) {
    MenuRawCall* node = Navigate(root, path);
    if (!node) return false;
    // Text Resource slot: raw_args[1] for MenuText/MenuTextConditional (raw_args[0]
    // is the instance name). Fall back to raw_args[0] when only one arg exists.
    if (node->type != "MenuText" && node->type != "MenuTextConditional") return false;
    if (node->raw_args.size() < 2) return false;
    node->raw_args[1] = "\"" + new_text + "\"";
    return true;
}

bool MenuEditSetArg(MenuRawCall& root, const std::vector<int>& path, int arg_index,
                    const std::string& new_raw_token) {
    MenuRawCall* node = Navigate(root, path);
    if (!node) return false;
    if (arg_index < 0 || arg_index >= static_cast<int>(node->raw_args.size())) return false;
    node->raw_args[static_cast<size_t>(arg_index)] = new_raw_token;
    return true;
}

bool MenuEditRemove(MenuRawCall& root, const std::vector<int>& path) {
    if (path.empty()) return false;
    MenuRawCall* parent = Navigate(root, std::vector<int>(path.begin(), path.end() - 1));
    if (!parent) return false;
    const int last = path.back();
    if (last < 0 || last >= static_cast<int>(parent->children.size())) return false;
    parent->children.erase(parent->children.begin() + last);
    return true;
}

int MenuEditAddFrame(MenuRawCall& root, const std::vector<int>& parent_path,
                     int head_id, const std::string& sprite,
                     int x, int y, int w, int h,
                     int lineup_mode, int spacing_x, int spacing_y,
                     bool selectable) {
    MenuRawCall* parent = Navigate(root, parent_path);
    if (!parent) return -1;
    MenuRawCall frame;
    frame.head_id = std::to_string(head_id);
    frame.type = "MenuFrame";
    frame.raw_args.push_back("\"\"");
    frame.raw_args.push_back("\"" + sprite + "\"");
    frame.raw_args.push_back(std::to_string(x));
    frame.raw_args.push_back(std::to_string(y));
    frame.raw_args.push_back(std::to_string(w));
    frame.raw_args.push_back(std::to_string(h));
    frame.raw_args.push_back(std::to_string(lineup_mode));
    frame.raw_args.push_back(std::to_string(spacing_x));
    frame.raw_args.push_back(std::to_string(spacing_y));
    frame.raw_args.push_back(selectable ? "TRUE" : "FALSE");
    parent->children.push_back(std::move(frame));
    return static_cast<int>(parent->children.size()) - 1;
}

int MenuEditAddText(MenuRawCall& root, const std::vector<int>& parent_path,
                    int head_id, const std::string& instance_name,
                    const std::string& text, const std::string& font,
                    int colour_index, const std::string& click_script,
                    const std::string& click_sound) {
    MenuRawCall* parent = Navigate(root, parent_path);
    if (!parent) return -1;
    MenuRawCall item;
    item.head_id = std::to_string(head_id);
    item.type = "MenuText";
    item.raw_args.push_back("\"" + instance_name + "\"");
    item.raw_args.push_back("\"" + text + "\"");
    item.raw_args.push_back("\"" + font + "\"");
    item.raw_args.push_back(std::to_string(colour_index));
    item.raw_args.push_back("\"" + click_script + "\"");
    item.raw_args.push_back("\"" + click_sound + "\"");
    parent->children.push_back(std::move(item));
    return static_cast<int>(parent->children.size()) - 1;
}

std::string MenuSerializeBack(const std::string& preamble, const MenuRawCall& root) {
    return preamble + EmitCall(root, 0) + ";";
}

bool MenuSaveToQvm(const std::string& qsc_text, const std::string& out_qvm_path,
                   std::string& error) {
    const qsc::LexResult lex = qsc::Lex(qsc_text);
    if (!lex.ok) {
        error = "lex failed: " + lex.error;
        return false;
    }
    const qsc::ParseResult parse = qsc::Parse(lex.tokens);
    if (!parse.ok || !parse.program) {
        error = "parse failed: " + parse.error;
        return false;
    }

    // Explicit-backup rule (#74): preserve any existing target as .bak first.
    if (std::filesystem::exists(out_qvm_path)) {
        std::ifstream src(out_qvm_path, std::ios::binary);
        std::ofstream bak(out_qvm_path + ".bak", std::ios::binary | std::ios::trunc);
        if (!src.is_open() || !bak.is_open()) {
            error = "could not create backup " + out_qvm_path + ".bak";
            return false;
        }
        bak << src.rdbuf();
    }

    if (!qvm::CompileToFile(*parse.program, out_qvm_path, &error)) {
        if (error.empty()) error = "compile failed";
        return false;
    }
    return true;
}

} // namespace igi
