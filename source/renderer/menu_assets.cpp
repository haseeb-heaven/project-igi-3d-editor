#include "menu_assets.h"
#include "tex_writer.h"
#include "res_writer.h"
#include "../pch.h"
#include "../utils.h"
#include "../logger.h"

#include <filesystem>
#include <cstring>

namespace igi {

MenuAssets& MenuAssets::Get() {
    static MenuAssets s_instance;
    return s_instance;
}

void MenuAssets::ClearCache() {
    for (const auto& [name, tex] : cache_) {
        if (tex != 0) glDeleteTextures(1, &tex);
    }
    cache_.clear();
    any_loaded_ = false;
}

uint32_t MenuAssets::UploadRgba(const uint8_t* rgba, int width, int height) {
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

MenuSpriteData LoadMenuSpriteDataFromPack(const std::string& pack_path, const std::string& name) {
    if (!std::filesystem::exists(pack_path)) return {};

    // Extract the .spr entry to the editor scratch dir, then parse with the
    // verified LOOP parser (versions 2/7/9/11 — IGI-1 and IGI-2 both covered).
    std::filesystem::path scratch =
        std::filesystem::temp_directory_path() / "igi-editor-menu" / (name + ".spr");
    std::error_code ec;
    std::filesystem::create_directories(scratch.parent_path(), ec);

    bool found = false;
    std::string res_error;
    RES_ForEachEntry(pack_path,
        [&](const std::string& entry_name, const uint8_t* data, size_t size) {
            if (found) return;
            // Pack entries are bare names ("mainmenu.spr") or paths; match on stem.
            std::string bare = entry_name;
            const size_t slash = bare.find_last_of("/\\");
            if (slash != std::string::npos) bare = bare.substr(slash + 1);
            // Menusystem packs prefix their NAME chunks with flag/length words,
            // so RES_ForEachEntry names may carry junk bytes — match by stem
            // substring instead of exact equality.
            if (bare.find(name + ".spr") != std::string::npos ||
                entry_name.find(name + ".spr") != std::string::npos) {
                FILE* f = fopen(scratch.string().c_str(), "wb");
                if (f) {
                    fwrite(data, 1, size, f);
                    fclose(f);
                    found = true;
                }
            }
        }, res_error);
    if (!found) return {};

    MenuSpriteData out;
    out.source = pack_path;
    const TEXFile tex = TEX_Parse(scratch.string());
    if (!tex.valid || tex.images.empty()) {
        Logger::Get().Log(LogLevel::WARNING,
            "[MenuAssets] '" + name + "' extracted but LOOP parse failed: " + tex.error);
        return out;
    }

    // Convert to RGBA8 and upload. Mode 2 = RGB565, 3/67 = ARGB8888
    // (pixel-mode table in tex_writer.h). Full-screen pictures (mainmenu.pic,
    // loading.pic) ship as a TILE GRID: N small images plus a trailing grid block
    // (open-igi ReadTileLayout / TileGrid_Load 0x4B75C0) that maps each cell to a
    // frame. Reassemble before use; non-tiled sprites have a single image.
    std::vector<uint8_t> assembled;
    int out_w = 0, out_h = 0;
    {
        // Re-read the raw container for the grid block (TEX_Parse drops it).
        FILE* rf = fopen(scratch.string().c_str(), "rb");
        std::vector<uint8_t> raw;
        if (rf) {
            fseek(rf, 0, SEEK_END);
            raw.resize(ftell(rf));
            fseek(rf, 0, SEEK_SET);
            if (fread(raw.data(), 1, raw.size(), rf) != raw.size()) raw.clear();
            fclose(rf);
        }
        // LOOP layout: magic@0, VERSION@4 (not @8), then per-version fields.
        const uint32_t version = raw.size() >= 8 ? *reinterpret_cast<const uint32_t*>(raw.data() + 4) : 0;
        const bool tiled_version = (version == 7 || version == 9);
        const uint32_t grid_off = tiled_version && raw.size() >= 32
            ? *reinterpret_cast<const uint32_t*>(raw.data() + 28) : 0;
        Logger::Get().Log(LogLevel::DEBUG,
            "[MenuAssets] '" + name + "' version=" + std::to_string(version) +
            " grid_off=" + std::to_string(grid_off) + " images=" +
            std::to_string(tex.images.size()));
        constexpr uint32_t kMipFlag = 0x40u;

        auto decode_img = [&](const TEXImage& img) {
            std::vector<uint8_t> px(static_cast<size_t>(img.width) * img.height * 4);
            for (size_t p = 0; p < px.size(); p += 4) {
                if (img.mode == 2) {
                    const uint16_t v = static_cast<uint16_t>(
                        img.pixels[p] | (img.pixels[p + 1] << 8));
                    const uint8_t r5 = static_cast<uint8_t>((v >> 11) & 0x1F);
                    const uint8_t g6 = static_cast<uint8_t>((v >> 5) & 0x3F);
                    const uint8_t b5 = static_cast<uint8_t>(v & 0x1F);
                    px[p + 0] = static_cast<uint8_t>((r5 << 3) | (r5 >> 2));
                    px[p + 1] = static_cast<uint8_t>((g6 << 2) | (g6 >> 4));
                    px[p + 2] = static_cast<uint8_t>((b5 << 3) | (b5 >> 2));
                    px[p + 3] = 0xFF;
                } else { // 3 / 67 = ARGB8888 stored BGRA byte order
                    px[p + 0] = img.pixels[p + 2];
                    px[p + 1] = img.pixels[p + 1];
                    px[p + 2] = img.pixels[p + 0];
                    px[p + 3] = img.pixels[p + 3];
                }
            }
            return px;
        };

        bool assembled_grid = false;
        constexpr uint32_t kTileFmtMask = ~kMipFlag;
        if (grid_off != 0 && grid_off + 24 <= raw.size() &&
            std::memcmp(raw.data() + grid_off, "LOOP", 4) == 0) {
            const int columns = static_cast<int>(*reinterpret_cast<const uint32_t*>(raw.data() + grid_off + 16));
            const int rows = static_cast<int>(*reinterpret_cast<uint32_t*>(raw.data() + grid_off + 20));
            if (columns > 0 && rows > 0 && !tex.images.empty()) {
                // Cell size derives from frame 0's declared extent.
                const int cell_w = static_cast<int>(tex.images[0].width);
                const int cell_h = static_cast<int>(tex.images[0].height);
                out_w = columns * cell_w;
                out_h = rows * cell_h;
                assembled.assign(static_cast<size_t>(out_w) * out_h * 4, 0);
                for (int r = 0; r < rows; ++r) {
                    for (int col = 0; col < columns; ++col) {
                        const size_t cell = grid_off + 24 +
                            (static_cast<size_t>(r) * columns + col) * 16;
                        if (cell + 16 > raw.size()) break;
                        const uint8_t frame_no = raw[cell + 12];
                        if (frame_no >= tex.images.size()) continue;
                        const auto px = decode_img(tex.images[frame_no]);
                        for (int y = 0; y < cell_h; ++y) {
                            const size_t dst =
                                ((static_cast<size_t>(r) * cell_h + y) * out_w +
                                 static_cast<size_t>(col) * cell_w) * 4;
                            const size_t src = static_cast<size_t>(y) * cell_w * 4;
                            std::memcpy(assembled.data() + dst, px.data() + src,
                                        static_cast<size_t>(cell_w) * 4);
                        }
                    }
                }
                assembled_grid = true;
            }
        }

        if (!assembled_grid) {
            const TEXImage& img = tex.images.front();
            assembled = decode_img(img);
            out_w = static_cast<int>(img.width);
            out_h = static_cast<int>(img.height);
        }

    }
    out.ok = true;
    out.width = out_w;
    out.height = out_h;
    out.rgba = std::move(assembled);
    return out;
}

MenuSpriteData LoadMenuSpriteData(const std::string& name, const std::string& game_root) {
    // Real game layout (verified against a retail install): the in-game pause menu
    // art lives in <root>/menusystem/ingamemenu.res; shared menu sprites (pointer,
    // buttons) in menusystem.res; mission HUD sprites in missionsprites.res.
    if (game_root.empty()) return {};
    for (const std::string rel : {"menusystem/ingamemenu.res",
                                  "menusystem/menusystem.res",
                                  "menusystem/missionsprites.res"}) {
        MenuSpriteData data = LoadMenuSpriteDataFromPack(game_root + "/" + rel, name);
        if (data.ok) return data;
    }
    return {};
}

uint32_t MenuAssets::GetSprite(const std::string& name) {
    auto it = cache_.find(name);
    if (it != cache_.end()) return it->second;

    const MenuSpriteData data = LoadMenuSpriteData(name, Utils::GetIGIRootPath());
    uint32_t tex = 0;
    if (data.ok) {
        tex = UploadRgba(data.rgba.data(), data.width, data.height);
        Logger::Get().Log(LogLevel::INFO,
            "[MenuAssets] loaded '" + name + "' from " +
            std::filesystem::path(data.source).filename().string() +
            " (" + std::to_string(data.width) + "x" + std::to_string(data.height) + ")");
    }

    cache_[name] = tex;
    if (tex != 0) any_loaded_ = true;
    else Logger::Get().Log(LogLevel::DEBUG,
        "[MenuAssets] '" + name + "' unavailable — menu uses fallback skin");
    return tex;
}

} // namespace igi
