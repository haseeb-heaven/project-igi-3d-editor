#include "light_fixture.h"
#include "mef_native.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <map>
#include <set>

namespace igi {

namespace {

std::string ToLower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// One emitter triangle, already in world space.
struct Triangle {
    std::string texture;
    glm::dvec3 a, b, c;

    glm::dvec3 Centroid() const { return (a + b + c) / 3.0; }
    double Area() const {
        return 0.5 * glm::length(glm::cross(b - a, c - a));
    }
};

double DistanceSquared(const glm::dvec3& l, const glm::dvec3& r) {
    glm::dvec3 d = l - r;
    return glm::dot(d, d);
}

// Union-find with path halving; unions always towards the lower index so a cluster's
// root is its first triangle (open-igi Find/Union, kept for determinism).
int Find(std::vector<int>& parent, int index) {
    while (parent[index] != index) {
        parent[index] = parent[parent[index]];
        index = parent[index];
    }
    return index;
}

void Union(std::vector<int>& parent, int left, int right) {
    int a = Find(parent, left);
    int b = Find(parent, right);
    if (a == b) return;
    if (a < b) parent[b] = a;
    else parent[a] = b;
}

// Spatial-hash clustering over triangle centroids: grid cells are exactly the joining
// distance across, so every near-enough neighbour is in this cell or the 26 around it.
std::vector<std::vector<int>> Cluster(const std::vector<Triangle>& triangles, float cluster_distance) {
    const int count = static_cast<int>(triangles.size());
    std::vector<int> parent(count);
    std::vector<glm::dvec3> centroids(count);
    for (int i = 0; i < count; ++i) {
        parent[i] = i;
        centroids[i] = triangles[i].Centroid();
    }

    const double cell = cluster_distance;
    const double limit = static_cast<double>(cluster_distance) * cluster_distance;
    std::map<std::tuple<long long, long long, long long>, std::vector<int>> grid;

    auto cellOf = [&](const glm::dvec3& at) {
        return std::make_tuple(
            static_cast<long long>(std::floor(at.x / cell)),
            static_cast<long long>(std::floor(at.y / cell)),
            static_cast<long long>(std::floor(at.z / cell)));
    };

    for (int i = 0; i < count; ++i) {
        const glm::dvec3 at = centroids[i];
        const auto [x, y, z] = cellOf(at);
        for (long long dx = -1; dx <= 1; ++dx)
            for (long long dy = -1; dy <= 1; ++dy)
                for (long long dz = -1; dz <= 1; ++dz) {
                    auto it = grid.find({x + dx, y + dy, z + dz});
                    if (it == grid.end()) continue;
                    for (int other : it->second) {
                        if (DistanceSquared(at, centroids[other]) <= limit) {
                            Union(parent, i, other);
                        }
                    }
                }
        grid[{x, y, z}].push_back(i);
    }

    // First-seen order rather than map order, so grouping does not depend on hashing.
    std::map<int, int> group;
    std::vector<std::vector<int>> clusters;
    for (int i = 0; i < count; ++i) {
        int root = Find(parent, i);
        auto it = group.find(root);
        if (it == group.end()) {
            it = group.emplace(root, static_cast<int>(clusters.size())).first;
            clusters.emplace_back();
        }
        clusters[it->second].push_back(i);
    }
    return clusters;
}

struct TextureContribution {
    double area = 0.0;
    int count = 0;
};

// Turns one cluster into the fixture it proposes (open-igi Describe).
LightFixture Describe(const std::vector<Triangle>& triangles,
                      const std::vector<int>& cluster,
                      float radius_scale) {
    double area = 0.0;
    glm::dvec3 weighted(0.0);
    glm::dvec3 plain(0.0);

    for (int index : cluster) {
        const Triangle& tri = triangles[index];
        const double own = tri.Area();
        area += own;
        weighted += tri.Centroid() * own;
        plain += tri.Centroid();
    }

    // Area-weighted: uneven tessellation must not drag the mean onto a sliver rim. The
    // unweighted fallback only matters for fully degenerate geometry.
    LightFixture fixture;
    fixture.position = area > 0.0 ? weighted * (1.0 / area) : plain * (1.0 / cluster.size());
    fixture.area = area;
    fixture.triangle_count = static_cast<int>(cluster.size());

    double extent_sq = 0.0;
    std::map<std::string, TextureContribution> by_texture; // lower-cased key
    std::map<std::string, std::string> original_names;
    for (int index : cluster) {
        const Triangle& tri = triangles[index];
        extent_sq = std::max(extent_sq, DistanceSquared(fixture.position, tri.a));
        extent_sq = std::max(extent_sq, DistanceSquared(fixture.position, tri.b));
        extent_sq = std::max(extent_sq, DistanceSquared(fixture.position, tri.c));
        std::string key = ToLower(tri.texture);
        by_texture[key].area += tri.Area();
        by_texture[key].count += 1;
        original_names[key] = tri.texture;
    }
    fixture.extent = std::sqrt(extent_sq);

    // Dominant texture by contributed area (count settles degenerate-only clusters).
    std::string dominant_key;
    double best_area = -1.0;
    int best_count = -1;
    for (const auto& [key, contrib] : by_texture) {
        if (contrib.area > best_area || (contrib.area == best_area && contrib.count > best_count)) {
            dominant_key = key;
            best_area = contrib.area;
            best_count = contrib.count;
        }
    }
    fixture.texture = original_names[dominant_key];

    // Report the centroid back in editor/game units so gizmos land on the drawn lamp.
    fixture.position *= static_cast<double>(kGameUnitsPerMetre);

    // Colour: white until a texture decoder feeds the survey (soft value, meant to be edited).
    fixture.color = glm::dvec3(1.0);

    // radius = extent * scale * sqrt(luminance), floored at kMinimumRadius.
    const double reach = fixture.extent * radius_scale *
                         std::sqrt(std::max(fixture.Luminance(), 0.0));
    fixture.radius = std::max(reach, static_cast<double>(kMinimumRadius));

    return fixture;
}

} // namespace

std::string LightFixture::IgnoreKey() const {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "|%.2f|%.2f|%.2f", position.x, position.y, position.z);
    return ToLower(texture) + buf;
}

glm::mat4 BuildModelMatrix(const glm::dvec3& pos, const glm::dvec3& rot, float obj_scale) {
    // Mirrors renderer_objects.cpp's placed-object composition exactly:
    // translate -> Yaw(Z) -> Pitch(X) -> Roll(Y) -> uniform scale (non-door branch —
    // doors are not lamp hosts). base scale 40.96 matches the renderer's world-unit scale.
    glm::mat4 model(1.0f);
    model = glm::translate(model, glm::vec3(pos.x, pos.y, pos.z));
    model = glm::rotate(model, static_cast<float>(rot.z), glm::vec3(0.0f, 0.0f, 1.0f)); // Yaw
    model = glm::rotate(model, static_cast<float>(rot.x), glm::vec3(1.0f, 0.0f, 0.0f)); // Pitch
    model = glm::rotate(model, static_cast<float>(rot.y), glm::vec3(0.0f, 1.0f, 0.0f)); // Roll
    model = glm::scale(model, glm::vec3(40.96f * obj_scale));
    return model;
}

std::vector<LightFixture> ProposeFixtures(
    const std::vector<PlacedMeshInput>& meshes,
    const std::vector<std::string>& emitter_textures,
    float cluster_distance,
    float radius_scale) {
    std::set<std::string> emitters;
    for (const std::string& t : emitter_textures) emitters.insert(ToLower(t));
    if (emitters.empty()) return {};

    std::vector<Triangle> triangles;
    for (const PlacedMeshInput& mesh : meshes) {
        if (!mesh.geometry || !mesh.geometry->fromRenderMesh) continue;

        const auto& verts = mesh.geometry->vertices;
        const auto& tris = mesh.geometry->triangles;
        const auto& blocks = mesh.geometry->renderBlocks;

        for (const auto& block : blocks) {
            if (block.materialSlot < 0 ||
                static_cast<size_t>(block.materialSlot) >= mesh.slot_textures.size()) continue;
            const std::string& tex = mesh.slot_textures[static_cast<size_t>(block.materialSlot)];
            if (!emitters.count(ToLower(tex))) continue;

            // Clamped rather than trusted: a malformed pack range costs its own triangles
            // only (same stance as open-igi Gather).
            const size_t start = block.triangleStart;
            const size_t end = std::min(start + block.triangleCount, tris.size());
            for (size_t t = start; t < end; ++t) {
                const auto& tri = tris[t];
                if (tri[0] >= verts.size() || tri[1] >= verts.size() || tri[2] >= verts.size()) continue;
                auto toMetres = [&](const glm::vec3& local) {
                    // Game-unit world position -> metres (open-igi's working unit).
                    return glm::dvec3(mesh.model_matrix * glm::vec4(local, 1.0f)) /
                           static_cast<double>(kGameUnitsPerMetre);
                };
                triangles.push_back({tex,
                    toMetres(verts[tri[0]].pos),
                    toMetres(verts[tri[1]].pos),
                    toMetres(verts[tri[2]].pos)});
            }
        }
    }
    if (triangles.empty()) return {};

    std::vector<LightFixture> fixtures;
    for (const auto& cluster : Cluster(triangles, cluster_distance)) {
        fixtures.push_back(Describe(triangles, cluster, radius_scale));
    }

    // Strongest first; position breaks ties so opposite-wall twins keep stable order
    // (the engine's determinism contract applies to tool output too).
    std::sort(fixtures.begin(), fixtures.end(), [](const LightFixture& l, const LightFixture& r) {
        if (l.Intensity() != r.Intensity()) return l.Intensity() > r.Intensity();
        if (l.position.x != r.position.x) return l.position.x < r.position.x;
        if (l.position.y != r.position.y) return l.position.y < r.position.y;
        return l.position.z < r.position.z;
    });
    return fixtures;
}

std::vector<std::string> SuggestEmitterTexturesByHeuristic(
    const std::vector<std::string>& all_texture_names) {
    static const char* kKeywords[] = {"lamp", "light", "neon"};
    std::vector<std::string> out;
    for (const std::string& name : all_texture_names) {
        std::string low = ToLower(name);
        for (const char* kw : kKeywords) {
            if (low.find(kw) != std::string::npos) {
                out.push_back(name);
                break;
            }
        }
    }
    return out;
}

} // namespace igi
