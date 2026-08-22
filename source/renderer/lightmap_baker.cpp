#include "lightmap_baker.h"
#include "../logger.h"
#include "../utils.h"
#include "object_lightmap.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace igi {

// ── Tonemap operators — verbatim math from open-igi PostProcessShaders.cs ──

glm::vec3 AcesTonemap(const glm::vec3& x) {
    // ToneMapAces (Narkowicz fit), PostProcessShaders.cs:111-119.
    const float a = 2.51f, b = 0.03f, c = 2.43f, d = 0.59f, e = 0.14f;
    glm::vec3 r = (x * (a * x + b)) / (x * (c * x + d) + e);
    return glm::clamp(r, 0.0f, 1.0f);
}

glm::vec3 NeutralTonemap(const glm::vec3& x, float white_point) {
    // ToneMapNeutral (PostProcessShaders.cs:130-134): Reinhard shoulder with white point.
    const float w = std::max(white_point, 1.0f);
    glm::vec3 r = x * (1.0f + x / (w * w)) / (1.0f + x);
    return glm::clamp(r, 0.0f, 1.0f);
}

// ── Lighting primitives ──

float SunLambert(const glm::vec3& normal, const glm::vec3& sun_dir) {
    return std::max(glm::dot(glm::normalize(normal), glm::normalize(sun_dir)), 0.0f);
}

glm::vec3 BakePointLight::Evaluate(const BakePointLight& light, const glm::vec3& pos,
                                   const glm::vec3& normal, bool shadowed) {
    if (shadowed) return glm::vec3(0.0f);
    const glm::vec3 to_light = light.position - pos;
    const float dist = glm::length(to_light);
    if (dist >= light.radius || dist <= 1e-6f) return glm::vec3(0.0f);

    const float ndotl = std::max(glm::dot(normal, to_light / dist), 0.0f);
    if (ndotl <= 0.0f) return glm::vec3(0.0f);

    // Squared falloff reaching zero at radius: smooth window (1-(d/r)^2)^2.
    const float t = dist / light.radius;
    const float window = 1.0f - t * t;
    const float falloff = window * window;

    return light.color * (ndotl * falloff);
}

// ── Atlas packing ──

bool ShelfPack(int page_size, const std::vector<int>& widths,
               const std::vector<int>& heights, std::vector<ShelfRect>& out_rects) {
    out_rects.clear();
    out_rects.reserve(widths.size());

    bool all_placed = true;
    int shelf_x = 0, shelf_y = 0, shelf_h = 0;
    for (size_t i = 0; i < widths.size(); ++i) {
        const int w = widths[i], h = heights[i];
        if (w <= 0 || h <= 0 || w > page_size || h > page_size) {
            // Cannot ever fit — report per contract ("returns false when any rect
            // cannot fit") while still laying out the rest.
            out_rects.push_back({static_cast<int>(i), -1, -1, w, h});
            all_placed = false;
            continue;
        }
        if (shelf_x + w > page_size) { // next shelf down
            shelf_y += shelf_h;
            shelf_x = 0;
            shelf_h = 0;
        }
        out_rects.push_back({static_cast<int>(i), shelf_x, shelf_y, w, h});
        shelf_x += w;
        shelf_h = std::max(shelf_h, h);
        if (shelf_y + shelf_h > page_size) return false; // exceeded the atlas
    }
    return all_placed;
}

// ── Rasterization ──

namespace {

struct RasterVert {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 uv;
};

// Edge-function barycentric fill over the UV-space bbox of one triangle. Each covered
// texel interpolates world position/normal and accumulates lighting into `page`.
void RasterizeTri(const BakeWorldTri& tri, int page_size,
                  std::vector<glm::vec3>& hdr_page, const std::vector<BakeLight>& lights,
                  const ShadowRayFunc& shadow_ray) {
    // Degenerate UV triangles cover no area.
    const glm::vec2 e0 = tri.uv[1] - tri.uv[0];
    const glm::vec2 e1 = tri.uv[2] - tri.uv[0];
    const float area2 = e0.x * e1.y - e0.y * e1.x;
    if (std::fabs(area2) < 1e-12f) return;
    const float inv_area = 1.0f / area2;

    // UV space here is [0,1]^2 scaled by page_size.
    const auto to_px = [&](const glm::vec2& uv) {
        return glm::vec2(uv.x * static_cast<float>(page_size), uv.y * static_cast<float>(page_size));
    };
    const glm::vec2 p0 = to_px(tri.uv[0]), p1 = to_px(tri.uv[1]), p2 = to_px(tri.uv[2]);

    const float min_x = std::max(0.0f, std::min({p0.x, p1.x, p2.x}));
    const float max_x = std::min(static_cast<float>(page_size - 1), std::max({p0.x, p1.x, p2.x}));
    const float min_y = std::max(0.0f, std::min({p0.y, p1.y, p2.y}));
    const float max_y = std::min(static_cast<float>(page_size - 1), std::max({p0.y, p1.y, p2.y}));

    const auto edge = [](const glm::vec2& a, const glm::vec2& b, const glm::vec2& p) {
        return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
    };

    RasterVert v[3] = {{tri.pos[0], tri.normal[0], tri.uv[0]},
                       {tri.pos[1], tri.normal[1], tri.uv[1]},
                       {tri.pos[2], tri.normal[2], tri.uv[2]}};

    for (int py = static_cast<int>(std::floor(min_y)); py <= static_cast<int>(std::ceil(max_y)); ++py) {
        for (int px = static_cast<int>(std::floor(min_x)); px <= static_cast<int>(std::ceil(max_x)); ++px) {
            const glm::vec2 p(static_cast<float>(px) + 0.5f, static_cast<float>(py) + 0.5f);

            float w0 = edge(p1, p2, p);
            float w1 = edge(p2, p0, p);
            float w2 = edge(p0, p1, p);
            if (area2 < 0.0f) { // consistent winding
                w0 = -w0; w1 = -w1; w2 = -w2;
            }
            if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) continue;

            w0 *= inv_area; w1 *= inv_area; w2 *= inv_area;

            const glm::vec3 world_pos =
                v[0].pos * w0 + v[1].pos * w1 + v[2].pos * w2;
            glm::vec3 normal =
                v[0].normal * w0 + v[1].normal * w1 + v[2].normal * w2;
            if (glm::length(normal) > 1e-9f) normal = glm::normalize(normal);

            // Accumulate direct lighting.
            glm::vec3 radiance(0.0f);
            for (const auto& light : lights) {
                if (light.kind == BakeLight::Kind::Directional) {
                    radiance += light.color * SunLambert(normal, -light.direction);
                } else {
                    BakePointLight pl;
                    pl.position = light.position;
                    pl.color = light.color;
                    pl.radius = light.radius;
                    bool blocked = false;
                    if (shadow_ray) {
                        const glm::vec3 to_light = light.position - world_pos;
                        const float d = glm::length(to_light);
                        if (d > 1e-6f)
                            blocked = shadow_ray(world_pos, to_light / d, d);
                    }
                    radiance += BakePointLight::Evaluate(pl, world_pos, normal, blocked);
                }
            }

            hdr_page[static_cast<size_t>(py) * page_size + px] += radiance;
        }
    }
}

// Serializes one HDR accumulation buffer as a single-page .olm container (the exact
// layout LoadLevelLightmaps parses: 60-byte prefix, u32 count at +44, per-page
// 44-byte descriptors with u16 w/h at descriptor+40, BGRA pixels after).
bool WriteOlmPage(const std::string& path, int width, int height,
                  const std::vector<glm::vec3>& hdr_page, bool hdr_tonemap) {
    constexpr size_t kPrefixSize = 60;
    constexpr size_t kDescriptorSize = 44;

    std::vector<uint8_t> buf(kPrefixSize + kDescriptorSize +
                             static_cast<size_t>(width) * height * 4, 0);
    uint32_t count = 1;
    std::memcpy(buf.data() + 44, &count, sizeof(count));
    uint16_t w = static_cast<uint16_t>(width), h = static_cast<uint16_t>(height);
    std::memcpy(buf.data() + kPrefixSize + 40, &w, sizeof(w));
    std::memcpy(buf.data() + kPrefixSize + 42, &h, sizeof(h));

    uint8_t* px = buf.data() + kPrefixSize + kDescriptorSize;
    for (int i = 0; i < width * height; ++i) {
        glm::vec3 c = hdr_page[i];
        if (hdr_tonemap) c = AcesTonemap(c);          // HDR on: roll off, don't clip
        else c = glm::clamp(c, 0.0f, 1.0f);           // vanilla LDR look
        const uint8_t r = static_cast<uint8_t>(c.r * 255.0f + 0.5f);
        const uint8_t g = static_cast<uint8_t>(c.g * 255.0f + 0.5f);
        const uint8_t b = static_cast<uint8_t>(c.b * 255.0f + 0.5f);
        px[i * 4 + 0] = b;                            // loader expects BGRA
        px[i * 4 + 1] = g;
        px[i * 4 + 2] = r;
        px[i * 4 + 3] = 255;
    }

    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(buf.data()),
            static_cast<std::streamsize>(buf.size()));
    return static_cast<bool>(f);
}

} // namespace

// ── Public API ──

std::string BakedLightmapsDir(int level_no) {
    return Utils::GetIGIRootPath() + "\\missions\\location0\\level" +
           std::to_string(level_no) + "\\lightmaps_baked";
}

void ClearBakedLightmaps(int level_no) {
    namespace fs = std::filesystem;
    const std::string dir = BakedLightmapsDir(level_no);
    std::error_code ec;
    if (!fs::exists(dir)) return;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        fs::remove(entry.path(), ec);
    }
    Logger::Get().Log(LogLevel::INFO,
        "[Bake] Cleared baked lightmaps in " + dir);
}

std::vector<BakeObjectResult> BakeLightmaps(const BakeParams& params) {
    std::vector<BakeObjectResult> results;
    const int res = std::clamp(params.resolution, 256, 1024);

    namespace fs = std::filesystem;
    const std::string dir = BakedLightmapsDir(params.level_no);
    std::error_code ec;
    fs::create_directories(dir, ec);

    for (const auto& [name, tris] : params.objects) {
        BakeObjectResult result;
        result.olm_name = name + ".olm";

        // One full page per object (v1 layout). The shelf packer documents where a
        // shared atlas would place sub-rects once TEXCOORD1 coverage is audited.
        std::vector<glm::vec3> hdr_page(static_cast<size_t>(res) * res, glm::vec3(0.0f));
        for (const auto& tri : tris) {
            RasterizeTri(tri, res, hdr_page, params.lights, params.shadow_ray);
            result.tri_count++;
        }

        // Count lit texels for progress reporting.
        for (const auto& c : hdr_page)
            if (c.r > 0.001f || c.g > 0.001f || c.b > 0.001f) result.texel_count++;

        if (result.tri_count > 0 &&
            WriteOlmPage(dir + "\\" + result.olm_name, res, res, hdr_page, params.hdr)) {
            Logger::Get().Log(LogLevel::INFO,
                "[Bake] " + result.olm_name + ": " +
                std::to_string(result.tri_count) + " tris, " +
                std::to_string(result.texel_count) + " lit texels @ " +
                std::to_string(res) + (params.hdr ? " (HDR)" : ""));
            results.push_back(result);
        }
    }

    Logger::Get().Log(LogLevel::INFO,
        "[Bake] Level " + std::to_string(params.level_no) + ": wrote " +
        std::to_string(results.size()) + " page(s) to " + dir);
    return results;
}

} // namespace igi
