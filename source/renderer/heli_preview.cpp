#include "heli_preview.h"
#include "../level/level_objects.h"
#include "../logger.h"
#include <algorithm>
#include <cctype>

// Retail-parity helicopter preview parameters — see heli_preview.h for the
// full evidence chain (igi2.pdb symbols via open-igi).

namespace heli_preview {

const PhysicsRecord& GetPhysics() {
    static const PhysicsRecord kRecord; // Bell/Mil shared values, see header
    return kRecord;
}

bool IsKnownRotorModel(const std::string& modelId) {
    // VERIFIED: rotor model ids embedded in physicsobj/helis/{bell,mil}/HELI.QVM
    static const char* kRotorModels[] = {
        "711_01_1", "711_02_1",            // BELL rotors
        "700_03_1", "700_04_1", "700_02_1" // MIL rotors
    };
    for (const char* m : kRotorModels) {
        if (modelId == m) return true;
    }
    return false;
}

bool IsKnownHeliBodyModel(const std::string& modelId) {
    // VERIFIED: body model ids embedded in the same retail scripts.
    static const char* kBodyModels[] = {"709_01_1", "700_01_1"};
    for (const char* m : kBodyModels) {
        if (modelId == m) return true;
    }
    return false;
}

float NormalizeCollective(float authoredThrust) {
    // Retail reads authored collective as a plain control Real32 (0x431B70).
    // Vanilla data authors [0..1]; clamp so stray values cannot invert the spin.
    return std::min(std::max(authoredThrust, 0.0f), 1.0f);
}

static float CollectiveScale(float collective) {
    // INFERRED magnitude mapping: linear in collective, matching the retail
    // RotorPhase += Thrust proportionality (CutsceneRuntime.cs ~1143). At full
    // collective the preview keeps its historical rates (15 / 25 rad/s); at
    // zero collective the rotors stop, as in-game.
    return NormalizeCollective(collective);
}

float MainRotorAngularSpeed(float collective) {
    return 15.0f * CollectiveScale(collective);
}

float TailRotorAngularSpeed(float collective) {
    // Tail spins opposite the main rotor in the existing preview; keep the sign
    // convention and scale by the same collective factor.
    return -25.0f * CollectiveScale(collective);
}

} // namespace heli_preview

// ---------------------------------------------------------------------------
// Authored collective lookup from decompiled QSC data.
//
// Levels declare per-type parameter layouts with a flat call:
//   Task_DeclareParameters("Heli", "<name0>", "<type0>", "<name1>", ...)
// (same shape the terrain writer emits — see terrain_io.cpp examples). The
// editor's QSC parser stores every non-child argument of an object task in
// LevelObject::argTokens starting at index 0 = task id, 1 = type, 2 = name,
// so declared parameter i lives at argTokens[3 + i].
//
// open-igi resolves the Heli collective by parameter NAME ("Original Thrust",
// space-insensitive — CutsceneRuntime.cs RealNamed, ~1387) because the layout
// is level-authored, not fixed. We do the same instead of hardcoding offsets.

namespace {

std::string CompactLower(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == ' ' || c == '\t' || c == '"') continue;
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

} // namespace

namespace heli_preview {

// Reads the authored collective at `valueOffset` from a Heli task's argTokens.
// -1 (caller keeps default spin) when the declaration or token is missing/invalid.
float ResolveCollectiveFromTokens(const LevelObject& heliObj, int valueOffset) {
    if (valueOffset < 0 ||
        valueOffset >= static_cast<int>(heliObj.argTokens.size())) {
        return -1.0f;
    }
    try {
        std::string tok = heliObj.argTokens[valueOffset];
        tok.erase(std::remove(tok.begin(), tok.end(), '"'), tok.end());
        return std::stof(tok);
    } catch (...) {
        return -1.0f;
    }
}

float LookupAuthoredCollective(const std::vector<LevelObject>* objects,
                               const LevelObject& heliObj,
                               DeclarationIndex& declCache) {
    if (!objects || heliObj.type != "Heli") return -1.0f;

    // Resolve (and cache) which argToken offset carries the authored collective
    // for this level's "Heli" declaration.
    // declCache is cleared per level switch in Renderer_Objects::ClearCaches(),
    // so entries here are always from the current level's declarations.
    auto cached = declCache.find("Heli");
    if (cached != declCache.end()) return ResolveCollectiveFromTokens(heliObj, cached->second);

    int valueOffset = -1;
    int decl_count = 0;
    for (const LevelObject& o : *objects) {
        if (o.type != "Task_DeclareParameters" || o.argTokens.empty()) continue;
        if (CompactLower(o.argTokens[0]) != "heli") continue;
        ++decl_count;
        // argTokens: [0]="Heli", then alternating "<name>", "<type>" pairs.
        // Multi-component types consume several object tokens (e.g. an
        // "ObjectPos" position parameter spans x/y/z), so the object-side
        // offset accumulates per-type widths rather than counting pairs.
        int tokenOffset = 3;
        for (size_t i = 1; i + 1 < o.argTokens.size(); i += 2) {
            const std::string& name = o.argTokens[i];
            const std::string& type = CompactLower(o.argTokens[i + 1]);
            if (CompactLower(name) == "originalthrust") {
                valueOffset = tokenOffset;
                break;
            }
            int width = 1;
            if (type == "objectpos") width = 3;
            else if (type.size() > 2 && type.compare(type.size() - 2, 2, "x9") == 0) width = 9;
            tokenOffset += width;
        }
        if (valueOffset >= 0) break; // first resolvable "Heli" declaration wins
    }
    if (decl_count > 1) {
        // Review finding (#65): multiple Heli layouts — first-resolvable-wins is
        // the retail order (load-order array), but stay diagnosable.
        Logger::Get().Log(LogLevel::DEBUG,
            "[HeliPreview] " + std::to_string(decl_count) +
            " Heli Task_DeclareParameters found; using the first with Original Thrust (offset " +
            std::to_string(valueOffset) + ")");
    }
    declCache["Heli"] = valueOffset;
    return ResolveCollectiveFromTokens(heliObj, valueOffset);
}

} // namespace heli_preview
