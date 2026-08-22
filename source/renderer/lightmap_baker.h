#pragma once
#include "../pch.h"
#include <functional>
#include <string>
#include <vector>

// Lightmap bake engine (#72) — extends the Baked/Hybrid/Dynamic modes shipped in
// object_lightmap.* with an offline "Calculate Lightmaps" pass.
//
// Design notes:
//  * Pure math (packing, rasterization bounds, Lambert falloff, ACES/neutral
//    tonemap curves) lives here as free functions so unit tests can execute them
//    without GL or game assets.
//  * Scene access is injected: callers supply world-space triangles and lights via
//    callbacks, so neither this file nor its test links the renderer stack. The
//    #63 LightFixtureStore plugs in through BakeParams::lights_provider.
//  * Output pages are written as single-page .olm containers under
//    <IGI root>\missions\location0\level<no>\lightmaps_baked\ — the same container
//    layout LoadLevelLightmaps already parses, so vanilla lightmaps.res is never
//    touched. ClearBaked() removes only that directory's contents.

namespace igi {

// ── Tonemap operators (open-igi PostProcessShaders.cs:111-134, ToneMapOperator.cs) ──

// ACES filmic curve, Narkowicz's fitted form (PostProcessShaders.cs ToneMapAces):
//   clamp((x*(a*x+b)) / (x*(c*x+d)+e)), a=2.51 b=0.03 c=2.43 d=0.59 e=0.14
glm::vec3 AcesTonemap(const glm::vec3& x);

// Neutral: Reinhard shoulder with white-point, midtones left where vanilla put
// them (PostProcessShaders.cs ToneMapNeutral): x*(1+x/w^2)/(1+x), w>=1.
glm::vec3 NeutralTonemap(const glm::vec3& x, float white_point);

// ── Lighting primitives ──

struct BakePointLight {
    glm::vec3 position{0.0f};
    glm::vec3 color{1.0f};   // linear RGB, premultiplied by intensity
    float radius = 500.0f;   // world units; falloff reaches zero here

    // Lambert NdotL with squared distance falloff reaching zero at `radius`.
    // shadowed==true zeroes the contribution (occluder between surface and light).
    static glm::vec3 Evaluate(const BakePointLight& light, const glm::vec3& pos,
                              const glm::vec3& normal, bool shadowed);
};

// Sun contribution: NdotL only — directional lights do not fall off.
float SunLambert(const glm::vec3& normal, const glm::vec3& sun_dir);

// ── Atlas packing (shelf packer) ──

struct ShelfRect {
    int index = -1;  // caller's item id, echoed back
    int x = 0, y = 0, w = 0, h = 0;
};

// Packs `widths`/`heights` into shelves of `page_size` width. Returns false when any
// rect cannot fit (oversize items get a (-1,-1) placeholder and are skipped; the rest
// still lay out). Deterministic: first-fit by input order, shelves grow downward. Used
// to lay per-object pages out inside an atlas; the editor's default bake uses whole
// pages per object, so most calls pass one full-size item.
bool ShelfPack(int page_size,
               const std::vector<int>& widths,
               const std::vector<int>& heights,
               std::vector<ShelfRect>& out_rects);

// ── Bake inputs/outputs ──

struct BakeWorldTri {
    glm::vec3 pos[3];
    glm::vec3 normal[3];
    glm::vec2 uv[3];      // lightmap UVs (provider generates planar/box if mesh lacks TEXCOORD1)
};

struct BakeLight {
    enum class Kind { Point, Directional } kind = Kind::Point;
    glm::vec3 position{0.0f};  // Point
    glm::vec3 direction{0.0f}; // Directional (normalized, pointing FROM sun TO scene)
    glm::vec3 color{1.0f};
    float radius = 500.0f;     // Point only
};

// Callbacks injecting scene data (keeps this unit free of renderer dependencies):
//   geometry_provider: fills `out` with world-space triangles for one placed object;
//                      returns false when the model has no bakeable geometry.
//   lights_provider:   returns the light list (placed lights + #63 inferred fixtures).
//   shadow_ray:        optional occlusion query; return true = blocked. nullptr = unshadowed v1.
using GeometryProvider = std::function<bool(
    const std::string& model_id, std::vector<BakeWorldTri>& out)>;
using LightsProvider = std::function<std::vector<BakeLight>()>;
using ShadowRayFunc = std::function<bool(const glm::vec3& pos, const glm::vec3& dir_to_light,
                                         float dist)>;

struct BakeObjectResult {
    std::string olm_name;  // file written, e.g. "001_01_1.olm"
    int tri_count = 0;
    int texel_count = 0;
};

struct BakeParams {
    int level_no = 1;
    int resolution = 512;  // 256 / 512 / 1024
    bool hdr = false;      // true: ACES-tonemap on write; false: straight clamp (vanilla LDR look)
    std::vector<std::pair<std::string, std::vector<BakeWorldTri>>> objects;
    std::vector<BakeLight> lights;
    ShadowRayFunc shadow_ray;  // nullable
};

// Bakes every object in `params` into <IGI root>\...\level<no>\lightmaps_baked\<name>.olm.
// Returns per-object stats; logs progress via Logger. Never touches vanilla lightmaps.res.
std::vector<BakeObjectResult> BakeLightmaps(const BakeParams& params);

// Removes <IGI root>\...\level<no>\lightmaps_baked\ contents ("Clear Baked" menu action).
void ClearBakedLightmaps(int level_no);

// Directory the baker writes / the loader prefers.
std::string BakedLightmapsDir(int level_no);

} // namespace igi

// ─────────────────────────────────────────────────────────────────────────────
// MENU HOOKUP (#72) — for the pause-menu owner (renderer_draw.cpp):
//
//   // After LIGHTMAPS_ROW:
//   const int BAKE_ROW = btn_labels.size();
//   btn_labels.push_back(igi::bake_in_progress_ ? "Baking..." : "Calculate Lightmaps");
//   const int CLEAR_ROW = btn_labels.size();
//   btn_labels.push_back("Clear Baked Lightmaps");
//   const int BAKERES_ROW = btn_labels.size();   // spinner-style, mirrors Fog Intensity row
//   static const char* kResLabels[] = {"256", "512", "1024"};
//   char res_lbl[32]; snprintf(res_lbl, 32, "Bake Res: %s", kResLabels[bake_res_idx]);
//   btn_labels.push_back(res_lbl);
//   const int HDR_ROW = btn_labels.size();
//   btn_labels.push_back(igi::GetLightmapHDR() ? "[X] HDR Lightmap" : "[ ] HDR Lightmap");
//
//   // activation handlers:
//   case BAKE_ROW: {
//     igi::BakeParams p;
//     p.level_no = current_level_; p.resolution = kResValues[bake_res_idx];
//     p.hdr = igi::GetLightmapHDR();
//     for (auto& obj : objects)                       // geometry via Renderer_Objects cache
//       if (!obj.deleted && !obj.modelId.empty()) { /* fill p.objects via GetOrLoadMesh */ }
//     p.lights = /* placed lights + igi::LightFixtureStore inferred fixtures (#63) */;
//     p.shadow_ray = nullptr;                          // v1: unshadowed
//     igi::BakeLightmaps(p);
//     ObjectLightmapManager::Get().LoadLevelLightmaps(current_level_);  // reload, picks up baked
//   } break;
//   case CLEAR_ROW: igi::ClearBakedLightmaps(current_level_);
//                   ObjectLightmapManager::Get().LoadLevelLightmaps(current_level_); break;
//   case BAKERES_ROW: bake_res_idx = (bake_res_idx + 1) % 3; break;
//   case HDR_ROW: igi::SetLightmapHDR(!igi::GetLightmapHDR()); break;
// ─────────────────────────────────────────────────────────────────────────────
