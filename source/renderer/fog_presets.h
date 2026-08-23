#pragma once
#include <glm/glm.hpp>
#include <optional>
#include <string>
#include <vector>

// Per-mission fog presets — C++ port of open-igi src/OpenIGI.Rendering/FogPresets.cs,
// FogPresetEntry.cs and LocalFogVolume.cs (issue #66).
//
// open-igi's model: a level's own FlatSky task already says how thick its fog should be,
// so every mission derives its base atmosphere from its own sky for free. A data file
// (fog-presets.json) holds only the EXCEPTIONS — per-mission field overrides layered
// over per-field defaults — because it expresses things the original engine had no way
// to author (shape/reach/scattering). A broken or missing file never stops the editor:
// levels fall back to their own authored fog, exactly the pre-file behavior.

namespace igi {

// One mission's authored atmosphere, every field optional (open-igi FogPresetEntry).
// Nullable throughout so an entry says only what it changes.
struct FogPresetEntry {
    std::optional<float> density;
    std::optional<float> height_falloff;
    std::optional<glm::vec3> albedo;
    std::optional<float> anisotropy;
    std::optional<float> distance;
    std::optional<float> multiple_scattering;
    std::optional<float> uniform_fraction;
    std::optional<float> sun_intensity;
    std::optional<float> ambient_intensity;

    bool IsEmpty() const;

    // Layers this entry over another, taking its own value wherever it has one
    // (open-igi FogPresetEntry.Over).
    void LayerOver(const FogPresetEntry& fallback);

    // Reads an entry from a JSON object body (open-igi FogPresetEntry.Read):
    // numbers for scalar fields, 3-float arrays for colours.
    static FogPresetEntry Read(const std::string& json_object_body);
};

// A region where the medium is thicker than the level's base fog (open-igi
// LocalFogVolume.cs). The retail data has no such entities — these are a deliberate
// port addition, authored per mission.
struct LocalFogVolumeDef {
    glm::vec3 centre{0.0f};
    glm::vec3 extent{0.0f};      // half-extents for a box, radius in .x for a sphere
    float density_scale = 1.0f;  // multiple of base density; <1 thins (keeps interiors clear)
    bool sphere = false;         // open-igi FogVolumeShape.Box/Sphere

    static constexpr int kMaxPerLevel = 8;   // per-sample cost multiplier, not per-frame
    static constexpr float kEdgeSoftness = 0.7f; // fade fraction over the volume's edge

    bool IsMeaningful() const { return density_scale != 1.0f && extent.x > 0.0f; }
};

// Fully-resolved fog settings for one mission (subset used by the editor's distance
// fog pipeline; the volumetric-only fields are carried for parity/documentation).
struct ResolvedFog {
    float density = 0.014f;
    float height_falloff = 24.0f;
    float base_height = 0.0f;
    glm::vec3 albedo{0.0f};
    float anisotropy = 0.7f;
    float distance = 500.0f;
    int steps = 0;
    float multiple_scattering = 0.35f;
    float uniform_fraction = 0.12f;
    float sun_intensity = 1.0f;
    float ambient_intensity = 1.0f;
    // True when a preset explicitly authored the density (else it equals the
    // FlatSky-derived value passed to Resolve).
    bool density_authored = false;
};

class FogPresets {
public:
    // Presets with nothing authored, so every mission derives from its own sky
    // (open-igi FogPresets.Empty).
    static FogPresets Empty();

    // Reads the preset file. Missing/malformed file -> Empty (never fatal):
    // "A broken preset file never stops the game: the levels fall back to their own
    // authored fog, which is the behaviour before this file existed."
    static FogPresets Load(const std::string& path, bool* ok = nullptr);

    // Builds a mission's atmosphere (open-igi FogPresets.Resolve): the mission's entry
    // layered over the default entry, then per-field fallbacks applied — density falls
    // back to `derived_density` whenever no positive density was authored anywhere.
    ResolvedFog Resolve(int mission, float derived_density, float base_height, int steps) const;

    // Direct access for callers that need to distinguish authored vs derived fields.
    const FogPresetEntry* EntryFor(int mission) const;

    // The volumes a mission declares; empty when none (open-igi FogPresets.VolumesFor).
    const std::vector<LocalFogVolumeDef>& VolumesFor(int mission) const;

    // open-igi's derivation rule (DesktopGame.cs, next to the Resolve call site):
    //   authoredDensity = sky.HasSky
    //       ? 0.004f + clamp(sky.FogAmount, 0, 1) * 0.03f   // FlatSky task's FogAmount
    //       : 0.014f;                                       // no-sky fallback
    static float DeriveFlatSkyDensity(float flat_sky_fog_amount, bool has_flat_sky);

private:
    FogPresets() = default;
    FogPresetEntry default_{};
    std::vector<std::pair<int, FogPresetEntry>> missions_;
    std::vector<std::pair<int, std::vector<LocalFogVolumeDef>>> volumes_;
};

} // namespace igi
