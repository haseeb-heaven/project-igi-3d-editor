#include "pch.h"
#include "renderer_splines.h"
#include "railroad_path.h"
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

void Renderer_Splines::Init() {}

// Geometry span in model X. minX is the model's near edge; localLen is the
// full X extent. A natural (unstretched) tile spans localLen * 40.96 in world units.
static constexpr float kLengthScale = 40.96f;

void Renderer_Splines::Draw(
    const std::vector<LevelObject>& objects,
    GLuint ubo_mats,
    GLuint shader_program)
{
    if (!shader_program) return;

    glUseProgram(shader_program);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, ubo_mats);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);

    for (const auto& obj : objects) {
        if (!obj.isSplineContainer || obj.deleted) continue;
        if (Renderer_Objects::IsSkippedModelId(obj.modelId)) continue;

        const auto& children = obj.childrenIndices;
        if (children.size() < 2) continue;

        // Validate child indices once; skip deleted waypoints.
        std::vector<const LevelObject*> wps;
        wps.reserve(children.size());
        bool any_deleted = false;
        for (int ci : children) {
            if (ci < 0 || ci >= (int)objects.size()) { wps.clear(); break; }
            if (objects[ci].deleted) { any_deleted = true; break; }
            wps.push_back(&objects[ci]);
        }
        if (wps.size() < 2 || any_deleted) continue;

        // First non-empty segmentModelId among the waypoints, else the container's
        // own fallback — same resolution order as before #69.
        std::string segModelId;
        for (const auto* wp : wps) {
            if (!wp->segmentModelId.empty()) { segModelId = wp->segmentModelId; break; }
        }
        if (segModelId.empty()) continue;
        if (Renderer_Objects::IsSkippedModelId(segModelId)) continue;

        Mesh mesh = obj_renderer_.GetOrLoadMesh(segModelId, false);
        if (mesh.vertexCount == 0) continue;

        float local_len = mesh.halfExtents.x * 2.0f;
        if (local_len < 1.0f) local_len = 1.0f;
        const float tile_world_len = local_len * kLengthScale;

        // Authored-orientation policy (same as train placement, #59): build the
        // waypoint-frame Hermite track when at least one waypoint carries a
        // non-zero authored rotation; otherwise keep the legacy chord-tangent
        // curve. Authored rotations give retail zero-roll frame tangents and
        // remove corner-cutting on curved roads/bridges.
        bool authored = false;
        for (const auto* wp : wps) {
            if (wp->rot != glm::dvec3(0.0)) { authored = true; break; }
        }

        igi::SplinePlacementTrack track;
        if (authored) {
            std::vector<igi::RailroadWaypoint> rwps;
            rwps.reserve(wps.size());
            for (const auto* wp : wps) {
                igi::RailroadWaypoint rwp;
                rwp.position = wp->pos;
                rwp.alpha = static_cast<float>(wp->rot.x); // editor X == engine alpha
                rwp.beta = static_cast<float>(wp->rot.y);  // editor Y == engine beta
                rwp.gamma = static_cast<float>(wp->rot.z); // editor Z == engine gamma
                rwps.push_back(rwp);
            }
            auto path = igi::RailroadPath::Build(rwps, /*flipped=*/false,
                                                 /*initial_position=*/0.0,
                                                 /*offset_x=*/0.0, /*offset_y=*/0.0);
            if (!path.has_value()) continue;
            const float total = path->TotalLength();
            // ~4 samples per expected tile keeps chord error well under a texel.
            const int n_samples = std::max(8, (int)std::ceil(total / (tile_world_len * 0.25f)) + 1);
            for (int k = 0; k <= n_samples; ++k) {
                const double arc = total * (double)k / (double)n_samples;
                glm::dvec3 pos;
                glm::dmat3 orient;
                path->Evaluate(arc, pos, orient);
                track.AddSample(pos, glm::vec3(orient[0]));
            }
        } else {
            std::vector<glm::vec3> knots;
            knots.reserve(wps.size());
            for (const auto* wp : wps) knots.push_back(glm::vec3(wp->pos));
            track = igi::BuildLegacyTrack(knots);
        }

        const float total_len = track.TotalLength();
        if (total_len < 1.0f) continue;

        GLint loc_model    = glGetUniformLocation(shader_program, "u_model");
        GLint loc_dirlight = glGetUniformLocation(shader_program, "u_dirlight");
        GLint loc_ambient  = glGetUniformLocation(shader_program, "u_ambient");
        GLint loc_useTex   = glGetUniformLocation(shader_program, "u_useTexture");
        GLint loc_tex      = glGetUniformLocation(shader_program, "u_texture");

        // Issue #69: tiles step by ARC LENGTH across the whole spline (the old path
        // restarted the count per knot pair). The count that lands closest to the
        // tile's natural authored-extent length wins; every tile then stretches
        // uniformly so they butt end-to-end with no gaps or overlaps.
        const int steps = igi::ChooseTileCount(total_len, tile_world_len, /*max_tiles=*/64);
        const float run = total_len / (float)steps;

        const float min_x = mesh.center.x - mesh.halfExtents.x;

        for (int i = 0; i < steps; ++i) {
            const float arc_a = run * (float)i;
            const float arc_b = run * (float)(i + 1);
            const float arc_mid = 0.5f * (arc_a + arc_b);

            const auto sa = track.SampleAtArc(arc_a);
            const auto sb = track.SampleAtArc(arc_b);
            const auto sm = track.SampleAtArc(arc_mid);

            const glm::vec3 a(sa.pos);
            const glm::vec3 b(sb.pos);
            const float chord_len = glm::length(b - a);
            if (chord_len < 0.001f) continue;

            // Orientation from the curve frame's forward column (zero-roll): the
            // authored track carries RailroadPath's frame X; the legacy track
            // carries the curve tangent. Pitch follows the sampled grade, so
            // slope runs connect seamlessly to their neighbours.
            const glm::vec3 forward = sm.forward;

            // Stretch X so the tile covers its arc-length share exactly; Y/Z keep
            // natural scale so the cross-section stays undistorted.
            const float sx = chord_len / local_len;

            // Place so the model's near edge (local minX) lands on point a.
            const glm::vec3 pos = a - forward * (sx * min_x);

            glm::vec3 right = glm::cross(glm::vec3(0.0f, 0.0f, 1.0f), forward);
            if (glm::length(right) < 0.001f)               // near-vertical fallback
                right = glm::normalize(glm::cross(glm::vec3(1.0f, 0.0f, 0.0f), forward));
            right = glm::normalize(right);
            const glm::vec3 up = glm::cross(forward, right);

            glm::mat4 rot_mat(1.0f);
            rot_mat[0] = glm::vec4(forward, 0.0f); // local X → track direction
            rot_mat[1] = glm::vec4(right, 0.0f);   // local Y → track width
            rot_mat[2] = glm::vec4(up, 0.0f);      // local Z → track up

            glm::mat4 unscaled_model = glm::translate(glm::mat4(1.0f), pos) * rot_mat;
            glm::mat4 model = glm::scale(unscaled_model, glm::vec3(sx, kLengthScale, kLengthScale));

            glUniformMatrix4fv(loc_model, 1, GL_FALSE, glm::value_ptr(model));

            DrawTile(mesh, segModelId, unscaled_model,
                     glm::vec3(sx, kLengthScale, kLengthScale), ubo_mats, shader_program);

            glUseProgram(shader_program);
            glBindBufferBase(GL_UNIFORM_BUFFER, 0, ubo_mats);
            glDepthFunc(GL_LESS);
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
            glDisable(GL_POLYGON_OFFSET_FILL);
        }
    }

    glDisable(GL_CULL_FACE);
    glUseProgram(0);
}

void Renderer_Splines::DrawTile(const Mesh& mesh, const std::string& seg_model_id,
                                const glm::mat4& unscaled_model,
                                const glm::vec3& stretch, GLuint ubo_mats,
                                GLuint shader_program)
{
    GLint loc_dirlight = glGetUniformLocation(shader_program, "u_dirlight");
    GLint loc_ambient  = glGetUniformLocation(shader_program, "u_ambient");
    GLint loc_useTex   = glGetUniformLocation(shader_program, "u_useTexture");
    GLint loc_tex      = glGetUniformLocation(shader_program, "u_texture");

    for (const auto& sub : mesh.subMeshes) {
        if (sub.VAO == 0 || sub.vertexCount == 0) continue;
        if (sub.textureID > 0) {
            glUniform3f(loc_dirlight, 0.6f, 0.6f, 0.6f);
            glUniform3f(loc_ambient,  0.4f, 0.4f, 0.4f);
            glUniform1i(loc_useTex, 1);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, sub.textureID);
            glUniform1i(loc_tex, 0);
        } else {
            glUniform3f(loc_dirlight, 0.7f, 0.7f, 0.7f);
            glUniform3f(loc_ambient,  0.2f, 0.2f, 0.2f);
            glUniform1i(loc_useTex, 0);
        }
        glBindVertexArray(sub.VAO);
        glDrawArrays(GL_TRIANGLES, 0, sub.vertexCount);
    }
    glBindVertexArray(0);

    // Rails/details (ATTA) use the same orientation and the same X stretch so they
    // stay aligned with the stretched deck tile.
    obj_renderer_.DrawAttachmentsForSpline(seg_model_id, /*isBuilding=*/false,
                                           unscaled_model, ubo_mats, stretch);

    (void)ubo_mats;
}
