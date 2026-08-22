#include "../pch.h"
#include "light_fixture_extract.h"
#include "light_fixture_store.h"
#include "../logger.h"

#include <map>
#include <set>

namespace igi {

int ExtractFixturesForLevel(
    int level_no,
    const std::vector<LevelObject>& objects,
    Renderer_Objects& renderer,
    const std::vector<std::string>& emitter_textures,
    float cluster_distance,
    float radius_scale) {
    // Build each unique model's geometry once however many times the level draws it —
    // a level draws the same lamp dozens of times (open-igi ProposeFromScene note).
    struct ModelData {
        ParsedGeometry geometry;
        std::vector<std::string> slot_textures;
    };
    std::map<std::string, ModelData> built;
    std::vector<PlacedMeshInput> meshes;

    for (const auto& obj : objects) {
        if (obj.deleted || obj.modelId.empty()) continue;
        // Splines carry bent per-placement meshes the editor does not model as MEF data;
        // ATTA proxies duplicate their parent's geometry and would double-count lamps.
        if (obj.isSplineContainer || obj.isSplineWaypoint || obj.isAttaProxy) continue;

        auto it = built.find(obj.modelId);
        if (it == built.end()) {
            ModelData md;
            std::vector<uint8_t> bytes = renderer.FindMeshData(obj.modelId);
            if (!bytes.empty()) {
                try {
                    md.geometry = ParseMefFileFromMemory(bytes);
                } catch (...) {
                    Logger::Get().Log(LogLevel::WARNING,
                        "[LightFixture] MEF parse failed for '" + obj.modelId + "' — skipped");
                }
            }
            md.slot_textures = renderer.GetTextureIdsForModel(obj.modelId);
            it = built.emplace(obj.modelId, std::move(md)).first;
        }
        if (!it->second.geometry.fromRenderMesh) continue; // no real materials to survey

        PlacedMeshInput input;
        input.geometry = &it->second.geometry;
        input.slot_textures = it->second.slot_textures;
        // Same composition the viewport uses for this object (non-door branch; doors
        // are not lamp hosts) — a fixture must sit where the drawn lamp sits.
        input.model_matrix = BuildModelMatrix(obj.pos, obj.rot, obj.scale);
        meshes.push_back(std::move(input));
    }

    // Emitter list: human-reviewed when supplied; otherwise the documented name heuristic
    // over every texture the level's models reference.
    std::vector<std::string> emitters = emitter_textures;
    if (emitters.empty()) {
        std::set<std::string> all_names;
        for (const auto& [model_id, md] : built) {
            for (const auto& t : md.slot_textures) {
                if (!t.empty()) all_names.insert(t);
            }
        }
        emitters = SuggestEmitterTexturesByHeuristic(
            {all_names.begin(), all_names.end()});
        Logger::Get().Log(LogLevel::INFO,
            "[LightFixture] No reviewed emitter list — name heuristic proposed "
            + std::to_string(emitters.size()) + " candidate texture(s) from "
            + std::to_string(all_names.size()));
    }
    if (emitters.empty()) return 0;

    std::vector<LightFixture> fixtures =
        ProposeFixtures(meshes, emitters, cluster_distance, radius_scale);

    LightFixtureStore::Get().SetLevelNo(level_no);
    LightFixtureStore::Get().SetFixtures(std::move(fixtures));

    Logger::Get().Log(LogLevel::INFO,
        "[LightFixture] Proposed " +
        std::to_string(LightFixtureStore::Get().Fixtures().size()) +
        " light fixture(s) from " + std::to_string(meshes.size()) +
        " placement(s) on level " + std::to_string(level_no) +
        ". Every one is a candidate for review, not a light the level declared.");
    return static_cast<int>(LightFixtureStore::Get().Fixtures().size());
}

} // namespace igi
