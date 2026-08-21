#include "object_lightmap.h"
#include "res_writer.h"
#include "../utils.h"
#include "../logger.h"
#include <filesystem>
#include <cstring>

namespace igi {

ObjectLightmapManager& ObjectLightmapManager::Get() {
    static ObjectLightmapManager s_instance;
    return s_instance;
}

ObjectLightmapManager::~ObjectLightmapManager() {
    Clear();
}

void ObjectLightmapManager::Clear() {
    for (auto& pair : lightmaps_) {
        for (auto& page : pair.second) {
            if (page.texture_id != 0) {
                glDeleteTextures(1, &page.texture_id);
                page.texture_id = 0;
            }
        }
    }
    lightmaps_.clear();
    current_level_no_ = 0;
}

void ObjectLightmapManager::CycleRenderMode() {
    switch (render_mode_) {
        case LightmapRenderMode::Baked:   render_mode_ = LightmapRenderMode::Hybrid; break;
        case LightmapRenderMode::Hybrid:  render_mode_ = LightmapRenderMode::Dynamic; break;
        case LightmapRenderMode::Dynamic: render_mode_ = LightmapRenderMode::Off; break;
        case LightmapRenderMode::Off:     render_mode_ = LightmapRenderMode::Baked; break;
    }
}

const char* ObjectLightmapManager::GetRenderModeName() const {
    switch (render_mode_) {
        case LightmapRenderMode::Baked:   return "Baked";
        case LightmapRenderMode::Hybrid:  return "Hybrid";
        case LightmapRenderMode::Dynamic: return "Dynamic";
        case LightmapRenderMode::Off:     return "Disabled";
        default: return "Unknown";
    }
}

void ObjectLightmapManager::LoadLevelLightmaps(int level_no) {
    if (current_level_no_ == level_no && !lightmaps_.empty()) {
        return;
    }

    Clear();
    current_level_no_ = level_no;

    std::string igi_root = Utils::GetIGIRootPath();
    std::string res_path = igi_root + "\\missions\\location0\\level" + std::to_string(level_no) + "\\lightmaps\\lightmaps.res";

    if (!std::filesystem::exists(res_path)) {
        Logger::Get().Log(LogLevel::WARNING, "[Lightmap] Archive not found at: " + res_path);
        return;
    }

    size_t loaded_count = 0;
    std::string err;
    RES_ForEachEntry(res_path, [&](const std::string& name, const uint8_t* data, size_t size) {
        if (size < 60 + 44 || !data) return;

        // Parse OLM header
        uint32_t count = 0;
        std::memcpy(&count, data + 44, sizeof(uint32_t));
        if (count == 0 || count > 64) return;

        size_t pixel_start = 60 + count * 44;
        if (pixel_start > size) return;

        std::vector<LightmapPage> pages;
        size_t offset = pixel_start;

        for (uint32_t i = 0; i < count; ++i) {
            size_t dims = 60 + i * 44 + 40;
            uint16_t w = 0, h = 0;
            std::memcpy(&w, data + dims, sizeof(uint16_t));
            std::memcpy(&h, data + dims + 2, sizeof(uint16_t));

            size_t bytes = 4ULL * w * h;
            if (offset + bytes > size) break;

            // Convert BGRA to RGBA for OpenGL
            const uint8_t* bgra = data + offset;
            std::vector<uint8_t> rgba(bytes);
            for (size_t p = 0; p < bytes; p += 4) {
                rgba[p + 0] = bgra[p + 2]; // R
                rgba[p + 1] = bgra[p + 1]; // G
                rgba[p + 2] = bgra[p + 0]; // B
                rgba[p + 3] = bgra[p + 3]; // A
            }

            LightmapPage page;
            page.width = w;
            page.height = h;

            glGenTextures(1, &page.texture_id);
            glBindTexture(GL_TEXTURE_2D, page.texture_id);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
            glBindTexture(GL_TEXTURE_2D, 0);

            pages.push_back(page);
            offset += bytes;
        }

        if (!pages.empty()) {
            std::string bare_name = name;
            size_t slash = bare_name.find_last_of("/\\");
            if (slash != std::string::npos) bare_name = bare_name.substr(slash + 1);
            lightmaps_[bare_name] = std::move(pages);
            loaded_count++;
        }
    }, err);

    Logger::Get().Log(LogLevel::INFO, "[Lightmap] Loaded " + std::to_string(loaded_count) + " lightmap entries for Level " + std::to_string(level_no));
}

GLuint ObjectLightmapManager::GetLightmapTexture(const std::string& object_name, int draw_record_index) {
    if (render_mode_ == LightmapRenderMode::Dynamic) {
        return 0;
    }

    char buf[128];
    snprintf(buf, sizeof(buf), "%s_%05d.olm", object_name.c_str(), draw_record_index);
    auto it = lightmaps_.find(buf);
    if (it != lightmaps_.end() && !it->second.empty()) {
        return it->second[0].texture_id;
    }
    return 0;
}

} // namespace igi
