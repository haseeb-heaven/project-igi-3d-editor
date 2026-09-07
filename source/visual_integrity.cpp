#include "visual_integrity.h"

#include <algorithm>
#include <cmath>
#include <queue>
#include <sstream>

namespace igi {
namespace {

bool HasExpectedSize(const VisualIntegrityView& view) {
    if (view.width <= 0 || view.height <= 0) return false;
    const size_t pixels = static_cast<size_t>(view.width) * view.height;
    return view.targetMask.size() == pixels && view.partIds.size() == pixels &&
           view.sceneDepth.size() == pixels && view.targetDepth.size() == pixels;
}

void AddFinding(VisualIntegrityResult& result, const char* rule, int view_index,
                const std::string& part, int observed, int required,
                const char* reason) {
    result.findings.push_back({rule, part, view_index, observed, required, reason});
}

int CountInteriorHolePixels(const VisualIntegrityView& view) {
    const int width = view.width;
    const int height = view.height;
    std::vector<uint8_t> visited(view.targetMask.size(), 0);
    int largest_hole = 0;
    const int dx[] = {1, -1, 0, 0};
    const int dy[] = {0, 0, 1, -1};

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int start = y * width + x;
            if (visited[start] || view.targetMask[start] != 0) continue;

            std::queue<int> pending;
            pending.push(start);
            visited[start] = 1;
            int area = 0;
            bool touches_bounds = false;
            while (!pending.empty()) {
                const int index = pending.front();
                pending.pop();
                ++area;
                const int px = index % width;
                const int py = index / width;
                touches_bounds |= px == 0 || py == 0 || px == width - 1 || py == height - 1;
                for (int dir = 0; dir < 4; ++dir) {
                    const int nx = px + dx[dir];
                    const int ny = py + dy[dir];
                    if (nx < 0 || ny < 0 || nx >= width || ny >= height) continue;
                    const int next = ny * width + nx;
                    if (visited[next] || view.targetMask[next] != 0) continue;
                    visited[next] = 1;
                    pending.push(next);
                }
            }
            if (!touches_bounds) largest_hole = std::max(largest_hole, area);
        }
    }
    return largest_hole;
}

std::string EscapeJson(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (char ch : value) {
        if (ch == '\\' || ch == '"') escaped.push_back('\\');
        escaped.push_back(ch);
    }
    return escaped;
}

}  // namespace

VisualIntegrityResult EvaluateVisualIntegrity(const VisualIntegrityInput& input) {
    VisualIntegrityResult result;
    if (input.expectedPartIds.empty() || input.views.empty()) return result;

    // A part is expected only when the target-only geometry pass projects it in
    // at least one sampled view.  Authoring inventory alone is insufficient:
    // a zero-area plane or a permanently back-facing sheet has no observable
    // screen-space coverage requirement.
    std::vector<int> projected_parts;
    std::vector<int> observed_parts;
    bool has_valid_view = false;
    for (size_t view_index = 0; view_index < input.views.size(); ++view_index) {
        const VisualIntegrityView& view = input.views[view_index];
        if (!HasExpectedSize(view)) {
            AddFinding(result, "invalid-evidence", static_cast<int>(view_index), "", 0, 0,
                       "target, material, and depth buffers must match the viewport");
            continue;
        }
        has_valid_view = true;
        ++result.viewsChecked;
        bool view_failed = false;
        int target_pixels = 0;
        for (size_t pixel = 0; pixel < view.targetMask.size(); ++pixel) {
            // `partIds` is the target-only, geometry-derived projection.  It
            // establishes what can be demanded from this camera.  A fragment
            // is observed only when its depth also agrees with the rendered
            // scene, preventing the diagnostic pass from certifying geometry
            // that the normal renderer failed to emit.
            if (view.partIds[pixel] > 0) projected_parts.push_back(view.partIds[pixel]);
            if (view.targetMask[pixel] == 0) continue;
            if (view.partIds[pixel] > 0) observed_parts.push_back(view.partIds[pixel]);
            ++target_pixels;
            if (view.sceneDepth[pixel] < 1.0f &&
                std::fabs(view.sceneDepth[pixel] - view.targetDepth[pixel]) > input.depthEpsilon) {
                AddFinding(result, "depth-consistency", static_cast<int>(view_index), "",
                           0, 0, "target depth differs from the visible scene depth");
                view_failed = true;
                break;
            }
        }
        if (target_pixels == 0) {
            AddFinding(result, "target-presence", static_cast<int>(view_index), "", 0, 1,
                       "target produced no visible fragments");
            view_failed = true;
        }
        const int largest_hole = CountInteriorHolePixels(view);
        if (largest_hole >= input.minimumInteriorHolePixels) {
            AddFinding(result, "silhouette-hole", static_cast<int>(view_index), "",
                       largest_hole, input.minimumInteriorHolePixels,
                       "target mask contains an unexplained interior hole");
            view_failed = true;
        }
        if (view_failed) ++result.viewsFailed;
        else ++result.viewsPassed;
    }

    if (!has_valid_view) return result;
    const auto& required_parts = input.strictPartIds.empty()
        ? input.expectedPartIds : input.strictPartIds;
    for (int expected_part : required_parts) {
        const int projected = static_cast<int>(std::count(
            projected_parts.begin(), projected_parts.end(), expected_part));
        if (projected < input.minimumPartPixels) continue;
        const int pixels = static_cast<int>(std::count(observed_parts.begin(), observed_parts.end(), expected_part));
        if (pixels < input.minimumPartPixels) {
            AddFinding(result, "part-coverage", -1, std::to_string(expected_part), pixels,
                       input.minimumPartPixels,
                       "expected target part produced too few visible fragments");
        }
    }
    result.status = result.findings.empty() ? VisualIntegrityStatus::kPass
                                             : VisualIntegrityStatus::kFail;
    return result;
}

const char* VisualIntegrityStatusName(VisualIntegrityStatus status) {
    switch (status) {
        case VisualIntegrityStatus::kPass: return "PASS";
        case VisualIntegrityStatus::kFail: return "FAIL";
        case VisualIntegrityStatus::kInconclusive: return "INCONCLUSIVE";
    }
    return "INCONCLUSIVE";
}

std::string VisualIntegrityJson(const VisualIntegrityResult& result) {
    std::ostringstream output;
    output << "{\"status\":\"" << VisualIntegrityStatusName(result.status)
           << "\",\"viewsChecked\":" << result.viewsChecked
           << ",\"viewsPassed\":" << result.viewsPassed
           << ",\"viewsFailed\":" << result.viewsFailed
           << ",\"findings\":[";
    for (size_t index = 0; index < result.findings.size(); ++index) {
        const auto& finding = result.findings[index];
        if (index != 0) output << ',';
        output << "{\"rule\":\"" << EscapeJson(finding.rule)
               << "\",\"part\":\"" << EscapeJson(finding.part)
               << "\",\"viewIndex\":" << finding.viewIndex
               << ",\"observedPixels\":" << finding.observedPixels
               << ",\"requiredPixels\":" << finding.requiredPixels
               << ",\"reason\":\"" << EscapeJson(finding.reason) << "\"}";
    }
    output << "]}";
    return output.str();
}

}  // namespace igi
