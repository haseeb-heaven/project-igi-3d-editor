#pragma once
#include "../pch.h"
#include "../level/level_objects.h"
#include "renderer_objects.h"
#include "spline_placement.h"
#include <functional>

class Renderer_Splines {
public:
    Renderer_Splines(Renderer_Objects& obj_renderer) : obj_renderer_(obj_renderer) {}

    void Init();
    void Draw(const std::vector<LevelObject>& objects, GLuint ubo_mats, GLuint shader_program);

    // Optional terrain height callback — when set, tile Z positions are snapped to
    // max(hermite_z, terrain_z) so flat track sits on terrain and elevated sections
    // stay above it. Signature: (world_x, world_y, out_z) → true if terrain found.
    void SetTerrainQuery(std::function<bool(double, double, float&)> fn) {
        terrain_z_fn_ = std::move(fn);
    }

private:
    Renderer_Objects& obj_renderer_;
    std::function<bool(double, double, float&)> terrain_z_fn_;

    // One stretched deck tile (plus its ATTA details) at a precomputed placement.
    void DrawTile(const Mesh& mesh, const std::string& seg_model_id,
                  const glm::mat4& unscaled_model, const glm::vec3& stretch,
                  GLuint ubo_mats, GLuint shader_program);
};
