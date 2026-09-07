#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace igi {

enum class VisualIntegrityStatus {
    kPass,
    kFail,
    kInconclusive,
};

struct VisualIntegrityFinding {
    std::string rule;
    std::string part;
    int viewIndex = -1;
    int observedPixels = 0;
    int requiredPixels = 0;
    std::string reason;
    std::string view;
    std::string evidence;
};

// Geometry/material inventory emitted by the target-only renderer pass. IDs
// are local to one capture and map directly to `VisualIntegrityView::partIds`.
struct VisualIntegrityPart {
    int id = 0;
    int vertexCount = 0;
    int triangleCount = 0;
    int materialSlot = -1;
    int alphaMode = 0;  // 0=opaque, 1=mask, 2=blend
};

struct VisualIntegrityView {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> targetMask;
    std::vector<int> partIds;
    std::vector<float> sceneDepth;
    std::vector<float> targetDepth;
    std::string name;
    std::string sourceFramePath;
    std::string overlayPath;
    // A capture command selects an authored task. The caller records whether
    // the runtime transform still agrees with that authored anchor.
    bool transformMatchesAuthored = true;
    // Two samples with the same non-negative group were rendered from the
    // same camera pose and can therefore be compared for temporal stability.
    int temporalGroup = -1;
};

struct VisualIntegrityInput {
    std::vector<int> expectedPartIds;
    std::vector<VisualIntegrityPart> expectedParts;
    // Parts with an independent attachment instance are not allowed to be
    // satisfied by another root surface.  Root parts may be self-occluded by
    // authored target geometry and are evaluated only when they are exposed.
    std::vector<int> strictPartIds;
    std::vector<VisualIntegrityView> views;
    int minimumPartPixels = 1;
    int minimumInteriorHolePixels = 16;
    float depthEpsilon = 0.0001f;
    bool requireTemporalEvidence = false;
    // Fractional target-area change allowed between repeated same-camera
    // samples. Camera motion is never compared across temporal groups.
    float maximumTemporalAreaDelta = 0.05f;
};

struct VisualIntegrityResult {
    VisualIntegrityStatus status = VisualIntegrityStatus::kInconclusive;
    int viewsChecked = 0;
    int viewsPassed = 0;
    int viewsFailed = 0;
    int partsExpected = 0;
    int partsObserved = 0;
    std::vector<VisualIntegrityFinding> findings;
};

VisualIntegrityResult EvaluateVisualIntegrity(const VisualIntegrityInput& input);
const char* VisualIntegrityStatusName(VisualIntegrityStatus status);
std::string VisualIntegrityJson(const VisualIntegrityResult& result);

}  // namespace igi
