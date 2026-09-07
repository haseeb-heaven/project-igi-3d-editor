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
};

struct VisualIntegrityView {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> targetMask;
    std::vector<int> partIds;
    std::vector<float> sceneDepth;
    std::vector<float> targetDepth;
    std::string name;
};

struct VisualIntegrityInput {
    std::vector<int> expectedPartIds;
    // Parts with an independent attachment instance are not allowed to be
    // satisfied by another root surface.  Root parts may be self-occluded by
    // authored target geometry and are evaluated only when they are exposed.
    std::vector<int> strictPartIds;
    std::vector<VisualIntegrityView> views;
    int minimumPartPixels = 1;
    int minimumInteriorHolePixels = 16;
    float depthEpsilon = 0.0001f;
};

struct VisualIntegrityResult {
    VisualIntegrityStatus status = VisualIntegrityStatus::kInconclusive;
    int viewsChecked = 0;
    int viewsPassed = 0;
    int viewsFailed = 0;
    std::vector<VisualIntegrityFinding> findings;
};

VisualIntegrityResult EvaluateVisualIntegrity(const VisualIntegrityInput& input);
const char* VisualIntegrityStatusName(VisualIntegrityStatus status);
std::string VisualIntegrityJson(const VisualIntegrityResult& result);

}  // namespace igi
