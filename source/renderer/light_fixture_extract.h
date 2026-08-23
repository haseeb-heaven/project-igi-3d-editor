#pragma once
#include <string>
#include <vector>

// Level-walk convenience over ProposeFixtures (open-igi LightFixtureExtractor.ProposeFromScene
// equivalent): resolves every placed object to re-parsed MEF geometry + its DAT texture map,
// composes the same model matrix the renderer draws with, proposes fixtures, and populates
// the LightFixtureStore workspace.
//
// Callable from a future menu button ("Infer Light Fixtures" under Rendering, next to Bake
// Lightmaps). UI hookup deliberately NOT part of this pass (issue #63 scope).
//
// Emitter-texture source: pass a human-reviewed list when available (the authoritative survey);
// when `emitter_textures` is empty the documented name heuristic runs over every texture the
// level's models reference (SuggestEmitterTexturesByHeuristic).

#include "renderer_objects.h" // global-namespace types: Renderer_Objects, LevelObject
#include "light_fixture.h"

namespace igi {

// Extracts proposals for `level_no` from `objects`, using `renderer`'s mesh/texture caches.
// Returns the proposed fixture count written into the store (ignored ones included).
int ExtractFixturesForLevel(
    int level_no,
    const std::vector<LevelObject>& objects,
    Renderer_Objects& renderer,
    const std::vector<std::string>& emitter_textures = {},
    float cluster_distance = kDefaultClusterDistance,
    float radius_scale = kDefaultRadiusScale);

} // namespace igi
