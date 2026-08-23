#include "menu_qvm_render.h"
#include "../pch.h"
#include "menu_assets.h"
#include "menu_qvm.h"
#include "fnt_parser.h"
#include "res_writer.h"
#include "../level/qvm_parser.h"
#include "../level/qvm_decompiler.h"
#include "../utils.h"
#include "../logger.h"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <map>

namespace igi {

namespace {

// Extract one entry from a menusystem pack to the scratch dir (same streaming
// pattern as MenuAssets). Returns the scratch path, empty on miss.
std::string ExtractPackEntry(const std::string& pack_path, const std::string& needle) {
    if (!std::filesystem::exists(pack_path)) return "";
    std::filesystem::path scratch = std::filesystem::temp_directory_path() /
        "igi-editor-menu" / (needle + ".bin");
    std::error_code ec;
    std::filesystem::create_directories(scratch.parent_path(), ec);
    bool found = false;
    std::string res_error;
    RES_ForEachEntry(pack_path,
        [&](const std::string& entry_name, const uint8_t* data, size_t size) {
            if (found || size == 0) return;
            if (entry_name.find(needle) == std::string::npos) return;
            FILE* f = fopen(scratch.string().c_str(), "wb");
            if (f) { fwrite(data, 1, size, f); fclose(f); found = true; }
        }, res_error);
    return found ? scratch.string() : "";
}

struct LoadedFont {
    FntFont font;
    GLuint atlas_tex = 0;
    bool ok = false;
};

LoadedFont LoadMenuFont(const std::string& game_root, const std::string& font_name) {
    LoadedFont out;
    std::string path = ExtractPackEntry(
        game_root + "/menusystem/menusystem.res", font_name);
    if (path.empty())
        path = ExtractPackEntry(game_root + "/menusystem/ingamemenu.res", font_name);
    if (path.empty()) return out;
    out.font = FNT_Parse(path);
    if (!out.font.valid) return out;

    glGenTextures(1, &out.atlas_tex);
    glBindTexture(GL_TEXTURE_2D, out.atlas_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, out.font.texWidth, out.font.texHeight,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, out.font.rgba.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    out.ok = true;
    return out;
}

} // namespace

MenuRender& MenuRender::Get() {
    static MenuRender s_instance;
    return s_instance;
}

bool MenuRender::EnsureLoaded(const std::string& game_root) {
    if (available_) return true;
    if (load_attempted_) return false;
    load_attempted_ = true;
    if (game_root.empty()) return false;

    const std::string qvm_path = game_root + "/menusystem/ingamemenu.qvm";
    if (!std::filesystem::exists(qvm_path)) {
        Logger::Get().Log(LogLevel::INFO,
            "[MenuRender] no ingamemenu.qvm at " + qvm_path + " — fallback menu stays");
        return false;
    }
    QVMFile qvm = QVM_Parse(qvm_path);
    const std::string qsc = QVM_DecompileToString(qvm);
    if (!core_.LoadFromQsc(qsc)) return false;
    available_ = true;
    Logger::Get().Log(LogLevel::INFO,
        "[MenuRender] retail menu loaded: " + std::to_string(core_.def().screens.size()) +
        " screens from ingamemenu.qvm");
    return true;
}

bool MenuRender::OnClick(const int viewport_w, const int viewport_h,
                         const int x, const int y) {
    if (!available_) return false;
    const MenuScreenDef* sc = core_.ActiveScreen();
    if (!sc) return false;

    const float scale = std::min(viewport_w / 640.0f, viewport_h / 480.0f);
    const float off_x = (viewport_w - 640.0f * scale) * 0.5f;
    const float off_y = (viewport_h - 480.0f * scale) * 0.5f;
    const int ux = static_cast<int>((x - off_x) / scale);
    const int uy = static_cast<int>((y - off_y) / scale);
    const auto layout = core_.LayoutActive(8, 16);
    const int hit = core_.HitTestText(layout, ux, uy);
    if (hit < 0) return false;
    core_.ExecuteScript(layout[static_cast<size_t>(hit)].item->click_script);
    return true;
}

bool MenuRender::Draw(const int viewport_w, const int viewport_h,
                      const int mouse_x, const int mouse_y) {
    if (!available_) return false;

    const MenuScreenDef* sc = core_.ActiveScreen();
    if (!sc) return false;

    // ── layout in retail 640x480 UI space, scaled/centred into the viewport ──
    const float scale = std::min(viewport_w / 640.0f, viewport_h / 480.0f);
    const float off_x = (viewport_w - 640.0f * scale) * 0.5f;
    const float off_y = (viewport_h - 480.0f * scale) * 0.5f;
    auto to_screen_x = [&](int ux) { return static_cast<int>(off_x + ux * scale); };
    auto to_screen_y = [&](int uy) { return static_cast<int>(off_y + uy * scale); };

    // ── draw ────────────────────────────────────────────────────────────────
    const auto layout = core_.LayoutActive(8, 16);

    // Screen background: main menu (900) is transparent over the paused game;
    // sub-screens carry authored pictures (mainmenu.pic).
    if (!sc->background.empty()) {
        const uint32_t bg = MenuAssets::Get().GetSprite(
            sc->background.substr(0, sc->background.find('.')));
        if (bg != 0) {
            glEnable(GL_TEXTURE_2D);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glColor4f(1, 1, 1, 1);
            glBindTexture(GL_TEXTURE_2D, bg);
            const int bx = to_screen_x(sc->rect.x0);
            const int by = to_screen_y(sc->rect.y0);
            const int bw = to_screen_x(sc->rect.x1) - bx;
            const int bh = to_screen_y(sc->rect.y1) - by;
            glBegin(GL_QUADS);
            glTexCoord2f(0, 1); glVertex2i(bx, by);
            glTexCoord2f(1, 1); glVertex2i(bx + bw, by);
            glTexCoord2f(1, 0); glVertex2i(bx + bw, by + bh);
            glTexCoord2f(0, 0); glVertex2i(bx, by + bh);
            glEnd();
            glBindTexture(GL_TEXTURE_2D, 0);
            glDisable(GL_TEXTURE_2D);
            glDisable(GL_BLEND);
        }
    }

    // Frames: window borders. The retail win1_*/win2_* sprites are tiny 9-slice
    // pieces; v1 draws a clean bordered rect (retail-green for title frames,
    // grey for content) — sprite-slice rendering documented as a refinement.
    for (const auto& l : layout) {
        if (!l.item->is_frame) continue;
        const int x = to_screen_x(l.item->rect.x0);
        const int y = to_screen_y(l.item->rect.y0);
        const int x2 = to_screen_x(l.item->rect.x1);
        const int y2 = to_screen_y(l.item->rect.y1);
        glDisable(GL_TEXTURE_2D);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glColor4f(0.0f, 0.8f, 0.0f, 0.9f);
        glLineWidth(2.0f);
        glBegin(GL_LINE_LOOP);
        glVertex2i(x, y); glVertex2i(x2, y); glVertex2i(x2, y2); glVertex2i(x, y2);
        glEnd();
        glLineWidth(1.0f);
        glDisable(GL_BLEND);
    }

    // Selection highlight: glow.spr stretched behind the hovered text row.
    const uint32_t glow = MenuAssets::Get().GetSprite("glow");
    int hover = -1;
    {
        const int ux = static_cast<int>((mouse_x - off_x) / scale);
        const int uy = static_cast<int>((mouse_y - off_y) / scale);
        hover = core_.HitTestText(layout, ux, uy);
    }
    if (glow != 0 && hover >= 0) {
        const auto& l = layout[static_cast<size_t>(hover)];
        glEnable(GL_TEXTURE_2D);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glColor4f(1, 1, 1, 0.85f);
        glBindTexture(GL_TEXTURE_2D, glow);
        const int gx = to_screen_x(l.x - 4);
        const int gy = to_screen_y(l.y - 2);
        const int gw = to_screen_x(l.x + l.w + 4) - gx;
        const int gh = to_screen_y(l.y + l.h + 2) - gy;
        glBegin(GL_QUADS);
        glTexCoord2f(0, 1); glVertex2i(gx, gy);
        glTexCoord2f(1, 1); glVertex2i(gx + gw, gy);
        glTexCoord2f(1, 0); glVertex2i(gx + gw, gy + gh);
        glTexCoord2f(0, 0); glVertex2i(gx, gy + gh);
        glEnd();
        glBindTexture(GL_TEXTURE_2D, 0);
        glDisable(GL_TEXTURE_2D);
        glDisable(GL_BLEND);
    }

    // Text: glyph quads from the game FNT atlases (loaded per font on demand).
    // Font cache: name -> LoadedFont (static to survive across frames).
    static std::map<std::string, LoadedFont> font_cache;
    std::map<std::string, GLuint> atlas_by_font;

    for (const auto& l : layout) {
        if (l.item->is_frame || l.item->text.empty()) continue;
        const std::string font_name = l.item->font.empty() ? "font3.fnt" : l.item->font;
        auto it = font_cache.find(font_name);
        if (it == font_cache.end()) {
            LoadedFont lf = LoadMenuFont(Utils::GetIGIRootPath(), font_name);
            it = font_cache.emplace(font_name, std::move(lf)).first;
        }
        const LoadedFont& lf = it->second;
        const MenuColourRgba col = MenuColourRgbaFn(l.item->colour_index);

        if (!lf.ok) {
            // Fallback: bitmap-free text via the editor's system-font helper is
            // unavailable inside this immediate pass; draw a placeholder bar so
            // layout stays visible when fonts are missing.
            glColor4f(col.r, col.g, col.b, 0.6f);
            const int bx = to_screen_x(l.x);
            const int by = to_screen_y(l.y);
            const int bw = to_screen_x(l.x + l.w) - bx;
            const int bh = to_screen_y(l.y + l.h) - by;
            glBegin(GL_QUADS);
            glVertex2i(bx, by); glVertex2i(bx + bw, by);
            glVertex2i(bx + bw, by + bh); glVertex2i(bx, by + bh);
            glEnd();
            continue;
        }

        glEnable(GL_TEXTURE_2D);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glBindTexture(GL_TEXTURE_2D, lf.atlas_tex);
        glColor4f(col.r, col.g, col.b, col.a);

        float pen_x = static_cast<float>(to_screen_x(l.x));
        const float pen_y0 = static_cast<float>(to_screen_y(l.y));
        const float scale_y = (scale * 16.0f) / static_cast<float>(lf.font.lineHeight > 0 ? lf.font.lineHeight : 16);
        for (const char ch : l.item->text) {
            auto g = lf.font.glyphs.find(static_cast<int>(static_cast<unsigned char>(ch)));
            if (g == lf.font.glyphs.end()) g = lf.font.glyphs.find('?');
            if (g == lf.font.glyphs.end()) continue;
            const FntGlyph& gl = g->second;
            const float x0 = pen_x;
            const float x1 = pen_x + gl.width * scale;
            const float y0 = pen_y0;
            const float y1 = pen_y0 + gl.height * scale_y;
            glBegin(GL_QUADS);
            glTexCoord2f(gl.u0, gl.v0); glVertex2f(x0, y0);
            glTexCoord2f(gl.u1, gl.v0); glVertex2f(x1, y0);
            glTexCoord2f(gl.u1, gl.v1); glVertex2f(x1, y1);
            glTexCoord2f(gl.u0, gl.v1); glVertex2f(x0, y1);
            glEnd();
            pen_x += gl.advance * scale;
        }
        glBindTexture(GL_TEXTURE_2D, 0);
        glDisable(GL_TEXTURE_2D);
        glDisable(GL_BLEND);
    }

    return true;
}

} // namespace igi
