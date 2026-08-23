#include "menu_qvm.h"
#include <cctype>
#include <cstdlib>
#include <functional>
#include <map>
#include <memory>

// Recursive-descent parser over the decompiled menu QSC. The decompiler emits
// nested Task_New(parentId, "Type", "instanceName", <declared params...>) calls;
// we tokenise with paren/string awareness and map each element type's declared
// parameter order (transcribed from the retail ingamemenu.qvm — see header).

namespace igi {

namespace {

struct Token {
    enum Kind { String, Number, Ident } kind;
    std::string text;
};

// Split a Task_New argument list at top level (paren/quote aware).
size_t SkipTaskNewArgs(const std::string& s, size_t open_paren, std::vector<std::string>& args) {
    size_t i = open_paren + 1;
    int depth = 1;
    std::string cur;
    bool in_string = false;
    auto flush = [&]() {
        // trim whitespace
        size_t b = cur.find_first_not_of(" \t\r\n");
        if (b == std::string::npos) { cur.clear(); args.push_back(""); return; }
        args.push_back(cur);
        cur.clear();
    };
    while (i < s.size() && depth > 0) {
        const char ch = s[i];
        if (in_string) {
            cur += ch;
            if (ch == '\\' && i + 1 < s.size()) { cur += s[++i]; }
            else if (ch == '"') in_string = false;
        } else if (ch == '"') {
            in_string = true;
            cur += ch;
        } else if (ch == '(') {
            ++depth;
            cur += ch;
        } else if (ch == ')') {
            --depth;
            if (depth > 0) cur += ch;
        } else if (ch == ',' && depth == 1) {
            flush();
        } else {
            cur += ch;
        }
        ++i;
    }
    flush();
    return i; // one past the closing paren
}

std::string Unquote(const std::string& t) {
    if (t.size() >= 2 && t.front() == '"' && t.back() == '"') {
        std::string out;
        for (size_t i = 1; i + 1 < t.size(); ++i) {
            if (t[i] == '\\' && i + 1 < t.size() - 1) {
                ++i;
                // Retail scripts embed literal \n line separators between
                // statements; keep them as real newlines so save-back editing
                // recompiles intact statements instead of glued ones with a
                // stray 'n' suffix (review finding 3836032333).
                if (t[i] == 'n') out += '\n';
                else if (t[i] == 't') out += '\t';
                else out += t[i];
                continue;
            }
            out += t[i];
        }
        return out;
    }
    return t;
}

int ToInt(const std::string& t, int fallback = 0) {
    try { return std::stoi(t); } catch (...) { return fallback; }
}

bool IsNumber(const std::string& t) {
    return !t.empty() && (std::isdigit(static_cast<unsigned char>(t[0])) || t[0] == '-');
}

// Strip a trailing script newline artifact ("\n" inside quoted scripts).
std::string CleanScript(const std::string& t) { return Unquote(t); }

const char* FindNextTaskNew(const std::string& s, size_t from) {
    return s.c_str() + s.find("Task_New(", from);
}

} // namespace

const MenuScreenDef* MenuDef::FindScreen(int id) const {
    for (const auto& sc : screens) {
        if (sc.id == id) return &sc;
    }
    return nullptr;
}

namespace {

struct ParsedCall {
    int head_id = -1;
    std::string type;
    std::string instance_name;
    std::vector<std::string> params;   // declared parameters, in order
    std::vector<ParsedCall> children;
};

// Recursive parser: Task_New(head, "Type", "instance", p0, p1, ..., children...)
// Children appear as trailing Task_New(...) arguments.
class CallParser {
public:
    // Returns false when no call starts at/from `i`.
    bool Parse(const std::string& s, size_t& i, ParsedCall& out) {
        SkipWs(s, i);
        if (s.compare(i, 9, "Task_New(") != 0) return false;
        i += 9;
        // head id
        std::string tok = ReadToken(s, i);
        out.head_id = ToInt(tok, -1);
        ExpectComma(s, i);
        out.type = Unquote(ReadToken(s, i));
        // NOTE: screens/dialogs take their title directly as params[0]; widget
        // types carry an instance-name token first (handled per-type below).
        // Remaining args until ')' — scalars or nested calls
        while (true) {
            SkipWs(s, i);
            if (i >= s.size()) return true;
            const char ch = s[i];
            if (ch == ')') { ++i; return true; }
            if (ch == ',') { ++i; continue; }
            if (s.compare(i, 9, "Task_New(") == 0) {
                ParsedCall child;
                Parse(s, i, child);
                out.children.push_back(std::move(child));
                continue;
            }
            out.params.push_back(ReadToken(s, i));
        }
    }

private:
    static void SkipWs(const std::string& s, size_t& i) {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    }
    static void ExpectComma(const std::string& s, size_t& i) {
        SkipWs(s, i);
        if (i < s.size() && s[i] == ',') ++i;
    }
    // Reads one token: quoted string (kept quoted for script fidelity), number,
    // TRUE/FALSE, identifier, or an arbitrary non-comma/non-paren expression.
    static std::string ReadToken(const std::string& s, size_t& i) {
        SkipWs(s, i);
        std::string tok;
        if (i < s.size() && s[i] == '"') {
            tok += s[i++];
            while (i < s.size()) {
                tok += s[i];
                if (s[i] == '\\' && i + 1 < s.size()) { tok += s[++i]; }
                else if (s[i] == '"') { ++i; break; }
                ++i;
            }
            return tok;
        }
        if (i < s.size() && s[i] == '(') {
            // Unquoted parenthesised expression: consume the balanced group so the
            // caller never stalls on an empty token (review finding 3836051561).
            int depth = 0;
            do {
                const char ch = s[i];
                tok += ch;
                ++i;
                if (ch == '(') ++depth;
                else if (ch == ')') --depth;
            } while (i < s.size() && depth > 0);
            return tok;
        }
        while (i < s.size() && s[i] != ',' && s[i] != ')') {
            tok += s[i++];
        }
        while (!tok.empty() && std::isspace(static_cast<unsigned char>(tok.back()))) tok.pop_back();
        if (tok.empty() && i < s.size()) ++i; // always make progress (hang guard)
        return tok;
    }
};

void CollectScreens(const ParsedCall& call, MenuDef& def);

// Appends `call` as an item (plus its subtree, flattened in draw order) to `out`.
void EmitItem(const ParsedCall& call, std::vector<MenuItemDef>& out) {
    MenuItemDef item;
    item.type = call.type;
    const auto& p = call.params;
    // Widget types carry an instance-name token at p[0]; declared params follow.
    if (call.type == "MenuFrame" && p.size() >= 10) {
        item.is_frame = true;
        item.sprite = Unquote(p[1]);
        item.rect.x0 = ToInt(p[2]);
        item.rect.y0 = ToInt(p[3]);
        item.rect.x1 = item.rect.x0 + ToInt(p[4]);
        item.rect.y1 = item.rect.y0 + ToInt(p[5]);
        item.lineup_mode = ToInt(p[6]);
        item.spacing_x = ToInt(p[7]);
        item.spacing_y = ToInt(p[8]);
        item.selectable = (p[9].find("TRUE") != std::string::npos);
    } else if (call.type == "MenuText" && p.size() >= 6) {
        item.text = Unquote(p[1]);
        item.font = Unquote(p[2]);
        item.colour_index = ToInt(p[3]);
        item.click_script = CleanScript(p[4]);
        item.click_sound = Unquote(p[5]);
    } else if (call.type == "MenuTextConditional" && p.size() >= 7) {
        item.text = Unquote(p[1]);
        item.font = Unquote(p[2]);
        item.colour_index = ToInt(p[3]);
        item.enabled_expr = CleanScript(p[4]);
        item.visible_expr = CleanScript(p[5]);
        item.click_script = CleanScript(p[6]);
        if (p.size() >= 8) item.click_sound = Unquote(p[7]);
    } else if (call.type == "SlideBar" && p.size() >= 2) {
        item.sprite = Unquote(p[1]);                       // "slide"/"slidevert"
        if (p.size() > 4) item.click_script = CleanScript(p[4]); // Get Data Script
    } else if (call.type == "ToggleBox" && p.size() >= 2) {
        item.click_script = CleanScript(p[1]); // Get Data Script
    }
    out.push_back(std::move(item));
    for (const auto& ch : call.children) EmitItem(ch, out);
}

// The retail tree nests items inside frames inside screens; frames may also nest
// frames. Emission walks the subtree appending to the screen's flat item list —
// draw order follows list order (background frames first, matching retail).

void CollectScreens(const ParsedCall& call, MenuDef& def) {
    if (call.type == "MenuManager") {
        // Declared: Current menuscreen(Int32)=id, Resource path(String256), isOwnDisplay.
        // params[0] is the human description; id and path follow positionally.
        for (size_t pi = 0; pi < call.params.size(); ++pi) {
            const std::string raw = call.params[pi];
            const std::string v = Unquote(raw);
            if (!v.empty() && v.find(".res") != std::string::npos && def.resource_path.empty())
                def.resource_path = v;
            if (IsNumber(raw)) def.manager_id = ToInt(raw, def.manager_id);
        }
    } else if (call.type == "MenuScreen" && call.params.size() >= 7) {
        // params[0]=title, [1]=background, [2..5]=frame rect, [6]=escape, ...
        MenuScreenDef sc;
        sc.id = call.head_id;
        sc.title = Unquote(call.params[0]);
        sc.background = Unquote(call.params[1]);
        sc.rect.x0 = ToInt(call.params[2]);
        sc.rect.y0 = ToInt(call.params[3]);
        sc.rect.x1 = ToInt(call.params[4]);
        sc.rect.y1 = ToInt(call.params[5]);
        sc.on_press_escape = CleanScript(call.params[6]);
        if (call.params.size() >= 8) sc.on_init_data = CleanScript(call.params[7]);
        if (call.params.size() >= 9) sc.on_change_data = CleanScript(call.params[8]);
        std::vector<MenuItemDef> items;
        for (const auto& ch : call.children) EmitItem(ch, items);
        sc.items = std::move(items);
        def.screens.push_back(std::move(sc));
        return;
    } else if (call.type == "DialogWindow" && call.params.size() >= 5) {
        // params[0]=title, [1]=x, [2]=y, [3]=w, [4]=h
        MenuScreenDef sc;
        sc.id = call.head_id;
        sc.is_dialog = true;
        sc.title = Unquote(call.params[0]);
        sc.rect.x0 = ToInt(call.params[1]);
        sc.rect.y0 = ToInt(call.params[2]);
        sc.rect.x1 = sc.rect.x0 + ToInt(call.params[3]); // w/h for dialogs
        sc.rect.y1 = sc.rect.y0 + ToInt(call.params[4]);
        std::vector<MenuItemDef> items;
        for (const auto& ch : call.children) EmitItem(ch, items);
        sc.items = std::move(items);
        def.screens.push_back(std::move(sc));
        return;
    }
    for (const auto& ch : call.children) CollectScreens(ch, def);
}

} // namespace

MenuDef ParseMenuQsc(const std::string& qsc) {
    MenuDef def;
    CallParser parser;
    size_t i = qsc.find("Task_New("); // skip Task_DeclareParameters preamble
    if (i == std::string::npos) {
        def.error = "no Task_New found";
        return def;
    }
    ParsedCall root;
    if (!parser.Parse(qsc, i, root)) {
        def.error = "no Task_New found";
        return def;
    }
    CollectScreens(root, def);
    def.valid = !def.screens.empty();
    if (!def.valid) def.error = "no MenuScreen/DialogWindow found";
    return def;
}


} // namespace igi
