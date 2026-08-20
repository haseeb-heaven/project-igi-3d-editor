// test_runtime_subsystems.cpp - Unit and integration tests for C++ Game Mode Runtime Subsystems
#include <gtest/gtest.h>
#include <cmath>
#include <memory>
#include <string>
#include <vector>
#include "../source/game_clock.h"
#include "../source/level/qvm_interpreter.h"
#include "../source/level/qvm_native_registry.h"
#include "../source/level/task_tree.h"
#include "../source/player_controller.h"
#include "../source/player_collision.h"
#include "../source/weapon_system.h"
#include "../source/ai_system.h"
#include "../source/level_flow.h"
#include "../source/runtime/runtime_world.h"
#include "../source/runtime/editor_snapshot.h"
#include "../source/runtime/gameplay_host.h"

using namespace igi;

namespace {

class RecordingTask final : public GameTask {
public:
    RecordingTask(uint32_t task_id, std::vector<std::string>& events)
        : GameTask(task_id, 0x200, "RecordingTask"), events_(events) {}

    void OnCreate() override { events_.push_back("create:" + std::to_string(GetId())); }
    void OnDestroy() override { events_.push_back("destroy:" + std::to_string(GetId())); }
    void OnMessage(const RuntimeTaskMessage& message) override {
        if (message.message_id == 42) {
            events_.push_back("message:" + std::to_string(GetId()));
        }
    }

private:
    std::vector<std::string>& events_;
};

float FlatTerrain(float, float) {
    return 0.0f;
}

} // namespace

// 1. Game Clock Determinism & Tick Tests
TEST(RuntimeClockTest, DeterministicTicksAndCatchUp) {
    GameClock clock;
    EXPECT_EQ(clock.GetTickCount(), 0);
    EXPECT_FALSE(clock.IsTickDue());

    // Establish time base, then advance by 100ms (~3 ticks)
    clock.Update(0);
    clock.Update(100);
    EXPECT_TRUE(clock.IsTickDue());

    int tick_count = 0;
    while (clock.IsTickDue()) {
        clock.CompleteTick();
        tick_count++;
        if (clock.GetTickCount() < GameClock::GUARDED_STARTUP_TICKS) {
            clock.CompleteRender();
        }
    }
    EXPECT_GE(tick_count, 3);
    EXPECT_EQ(clock.GetTickCount(), static_cast<uint64_t>(tick_count));

    // Test pause
    clock.SetPaused(true);
    clock.Update(300);
    EXPECT_FALSE(clock.IsTickDue());
}

TEST(RuntimeClockTest, UsesAbsoluteDeadlinesInsteadOfRoundedAccumulator) {
    GameClock clock;
    clock.Reset(1000);
    clock.Update(1000);
    clock.Update(1034);

    ASSERT_TRUE(clock.IsTickDue());
    EXPECT_EQ(clock.GetDueMilliseconds(), 1000);
    clock.CompleteTick();
    clock.CompleteRender();

    clock.Update(1067);
    EXPECT_TRUE(clock.IsTickDue());
    EXPECT_EQ(clock.GetDueMilliseconds(), 1033);
}

TEST(RuntimeClockTest, BoundsCatchUpBurstAndExcludesNestedWallTime) {
    GameClock clock;
    clock.Reset(0);
    clock.Update(0);
    clock.Update(1000);

    // The first three ticks are render-guarded in the reference loop.
    for (int startup_tick = 0; startup_tick < GameClock::GUARDED_STARTUP_TICKS; ++startup_tick) {
        ASSERT_TRUE(clock.IsTickDue());
        clock.CompleteTick();
        clock.CompleteRender();
    }

    int completed_ticks = 0;
    while (clock.IsTickDue()) {
        clock.CompleteTick();
        ++completed_ticks;
    }

    EXPECT_EQ(completed_ticks, 11);
    EXPECT_TRUE(clock.IsCatchUpCapped());

    clock.Reset(100);
    clock.Update(100);
    clock.BeginExcludedTime(100);
    clock.BeginExcludedTime(200);
    clock.EndExcludedTime(300);
    clock.EndExcludedTime(400);
    clock.Update(400);

    EXPECT_EQ(clock.GetExcludedMilliseconds(), 300);
    EXPECT_FALSE(clock.IsTickDue());
    clock.Update(401);
    EXPECT_TRUE(clock.IsTickDue());
}

// 2. QVM Bytecode Execution & Native Registry Tests
TEST(RuntimeQvmTest, BytecodeExecutionAndStack) {
    QvmNativeRegistry registry;
    registry.RegisterFunction(0x100, "AddTen", [](QvmExecutionContext& ctx, const std::vector<QvmRuntimeValue>& args) {
        int32_t val = args.empty() ? 0 : args[0].int_val;
        return QvmRuntimeValue::FromInt(val + 10);
    });

    QvmProgram program;
    // Bytecode: PUSH_INT 25, CALL_NATIVE 0x100 (1 arg), RETURN
    program.instructions.push_back({0x01, 0, 25, 0.0, ""});
    program.instructions.push_back({0x30, 0x100, 1, 0.0, ""});
    program.instructions.push_back({0xFF, 0, 0, 0.0, ""});

    QvmInterpreter interpreter(registry);
    auto ctx = interpreter.CreateContext(program);
    ASSERT_NE(ctx, nullptr);

    bool ok = ctx->Run();
    EXPECT_TRUE(ok);
    EXPECT_FALSE(ctx->HasErrored());
    EXPECT_EQ(ctx->StackSize(), 1);
    EXPECT_EQ(ctx->Pop().int_val, 35);
}

// 3. Task Tree & Messaging Tests
TEST(RuntimeTaskTreeTest, LifecycleAndMessaging) {
    TaskTree tree;
    auto root = std::make_shared<ContainerTask>(1, 0x181, "RootContainer");
    auto child = std::make_shared<GameTask>(2, 0x200, "PlayerTask");
    root->AppendChild(child);
    tree.SetRoot(root);
    tree.RegisterTask(child);

    EXPECT_NE(tree.FindTask(2), nullptr);
    EXPECT_EQ(tree.FindTask(2)->GetName(), "PlayerTask");

    RuntimeTaskMessage msg;
    msg.message_id = 42;
    tree.QueueMessage(msg);
    tree.Update(1.0 / 30.0);

    EXPECT_TRUE(child->IsActive());
}

TEST(RuntimeTaskTreeTest, RejectsDuplicateOwnershipAndDestroysChildrenBeforeParents) {
    std::vector<std::string> lifecycle_events;
    auto root = std::make_shared<RecordingTask>(1, lifecycle_events);
    auto child = std::make_shared<RecordingTask>(2, lifecycle_events);
    auto duplicate_id = std::make_shared<RecordingTask>(2, lifecycle_events);

    EXPECT_TRUE(root->AppendChild(child));
    EXPECT_FALSE(root->AppendChild(child));
    EXPECT_FALSE(root->AppendChild(duplicate_id));
    EXPECT_FALSE(child->AppendChild(root));

    TaskTree tree;
    ASSERT_TRUE(tree.SetRoot(root));
    tree.RegisterTask(child);
    ASSERT_EQ(lifecycle_events, (std::vector<std::string>{"create:1", "create:2"}));

    RuntimeTaskMessage message;
    message.message_id = 42;
    message.target_id = child->GetId();
    tree.QueueMessage(message);
    tree.Update(1.0 / 30.0);
    EXPECT_EQ(lifecycle_events.back(), "message:2");

    child->MarkForDestruction();
    tree.Update(1.0 / 30.0);
    ASSERT_GE(lifecycle_events.size(), 4U);
    EXPECT_EQ(lifecycle_events[lifecycle_events.size() - 1], "destroy:2");

    tree.Clear();
    EXPECT_EQ(lifecycle_events.back(), "destroy:1");
}

// 4. Player Locomotion, Physics & Obstacle Collision Integration Tests
TEST(RuntimePlayerTest, GravityAndJumpIntegration) {
    PlayerController player;
    player.Reset(glm::vec3(0.0f, 0.0f, 10000.0f));

    auto dummy_terrain = [](float x, float y) -> float { return 0.0f; };

    // Initially in air (Z = 100, terrain = 0)
    PlayerInputCmd cmd;
    player.Tick(cmd, dummy_terrain);
    EXPECT_FALSE(player.IsGrounded());
    EXPECT_LT(player.GetVelocity().z, 0.0f); // Falling

    // Place on ground
    player.Reset(glm::vec3(0.0f, 0.0f, 0.0f));
    player.Tick(cmd, dummy_terrain);
    EXPECT_TRUE(player.IsGrounded());

    // Jump trigger
    cmd.jump = true;
    player.Tick(cmd, dummy_terrain);
    EXPECT_FALSE(player.IsGrounded());
    EXPECT_GT(player.GetVelocity().z, 0.0f);

    // Obstacle collision test
    player.Reset(glm::vec3(100.0f, 100.0f, 0.0f));
    std::vector<ObstacleCollider> obstacles;
    ObstacleCollider enemy;
    enemy.center = glm::vec3(100.0f, 100.0f, 0.0f);
    enemy.radius = 1638.4f;
    enemy.height = 7372.8f;
    obstacles.push_back(enemy);

    PlayerCollision collision;
    glm::vec3 test_pos(100.0f, 100.0f, 0.0f);
    collision.ResolveObstacles(test_pos, obstacles, 1638.4f);
    // Should resolve without NaN/Inf
    EXPECT_FALSE(std::isnan(test_pos.x) || std::isnan(test_pos.y));
}

TEST(RuntimeCollisionTest, GroundProbeHonorsStepBudgetAndSlopeNormal) {
    PlayerCollision collision;
    auto sloped_terrain = [](float x, float) -> float { return x * 0.25f; };

    PlayerGroundQuery query = collision.QueryGround(
        glm::vec3(0.0f, 0.0f, 1024.0f),
        PlayerController::STANDING_EYE_HEIGHT,
        sloped_terrain,
        true,
        false);

    EXPECT_TRUE(query.is_grounded);
    EXPECT_FLOAT_EQ(query.ground_height, 0.0f);
    EXPECT_EQ(query.step_down_budget, 2048.0f);
    EXPECT_GT(query.surface_normal.z, 0.9f);
    EXPECT_LT(query.surface_normal.x, 0.0f);
}

TEST(RuntimeCollisionTest, WallSweepStopsAtSolidGeometryAndSlides) {
    PlayerCollision collision;
    collision.SetSolidQuery([](const glm::vec3& sample_position) {
        return sample_position.x >= 4096.0f;
    });

    PlayerWallSweepResult result = collision.SweepWalls(
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(8192.0f, 8192.0f, 0.0f),
        512.0f,
        PlayerController::STANDING_EYE_HEIGHT);

    EXPECT_TRUE(result.hit_wall);
    EXPECT_LT(result.hit_fraction, 1.0f);
    EXPECT_LT(result.slide_velocity.x, 4096.0f);
    EXPECT_GT(result.slide_velocity.y, 7000.0f);
    EXPECT_LT(result.wall_normal.x, -0.9f);
}

TEST(RuntimeCollisionTest, CeilingQueryPreventsStandingInLowClearance) {
    PlayerCollision collision;
    collision.SetCeilingQuery([](const glm::vec3&) {
        return 6000.0f;
    });

    EXPECT_FALSE(collision.CanStandUp(
        glm::vec3(0.0f, 0.0f, 0.0f),
        PlayerController::STANDING_EYE_HEIGHT,
        FlatTerrain));
    EXPECT_TRUE(collision.CanStandUp(
        glm::vec3(0.0f, 0.0f, 0.0f),
        PlayerController::CROUCHING_EYE_HEIGHT,
        FlatTerrain));
}

TEST(RuntimePlayerTest, ControllerUsesCollisionBoundaryForFixedStepMovement) {
    PlayerController player;
    player.Reset(glm::vec3(0.0f, 0.0f, 0.0f));
    player.SetCollisionQuery([](const glm::vec3& sample_position) {
        return sample_position.x >= 100.0f;
    });

    PlayerInputCmd input;
    input.strafe = 1.0f;
    player.Tick(input, FlatTerrain);

    EXPECT_LT(player.GetPosition().x, 100.0f);
    EXPECT_TRUE(player.IsGrounded());
}

// 5. Weapon Fire & Ballistics Tests
TEST(RuntimeWeaponTest, FireAndRecoilCooldown) {
    WeaponSystem weapons;
    weapons.SelectWeapon(0);

    BulletTrace trace;
    bool fired = weapons.TryFire(glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f), trace);
    EXPECT_TRUE(fired);
    EXPECT_TRUE(trace.hit);
    EXPECT_GT(trace.distance, PlayerController::WORLD_METER);
    EXPECT_EQ(weapons.GetCurrentClipAmmo(), 29);

    // Rapid second shot must be throttled by RPM cooldown
    bool fired_immediately = weapons.TryFire(glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f), trace);
    EXPECT_FALSE(fired_immediately);

    // Advance cooldown
    weapons.Update(0.2);
    bool fired_after_cooldown = weapons.TryFire(glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f), trace);
    EXPECT_TRUE(fired_after_cooldown);
    EXPECT_EQ(weapons.GetCurrentClipAmmo(), 28);
}

// 6. AI Perception Dual-Cone Tests
TEST(RuntimeAiTest, VisionConeDirectAndPeripheral) {
    AiSystem ai;
    AiGuardEntity guard;
    guard.id = 1;
    guard.position = glm::vec3(0.0f, 0.0f, 0.0f);
    guard.yaw = 0.0f; // Facing +Y
    ai.RegisterGuard(guard);

    // Unit scale: 1 world meter = 4096 units (PlayerController::WORLD_METER).
    const float kMeters = 4096.0f;
    const float kEyeHeight = 1.7f * kMeters; // eye sits ~1.7m above the feet

    // Target directly in front (30m ahead at eye level) -> Primary cone (0x101)
    AiVisionResult res1 = ai.CheckVision(guard, glm::vec3(0.0f, 30.0f * kMeters, kEyeHeight), false);
    EXPECT_EQ(res1, AiVisionResult::Primary);

    // Target behind guard -> None
    AiVisionResult res2 = ai.CheckVision(guard, glm::vec3(0.0f, -20.0f * kMeters, kEyeHeight), false);
    EXPECT_EQ(res2, AiVisionResult::None);

    // Target in wide peripheral angle (approx 60 degrees, past the 45-degree
    // primary yaw cone) -> Peripheral
    AiVisionResult res3 = ai.CheckVision(guard, glm::vec3(17.32f * kMeters, 10.0f * kMeters, kEyeHeight), false);
    EXPECT_EQ(res3, AiVisionResult::Peripheral);
}

TEST(RuntimeAiTest, PatrolFallbackMovesGuardsWithoutScriptData) {
    AiSystem ai;
    AiGuardEntity guard;
    guard.id = 7;
    guard.state = AiGuardState::Patrol;
    guard.position = glm::vec3(0.0f, 0.0f, 0.0f);
    guard.waypoints = {
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 8192.0f, 0.0f),
    };
    ai.RegisterGuard(guard);

    ai.Update(GameClock::TICK_INTERVAL_SECONDS, glm::vec3(100000.0f), false);
    ai.Update(GameClock::TICK_INTERVAL_SECONDS, glm::vec3(100000.0f), false);

    ASSERT_EQ(ai.GetGuards().size(), 1U);
    EXPECT_GT(ai.GetGuards()[0].position.y, 0.0f);
}

// 7. Mission Flow & Objective Tests
TEST(RuntimeLevelFlowTest, ObjectiveSuccessFlow) {
    LevelFlow flow;
    flow.InitializeMission(1);
    EXPECT_EQ(flow.GetStatus(), MissionStatus::InProgress);

    // Player dies -> Mission failed
    flow.Update(false, false);
    EXPECT_EQ(flow.GetStatus(), MissionStatus::Failed);

    // Reinitialize and complete objective
    flow.InitializeMission(1);
    flow.SetObjectiveState(1, ObjectiveState::Completed);
    flow.Update(true, true); // Player alive in extraction
    EXPECT_EQ(flow.GetStatus(), MissionStatus::Success);
}

TEST(RuntimeWorldTest, PlayerFireDamagesGuardUsingWorldUnits) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain);
    world.GetPlayer().Reset(glm::vec3(0.0f, 0.0f, 0.0f));
    world.GetPlayer().SetOrientation(0.0f, 0.0f);

    AiGuardEntity guard;
    guard.id = 17;
    guard.position = glm::vec3(0.0f, 4096.0f, 0.0f);
    guard.yaw = 180.0f;
    guard.health = 100.0f;
    world.GetAi().RegisterGuard(guard);

    PlayerInputCmd input;
    input.fire = true;
    world.UpdateSimulationTick(0, input);

    ASSERT_EQ(world.GetAi().GetGuards().size(), 1U);
    EXPECT_LT(world.GetAi().GetGuards()[0].health, 100.0f);
}

TEST(RuntimeWorldTest, CombatGuardDamagesPlayerAtFixedCadence) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain);
    world.GetPlayer().ApplyTuning(100.0f, 0.0f);
    world.GetPlayer().Reset(glm::vec3(0.0f, 0.0f, 0.0f));

    AiGuardEntity guard;
    guard.id = 23;
    guard.position = glm::vec3(0.0f, 4096.0f, 0.0f);
    guard.yaw = 180.0f;
    guard.state = AiGuardState::Combat;
    guard.health = 100.0f;
    world.GetAi().RegisterGuard(guard);

    const float initial_health = world.GetPlayer().GetHealth();
    PlayerInputCmd input;
    world.UpdateSimulationTick(30, input);

    EXPECT_LT(world.GetPlayer().GetHealth(), initial_health);
}

// 8. Twin-Window & Editor Snapshot Tests
TEST(RuntimeHostTest, ModeSwitchingAndSnapshotRestore) {
    GameplayHost host;
    auto dummy_terrain = [](float x, float y) -> float { return 0.0f; };
    host.Initialize(dummy_terrain);

    EditorSnapshot snap;
    snap.camera_pos = glm::vec3(123.0f, 456.0f, 789.0f);
    snap.camera_yaw = 45.0f;
    snap.was_noclip_mode = false;
    snap.was_hud_visible = false;

    EXPECT_FALSE(host.IsGameplayActive());
    bool opened = host.OpenGameplay(snap);
    EXPECT_TRUE(opened);
    EXPECT_TRUE(host.IsGameplayActive());

    // Update simulation
    host.Update(100);

    EditorSnapshot restored;
    bool closed = host.CloseGameplay(restored);
    EXPECT_TRUE(closed);
    EXPECT_FALSE(host.IsGameplayActive());
    EXPECT_FLOAT_EQ(restored.camera_pos.x, 123.0f);
    EXPECT_FLOAT_EQ(restored.camera_yaw, 45.0f);
    EXPECT_FALSE(restored.was_noclip_mode);
    EXPECT_FALSE(restored.was_hud_visible);
}
