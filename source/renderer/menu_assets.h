#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// Authentic igi.exe menu assets for the Escape menu (#71).
//
// Loads sprite art from the game's own packs (resources.res / mainmenu.res) using
// the editor's existing RES streaming (RES_ForEachEntry) and its verified LOOP
// texture parser (TEX_Parse — LOOP versions 2/7/9/11 cover BOTH IGI-1 and IGI-2
// sprite containers; note open-igi's igi2.pdb-derived SpriteLoader only accepts
// v1/7/9 and would miss IGI-1's Loop02 files).
//
// Every entry point degrades gracefully: when the game root is unset, the packs
// are missing, or an entry is absent, textures stay 0 and the menu keeps its
// hand-drawn fallback skin. Game files are never modified — extraction goes to
// the editor's scratch directory.

namespace igi {

// Pure (GL-free) sprite resolution: extract `name` from the menusystem packs under
// `game_root`, parse via the LOOP parser, decode to RGBA8. Testable headless.
struct MenuSpriteData {
    bool ok = false;
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;
    std::string source; // pack file it came from (diagnostics)
};
MenuSpriteData LoadMenuSpriteData(const std::string& name, const std::string& game_root);

class MenuAssets {
public:
    static MenuAssets& Get();

    // Returns a GL texture id for the named sprite (0 = unavailable).
    // `pack` selects which game archive to look in first.
    uint32_t GetSprite(const std::string& name);

    // True when at least one sprite resolved this session (drives fallback skin choice).
    bool AnyLoaded() const { return any_loaded_; }

    void ClearCache();

private:
    MenuAssets() = default;

    uint32_t LoadFromPack(const std::string& pack_path, const std::string& name);
    uint32_t UploadRgba(const uint8_t* rgba, int width, int height);

    std::unordered_map<std::string, uint32_t> cache_;
    std::string pending_entry_name_; // NAME chunk pending its BODY (menusystem packs)
    bool any_loaded_ = false;
};

} // namespace igi
