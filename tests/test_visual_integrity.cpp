#include <gtest/gtest.h>

#include "visual_integrity.h"

namespace {

igi::VisualIntegrityInput MakeInput(
    std::vector<int> expected_parts,
    std::vector<igi::VisualIntegrityView> views) {
    igi::VisualIntegrityInput input;
    input.expectedPartIds = std::move(expected_parts);
    input.views = std::move(views);
    return input;
}

igi::VisualIntegrityView MakeView(std::vector<int> part_ids,
                                  int width = 4, int height = 4) {
    igi::VisualIntegrityView view;
    view.width = width;
    view.height = height;
    view.targetMask.assign(static_cast<size_t>(width * height), 1);
    view.partIds = std::move(part_ids);
    view.sceneDepth.assign(static_cast<size_t>(width * height), 0.5f);
    view.targetDepth.assign(static_cast<size_t>(width * height), 0.5f);
    return view;
}

bool HasRule(const igi::VisualIntegrityResult& result, const char* rule) {
    return std::any_of(result.findings.begin(), result.findings.end(),
        [rule](const igi::VisualIntegrityFinding& finding) {
            return finding.rule == rule;
        });
}

}  // namespace

TEST(VisualIntegrityTest, FailsWhenExpectedPartHasNoFragments) {
    // Part 2 is projected but absent from the visible target mask.
    auto view = MakeView({1, 1, 1, 1, 1, 1, 1, 1,
                          2, 2, 2, 2, 2, 2, 2, 2});
    for (size_t index = 8; index < view.targetMask.size(); ++index)
        view.targetMask[index] = 0;
    const auto missing = igi::EvaluateVisualIntegrity(MakeInput({1, 2}, {view}));

    EXPECT_EQ(missing.status, igi::VisualIntegrityStatus::kFail);
    EXPECT_TRUE(HasRule(missing, "part-coverage"));
}

TEST(VisualIntegrityTest, PassesWhenEveryExpectedPartHasVisibleFragments) {
    const auto result = igi::EvaluateVisualIntegrity(MakeInput(
        {1, 2}, {MakeView({1, 1, 1, 1, 1, 1, 1, 1,
                           2, 2, 2, 2, 2, 2, 2, 2})}));

    EXPECT_EQ(result.status, igi::VisualIntegrityStatus::kPass);
    EXPECT_TRUE(result.findings.empty());
}

TEST(VisualIntegrityTest, IgnoresOtherObjectPixelsWhenTargetPartIsMissing) {
    auto view = MakeView({1, 1, 1, 1, 1, 1, 1, 1,
                          2, 2, 2, 2, 2, 2, 2, 2});
    view.targetMask = {1, 1, 1, 1, 1, 1, 1, 1,
                       0, 0, 0, 0, 0, 0, 0, 0};

    const auto result = igi::EvaluateVisualIntegrity(MakeInput({1, 2}, {view}));

    EXPECT_EQ(result.status, igi::VisualIntegrityStatus::kFail);
    EXPECT_TRUE(HasRule(result, "part-coverage"));
}

TEST(VisualIntegrityTest, RequiresProjectedPartsToAgreeWithRenderedScene) {
    auto view = MakeView({1, 1, 1, 1, 1, 1, 1, 1,
                          2, 2, 2, 2, 2, 2, 2, 2});
    // Part 2 exists in the target-only geometry projection but never agrees
    // with the visible scene. It must not be counted as renderer coverage.
    for (size_t index = 8; index < view.targetMask.size(); ++index)
        view.targetMask[index] = 0;

    const auto result = igi::EvaluateVisualIntegrity(MakeInput({1, 2}, {view}));

    EXPECT_EQ(result.status, igi::VisualIntegrityStatus::kFail);
    EXPECT_TRUE(HasRule(result, "part-coverage"));
}

TEST(VisualIntegrityTest, FailsWhenTargetDepthDisagreesWithVisibleSceneDepth) {
    auto view = MakeView({1, 1, 1, 1, 1, 1, 1, 1,
                          1, 1, 1, 1, 1, 1, 1, 1});
    view.targetDepth[0] = 0.8f;

    const auto result = igi::EvaluateVisualIntegrity(MakeInput({1}, {view}));

    EXPECT_EQ(result.status, igi::VisualIntegrityStatus::kFail);
    EXPECT_TRUE(HasRule(result, "depth-consistency"));
}

TEST(VisualIntegrityTest, FailsWhenTargetMaskContainsLargeInteriorHole) {
    auto view = MakeView({1, 1, 1, 1, 1, 1, 1, 1,
                          1, 1, 1, 1, 1, 1, 1, 1}, 5, 5);
    view.targetMask.assign(25, 1);
    view.partIds.assign(25, 1);
    view.sceneDepth.assign(25, 0.5f);
    view.targetDepth.assign(25, 0.5f);
    view.targetMask[12] = 0;
    view.partIds[12] = 0;

    auto input = MakeInput({1}, {view});
    input.minimumInteriorHolePixels = 1;
    const auto result = igi::EvaluateVisualIntegrity(input);

    EXPECT_EQ(result.status, igi::VisualIntegrityStatus::kFail);
    EXPECT_TRUE(HasRule(result, "silhouette-hole"));
}
