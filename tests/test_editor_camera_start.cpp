#include <gtest/gtest.h>

#include "../source/runtime/editor_camera_start.h"
#include "../source/runtime/graph_camera_target.h"
#include "../source/capture_camera.h"
#include "../source/renderer/renderer_objects_internal.h"

namespace {

TEST(EditorCameraStartTest, RejectsPersistedCameraOnFreshAndSwitchedLevelLoads) {
    const glm::vec3 stale_camera(127000280.0f, -37829456.0f, 175303728.0f);

    EXPECT_FALSE(igi::ShouldUseSavedEditorCamera(-1, 12, stale_camera));
    EXPECT_FALSE(igi::ShouldUseSavedEditorCamera(9, 12, stale_camera));
}

TEST(EditorCameraStartTest, ReusesPersistedCameraOnSameLevelReload) {
    const glm::vec3 saved_camera(112255288.0f, -42206448.0f, 179544736.0f);

    EXPECT_TRUE(igi::ShouldUseSavedEditorCamera(12, 12, saved_camera));
    EXPECT_FALSE(igi::ShouldUseSavedEditorCamera(12, 12, glm::vec3(0.0f)));
}

TEST(EditorCameraStartTest, ResetLevelForcesAuthoredStartCamera) {
    // When a level reset is triggered, last_loaded_level is reset to -1
    // and saved camera is zeroed, ensuring ShouldUseSavedEditorCamera evaluates false
    const glm::vec3 zeroed_camera(0.0f);
    EXPECT_FALSE(igi::ShouldUseSavedEditorCamera(-1, 9, zeroed_camera));

    const glm::vec3 stale_camera(127000280.0f, -37829456.0f, 175303728.0f);
    EXPECT_FALSE(igi::ShouldUseSavedEditorCamera(-1, 9, stale_camera));
}

TEST(EditorCameraStartTest, FocusesOutsideModelBounds) {
    const auto pose = igi::FocusCameraOnModel(glm::vec3(10.0f, 20.0f, 30.0f), 1000.0f);

    EXPECT_NE(pose.position.y, 20.0f);
    EXPECT_LT(pose.position.y, 20.0f);
    EXPECT_LT(pose.pitch, 0.0f);
}

TEST(EditorCameraStartTest, KeepsBuildingAttachmentsVisibleInsideModelBounds) {
    // Level 12's authored start lies within FortressWinchHouse's transformed
    // bounds but beyond its short portal distance. The normal viewport must
    // retain the structural attachments in that valid interior view.
    EXPECT_TRUE(igi::ShouldDrawBuildingAttachments(
        31000.0f, 199877.0f, 25600.0f, true));
}

TEST(EditorCameraStartTest, FortressWinchHouseCutoffDistanceKeepsAttachmentsVisibleAtStart) {
    // In Level 12, FortressWinchHouse (463_01_1) has LOD cutoff = 600m (2,457,600 units).
    // The player / start camera is at distance ~98,000 units (~24m) from the building.
    // The attachment culling must NOT cull the winch machinery, cables, or glass rooms.
    const float distToCamera = 98000.0f;
    const float boundRadius = 34693.0f;
    const float cutoffDistance = 600.0f * 4096.0f; // 600m in world units

    EXPECT_TRUE(igi::ShouldDrawBuildingAttachments(
        distToCamera, boundRadius, cutoffDistance, true));

    // Beyond cutoff distance (e.g. 700m), attachments should be culled
    const float farDistance = 700.0f * 4096.0f;
    EXPECT_FALSE(igi::ShouldDrawBuildingAttachments(
        farDistance, boundRadius, cutoffDistance, true));
}

TEST(EditorCameraStartTest, F11CameraSnapToObjectFramesOutsideModelBounds) {
    const glm::dvec3 objPos(112284896.0, -42113812.0, 179532464.0);
    const float boundRadius = 34693.0f;
    const glm::dvec3 viewerForward(0.0, 1.0, 0.0);

    // Standard F11 framing: distance is at least 1.6 * boundRadius
    const GraphCameraPose poseNormal = MakeF11ObjectCameraPose(
        objPos, boundRadius, viewerForward, /*shiftHeld=*/false);

    const double distNormal = glm::distance(poseNormal.position, objPos);
    EXPECT_GE(distNormal, static_cast<double>(boundRadius * 1.6f));
    EXPECT_NE(poseNormal.position, objPos);

    // Shift+F11 framing: distance is at least 3.0 * boundRadius
    const GraphCameraPose poseShift = MakeF11ObjectCameraPose(
        objPos, boundRadius, viewerForward, /*shiftHeld=*/true);

    const double distShift = glm::distance(poseShift.position, objPos);
    EXPECT_GE(distShift, static_cast<double>(boundRadius * 3.0f));
    EXPECT_GT(distShift, distNormal);
}

TEST(EditorCameraStartTest, IdentifiesZyxEulerModelsCorrectly) {
    // 506_ slide up doors use ZYX Euler rotation order
    EXPECT_TRUE(IsZyxEulerModel("506_01_1"));
    EXPECT_TRUE(IsZyxEulerModel("506_02_1"));
    // 615_01_1 missile on rack/carriage in Level 9/13 uses ZYX Euler rotation order
    EXPECT_TRUE(IsZyxEulerModel("615_01_1"));

    // Standard objects use default ZXY Euler rotation order
    EXPECT_FALSE(IsZyxEulerModel("615_10_1")); // carriage
    EXPECT_FALSE(IsZyxEulerModel("205_01_1")); // desk
    EXPECT_FALSE(IsZyxEulerModel(""));
}

#include "../source/runtime/editor_cursor_mode.h"

TEST(EditorCameraStartTest, ResolveCursorModeForTerrainTools) {
    using igi::CursorMode;
    using igi::ResolveCursorMode;

    // Terrain edit brushes: 0=Raise/Lift, 1=Lower, 2=Soften, 3=Flatten, other=Drop
    EXPECT_EQ(ResolveCursorMode(false, false, true, 0, -1, -1), CursorMode::TerrainLift);
    EXPECT_EQ(ResolveCursorMode(false, false, true, 1, -1, -1), CursorMode::TerrainLower);
    EXPECT_EQ(ResolveCursorMode(false, false, true, 2, -1, -1), CursorMode::TerrainSoften);
    EXPECT_EQ(ResolveCursorMode(false, false, true, 3, -1, -1), CursorMode::TerrainFlatten);
    EXPECT_EQ(ResolveCursorMode(false, false, true, 4, -1, -1), CursorMode::TerrainDrop);

    // Normal mode: Default (pointer), Hover, Selected
    EXPECT_EQ(ResolveCursorMode(false, false, false, 0, -1, -1), CursorMode::Default);
    EXPECT_EQ(ResolveCursorMode(false, false, false, 0, -1, 5), CursorMode::Hover);
    EXPECT_EQ(ResolveCursorMode(false, false, false, 0, 2, -1), CursorMode::Selected);
    EXPECT_EQ(ResolveCursorMode(false, false, false, 0, 2, 5), CursorMode::Selected);

    // Camera mode overrides all: Camera on LMB down, Move on movement
    EXPECT_EQ(ResolveCursorMode(true, true, true, 0, -1, -1), CursorMode::Camera);
    EXPECT_EQ(ResolveCursorMode(true, false, true, 0, -1, -1), CursorMode::Move);
    EXPECT_EQ(ResolveCursorMode(true, true, false, 0, -1, -1), CursorMode::Camera);
    EXPECT_EQ(ResolveCursorMode(true, false, false, 0, -1, -1), CursorMode::Move);
}

} // namespace
