// test_runtime_subsystems.cpp - Unit and integration tests for C++ Game Mode Runtime Subsystems
#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>
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
#include "../source/runtime/projectile_system.h"
#include "../source/runtime/window_input_router.h"
#include "../source/runtime/human_player_config.h"

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

bool WallAtOneMeter(float, float y, float) {
    return y >= PlayerController::WORLD_METER;
}

bool LowRoofAtStandingHeight(float, float, float z) {
    return z >= 6000.0f;
}

QVMFile BuildRetailAiPatrolScript() {
    QVMFile parsed_file;
    parsed_file.valid = true;
    parsed_file.identifiers = {
        "AIFunction_GetCurrentEventType",
        "AIEVENT_IDLE",
        "AIAction_Patrol",
        "AIACTIONFLAG_PUSHABLE",
    };

    auto make_instruction = [](QVMOpType type, uint32_t address, uint32_t size) {
        QVMInstruction instruction{};
        instruction.type = type;
        instruction.address = address;
        instruction.size = size;
        return instruction;
    };

    QVMInstruction get_event = make_instruction(QVMOpType::PUSHIIB, 0, 2);
    get_event.operand = 0;
    QVMInstruction get_event_call = make_instruction(QVMOpType::CALL, 2, 5);
    QVMInstruction skip_get_event_arguments = make_instruction(QVMOpType::BRA, 7, 5);

    QVMInstruction idle_event = make_instruction(QVMOpType::PUSHIIB, 12, 2);
    idle_event.operand = 1;
    QVMInstruction compare_event = make_instruction(QVMOpType::EQ, 14, 1);
    QVMInstruction skip_non_idle = make_instruction(QVMOpType::BF, 15, 5);
    skip_non_idle.operand = 33;
    skip_non_idle.signed_operand = 33;

    QVMInstruction patrol_action = make_instruction(QVMOpType::PUSHIIB, 20, 2);
    patrol_action.operand = 2;
    QVMInstruction patrol_call = make_instruction(QVMOpType::CALL, 22, 17);
    patrol_call.operand = 3;
    patrol_call.signed_operand = 3;
    patrol_call.call_targets = {44, 48, 50};
    QVMInstruction skip_patrol_arguments = make_instruction(QVMOpType::BRA, 39, 5);
    skip_patrol_arguments.operand = 9;
    skip_patrol_arguments.signed_operand = 9;

    QVMInstruction patrol_path = make_instruction(QVMOpType::PUSHW, 44, 3);
    patrol_path.operand = 700;
    patrol_path.signed_operand = 700;
    QVMInstruction patrol_path_end = make_instruction(QVMOpType::BRK, 47, 1);
    QVMInstruction patrol_start_index = make_instruction(QVMOpType::PUSH0, 48, 1);
    QVMInstruction patrol_start_index_end = make_instruction(QVMOpType::BRK, 49, 1);
    QVMInstruction patrol_flags = make_instruction(QVMOpType::PUSHIIB, 50, 2);
    patrol_flags.operand = 3;
    QVMInstruction patrol_flags_end = make_instruction(QVMOpType::BRK, 52, 1);
    QVMInstruction program_end = make_instruction(QVMOpType::BRK, 53, 1);

    parsed_file.instructions = {
        get_event,
        get_event_call,
        skip_get_event_arguments,
        idle_event,
        compare_event,
        skip_non_idle,
        patrol_action,
        patrol_call,
        skip_patrol_arguments,
        patrol_path,
        patrol_path_end,
        patrol_start_index,
        patrol_start_index_end,
        patrol_flags,
        patrol_flags_end,
        program_end,
    };
    return parsed_file;
}

} // namespace

TEST(RuntimeProjectileTest, ReferenceGrenadeBouncesAndDetonatesAtFuseExpiry) {
    ProjectileSystem projectiles;
    projectiles.SetCollisionQuery(
        [](const glm::vec3& start, const glm::vec3& end, ProjectileCollisionHit& hit) {
            if (start.z > 0.0f && end.z <= 0.0f) {
                hit.position = glm::vec3(end.x, end.y, 0.0f);
                hit.normal = glm::vec3(0.0f, 0.0f, 1.0f);
                return true;
            }
            return false;
        });

    ProjectileLaunch launch;
    launch.position = glm::vec3(0.0f, 0.0f, 100.0f);
    launch.velocity = glm::vec3(0.0f, 0.0f, -100.0f);
    launch.type = ProjectileType::FragGrenade;
    launch.fuse_ticks = 2;
    launch.damage = 100.0f;
    ASSERT_TRUE(projectiles.Spawn(launch));

    projectiles.Tick();
    ASSERT_EQ(projectiles.GetProjectiles().size(), 1U);
    EXPECT_GT(projectiles.GetProjectiles()[0].position.z, 0.0f);
    EXPECT_TRUE(projectiles.GetDetonations().empty());

    projectiles.Tick();
    EXPECT_TRUE(projectiles.GetProjectiles().empty());
    ASSERT_EQ(projectiles.GetDetonations().size(), 1U);
    EXPECT_EQ(projectiles.GetDetonations()[0].type, ProjectileType::FragGrenade);
}

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

TEST(RuntimeQvmTest, RejectsMalformedBytecodeAndUnsafeControlFlow) {
    QvmNativeRegistry registry;
    QvmInterpreter interpreter(registry);
    QvmProgram program;

    EXPECT_FALSE(interpreter.LoadProgram(
        std::vector<uint8_t>{0x01, 0x02},
        program));
    EXPECT_NE(interpreter.GetLastError().find("truncated"), std::string::npos);

    QvmProgram invalid_jump_program;
    invalid_jump_program.instructions.push_back({0x20, 99, 0, 0.0, ""});
    auto invalid_jump_context = interpreter.CreateContext(invalid_jump_program);
    EXPECT_FALSE(invalid_jump_context->Run());
    EXPECT_TRUE(invalid_jump_context->HasErrored());
}

TEST(RuntimeQvmTest, FailsOnStackUnderflowAndPreservesNativeArgumentOrder) {
    QvmNativeRegistry registry;
    QvmInterpreter interpreter(registry);
    registry.RegisterFunction(0x101, "EncodePair", [](QvmExecutionContext&, const std::vector<QvmRuntimeValue>& args) {
        if (args.size() != 2U) {
            return QvmRuntimeValue::FromInt(-1);
        }
        return QvmRuntimeValue::FromInt(args[0].int_val * 10 + args[1].int_val);
    });

    QvmProgram underflow_program;
    underflow_program.instructions.push_back({0x10, 0, 0, 0.0, ""});
    auto underflow_context = interpreter.CreateContext(underflow_program);
    EXPECT_FALSE(underflow_context->Run());
    EXPECT_TRUE(underflow_context->HasErrored());

    QvmProgram native_program;
    native_program.instructions.push_back({0x01, 0, 1, 0.0, ""});
    native_program.instructions.push_back({0x01, 0, 2, 0.0, ""});
    native_program.instructions.push_back({0x30, 0x101, 2, 0.0, ""});
    native_program.instructions.push_back({0xFF, 0, 0, 0.0, ""});
    auto native_context = interpreter.CreateContext(native_program);
    ASSERT_TRUE(native_context->Run());
    ASSERT_EQ(native_context->StackSize(), 1U);
    EXPECT_EQ(native_context->Pop().int_val, 12);
}

TEST(RuntimeQvmTest, StopsDeterministicallyAtInstructionBudgetAndCanReset) {
    QvmNativeRegistry registry;
    QvmProgram looping_program;
    looping_program.instructions.push_back({0x20, 0, 0, 0.0, ""});

    QvmInterpreter interpreter(registry);
    auto context = interpreter.CreateContext(looping_program);
    EXPECT_FALSE(context->Run());
    EXPECT_TRUE(context->HasErrored());
    EXPECT_EQ(context->GetStepCount(), QvmExecutionContext::MAX_INSTRUCTION_STEPS);

    context->Reset();
    EXPECT_FALSE(context->HasErrored());
    EXPECT_EQ(context->GetStepCount(), 0U);
}

TEST(RuntimeQvmTest, ExecutesRetailLoopArithmeticAndDeferredNativeArguments) {
    QvmNativeRegistry registry;
    int native_evaluation_count = 0;
    registry.RegisterDeferredFunctionByName(
        "RecordValue",
        [&native_evaluation_count](QvmExecutionContext&, const QvmNativeCallArguments& arguments) {
            ++native_evaluation_count;
            return QvmRuntimeValue::FromInt(arguments.GetInt(0) + 1);
        });

    QVMFile parsed_file;
    parsed_file.valid = true;
    parsed_file.identifiers = {"RecordValue"};

    QVMInstruction push_symbol{};
    push_symbol.type = QVMOpType::PUSHIIB;
    push_symbol.operand = 0;
    push_symbol.address = 0;
    push_symbol.size = 2;

    QVMInstruction call{};
    call.type = QVMOpType::CALL;
    call.operand = 1;
    call.signed_operand = 1;
    call.call_targets = {16};
    call.address = 2;
    call.size = 9;

    QVMInstruction skip_arguments{};
    skip_arguments.type = QVMOpType::BRA;
    skip_arguments.operand = 5;
    skip_arguments.signed_operand = 5;
    skip_arguments.address = 11;
    skip_arguments.size = 5;

    QVMInstruction argument_value{};
    argument_value.type = QVMOpType::PUSHB;
    argument_value.operand = 7;
    argument_value.signed_operand = 7;
    argument_value.address = 16;
    argument_value.size = 2;

    QVMInstruction argument_one{};
    argument_one.type = QVMOpType::PUSH1;
    argument_one.address = 18;
    argument_one.size = 1;

    QVMInstruction argument_add{};
    argument_add.type = QVMOpType::ADD;
    argument_add.address = 19;
    argument_add.size = 1;

    QVMInstruction argument_end{};
    argument_end.type = QVMOpType::BRK;
    argument_end.address = 20;
    argument_end.size = 1;

    QVMInstruction program_end{};
    program_end.type = QVMOpType::BRK;
    program_end.address = 21;
    program_end.size = 1;

    parsed_file.instructions = {
        push_symbol,
        call,
        skip_arguments,
        argument_value,
        argument_one,
        argument_add,
        argument_end,
        program_end,
    };

    QvmInterpreter interpreter(registry);
    QvmProgram program;
    ASSERT_TRUE(interpreter.LoadProgram(parsed_file, program))
        << interpreter.GetLastError();
    ASSERT_TRUE(program.uses_loop_85_instruction_set);

    auto context = interpreter.CreateContext(program);
    ASSERT_TRUE(context->Run()) << context->GetLastError();
    ASSERT_EQ(context->StackSize(), 1U);
    EXPECT_EQ(context->Pop().int_val, 9);
    EXPECT_EQ(native_evaluation_count, 1);
}

TEST(RuntimeQvmTest, ConvertsDeferredNativeExceptionsToDeterministicVmFaults) {
    QvmNativeRegistry registry;
    registry.RegisterDeferredFunctionByName(
        "Explode",
        [](QvmExecutionContext&, const QvmNativeCallArguments&) -> QvmRuntimeValue {
            throw std::runtime_error("native failure");
        });

    QVMFile parsed_file;
    parsed_file.valid = true;
    parsed_file.identifiers = {"Explode"};

    QVMInstruction push_symbol{};
    push_symbol.type = QVMOpType::PUSHIIB;
    push_symbol.operand = 0;
    push_symbol.address = 0;
    push_symbol.size = 2;

    QVMInstruction call{};
    call.type = QVMOpType::CALL;
    call.operand = 0;
    call.signed_operand = 0;
    call.address = 2;
    call.size = 5;

    QVMInstruction program_end{};
    program_end.type = QVMOpType::BRK;
    program_end.address = 7;
    program_end.size = 1;

    parsed_file.instructions = {push_symbol, call, program_end};

    QvmInterpreter interpreter(registry);
    QvmProgram program;
    ASSERT_TRUE(interpreter.LoadProgram(parsed_file, program))
        << interpreter.GetLastError();

    auto context = interpreter.CreateContext(program);
    EXPECT_FALSE(context->Run());
    EXPECT_TRUE(context->HasErrored());
    EXPECT_NE(context->GetLastError().find("native failure"), std::string::npos);
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

TEST(RuntimeCollisionTest, AccumulatesReferenceSlopeSlideOnlyWhileGrounded) {
    const glm::vec3 moderate_slope_normal(-0.83205f, 0.0f, 0.55470f);

    const glm::vec3 slide_velocity = PlayerCollision::AccumulateSlopeSlide(
        glm::vec3(0.0f),
        moderate_slope_normal,
        true);
    EXPECT_LT(slide_velocity.x, 0.0f);
    EXPECT_FLOAT_EQ(slide_velocity.y, 0.0f);
    EXPECT_FLOAT_EQ(slide_velocity.z, 0.0f);

    EXPECT_EQ(
        PlayerCollision::AccumulateSlopeSlide(
            glm::vec3(100.0f, 20.0f, 0.0f),
            moderate_slope_normal,
            false),
        glm::vec3(0.0f));
    EXPECT_EQ(
        PlayerCollision::AccumulateSlopeSlide(
            glm::vec3(100.0f, 20.0f, 0.0f),
            glm::vec3(0.0f, 0.0f, 1.0f),
            true),
        glm::vec3(0.0f));
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

TEST(RuntimePlayerTest, AppliesExplicitMovementTuningToFixedStepController) {
    PlayerController player;
    PlayerController::Tuning tuning;
    tuning.maximum_health = 75.0f;
    tuning.maximum_armor = 25.0f;
    tuning.walk_speed_units_per_tick = 123.0f;
    tuning.jump_speed_units_per_tick = 456.0f;
    tuning.gravity_units_per_tick = 12.0f;
    tuning.standing_eye_height_units = 5000.0f;
    tuning.crouching_eye_height_units = 3000.0f;
    player.ApplyTuning(tuning);
    player.Reset(glm::vec3(0.0f, 0.0f, 0.0f));

    EXPECT_FLOAT_EQ(player.GetMaximumHealth(), 75.0f);
    EXPECT_FLOAT_EQ(player.GetMaximumArmor(), 25.0f);
    EXPECT_FLOAT_EQ(player.GetEyeHeight(), 5000.0f);

    PlayerInputCmd move_command;
    move_command.forward = 1.0f;
    player.Tick(move_command, FlatTerrain);
    EXPECT_FLOAT_EQ(player.GetVelocity().y, 123.0f);

    player.Reset(glm::vec3(0.0f, 0.0f, 0.0f));
    move_command = PlayerInputCmd();
    move_command.jump = true;
    player.Tick(move_command, FlatTerrain);
    EXPECT_FLOAT_EQ(player.GetVelocity().z, 456.0f);
}

TEST(RuntimePlayerTest, ConvertsHumanPlayerMetersToRuntimeUnitsPerTick) {
    HumanPlayerTuning human_player_tuning;
    human_player_tuning.walk_speed = 3.0f;
    human_player_tuning.run_speed = 6.0f;
    human_player_tuning.crouch_speed = 1.5f;
    human_player_tuning.jump_impulse = 777.0f;
    human_player_tuning.gravity = 18.0f;
    human_player_tuning.eye_height_stand = 1.75f;
    human_player_tuning.eye_height_crouch = 0.90f;

    const PlayerController::Tuning controller_tuning =
        human_player_tuning.ToControllerTuning();

    EXPECT_FLOAT_EQ(
        controller_tuning.walk_speed_units_per_tick,
        3.0f * PlayerController::WORLD_METER / 30.0f);
    EXPECT_FLOAT_EQ(
        controller_tuning.run_speed_units_per_tick,
        6.0f * PlayerController::WORLD_METER / 30.0f);
    EXPECT_FLOAT_EQ(controller_tuning.jump_speed_units_per_tick, 777.0f);
    EXPECT_FLOAT_EQ(controller_tuning.gravity_units_per_tick,
                    18.0f * PlayerController::WORLD_METER / (30.0f * 30.0f));
    EXPECT_FLOAT_EQ(controller_tuning.standing_eye_height_units, 1.75f * PlayerController::WORLD_METER);
    EXPECT_FLOAT_EQ(controller_tuning.crouching_eye_height_units, 0.90f * PlayerController::WORLD_METER);
}

// 5. Weapon Fire & Ballistics Tests
TEST(RuntimeWeaponTest, UsesRetailPlayerCycleAndWeaponModels) {
    WeaponSystem weapons;

    EXPECT_EQ(weapons.GetActiveWeapon().id, 20U); // WEAPON_ID_KNIFE
    EXPECT_EQ(weapons.GetActiveWeapon().model_id, "133_01_1");

    ASSERT_TRUE(weapons.SelectWeaponSlot(6)); // WEAPON_ID_MP5SD
    EXPECT_EQ(weapons.GetActiveWeapon().id, 7U);
    EXPECT_EQ(weapons.GetActiveWeapon().script_id, "WEAPON_ID_MP5SD");
    EXPECT_EQ(weapons.GetActiveWeapon().model_id, "103_01_1");
    EXPECT_EQ(weapons.GetActiveWeapon().rounds_per_minute, 700.0f);
    EXPECT_EQ(weapons.GetActiveWeapon().clip_capacity, 32U);
}

TEST(RuntimeWeaponTest, ResolvesAuthoredWeaponIdentifiersWithDecorativeText) {
    WeaponSystem weapons;

    ASSERT_TRUE(weapons.SelectWeaponByScriptId("  \"WEAPON_ID_MP5SD\"  "));
    EXPECT_EQ(weapons.GetActiveWeapon().id, 7U);
}

TEST(RuntimeWeaponTest, ClassifiesVanillaThrownWeaponsForProjectileRuntime) {
    WeaponSystem weapons;

    ASSERT_TRUE(weapons.SelectWeapon(14));
    EXPECT_EQ(weapons.GetActiveWeapon().projectile_type, ProjectileType::FragGrenade);

    ASSERT_TRUE(weapons.SelectWeapon(15));
    EXPECT_EQ(weapons.GetActiveWeapon().projectile_type, ProjectileType::Flashbang);

    ASSERT_TRUE(weapons.SelectWeapon(16));
    EXPECT_EQ(weapons.GetActiveWeapon().projectile_type, ProjectileType::ProximityMine);

    ASSERT_TRUE(weapons.SelectWeapon(12));
    EXPECT_EQ(weapons.GetActiveWeapon().projectile_type, ProjectileType::Rocket);
}

TEST(RuntimeWeaponTest, UsesRetailAutomaticCadenceAndResetsBurstOnRelease) {
    WeaponSystem weapons;
    ASSERT_TRUE(weapons.SelectWeaponSlot(6)); // MP5SD: 700 RPM -> 2 fixed ticks

    BulletTrace first_trace;
    EXPECT_TRUE(weapons.TryFire(glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f), first_trace));
    EXPECT_NE(first_trace.direction, glm::vec3(0.0f, 1.0f, 0.0f));
    EXPECT_EQ(weapons.GetCurrentClipAmmo(), 31U);

    weapons.Update(1.0 / 30.0, true);
    BulletTrace cooling_trace;
    EXPECT_FALSE(weapons.TryFire(
        glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f), cooling_trace));

    weapons.Update(1.0 / 30.0, true);
    EXPECT_TRUE(weapons.TryFire(
        glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f), cooling_trace));
    EXPECT_EQ(weapons.GetCurrentClipAmmo(), 30U);

    weapons.Update(1.0 / 30.0, false);
    weapons.Update(1.0 / 30.0, false);
    EXPECT_TRUE(weapons.TryFire(
        glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f), cooling_trace));
}

TEST(RuntimeWeaponTest, PreservesAmmoWhenCyclingThePlayerLoadout) {
    WeaponSystem weapons;
    ASSERT_TRUE(weapons.SelectWeaponSlot(6)); // MP5SD

    BulletTrace trace;
    ASSERT_TRUE(weapons.TryFire(
        glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f), trace));
    EXPECT_EQ(weapons.GetCurrentClipAmmo(), 31U);

    ASSERT_TRUE(weapons.SelectWeaponSlot(0));
    ASSERT_TRUE(weapons.SelectWeaponSlot(6));
    EXPECT_EQ(weapons.GetCurrentClipAmmo(), 31U);
}

TEST(RuntimeWeaponTest, EmitsRetailShotgunPelletsAsOneAmmoConsumingShot) {
    WeaponSystem weapons;
    ASSERT_TRUE(weapons.SelectWeaponSlot(9)); // WEAPON_ID_SPAS12

    std::vector<BulletTrace> traces;
    ASSERT_TRUE(weapons.TryFire(
        glm::vec3(0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        traces));

    EXPECT_EQ(traces.size(), 30U);
    EXPECT_EQ(weapons.GetCurrentClipAmmo(), 6U);
    EXPECT_TRUE(std::any_of(
        traces.begin() + 1,
        traces.end(),
        [&traces](const BulletTrace& trace) {
            return trace.direction != traces.front().direction;
        }));
}

TEST(RuntimeWeaponTest, FireAndRecoilCooldown) {
    WeaponSystem weapons;
    ASSERT_TRUE(weapons.SelectWeapon(4)); // WEAPON_ID_M16A2

    BulletTrace trace;
    bool fired = weapons.TryFire(glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f), trace);
    EXPECT_TRUE(fired);
    EXPECT_TRUE(trace.hit);
    EXPECT_GT(trace.distance, PlayerController::WORLD_METER);
    EXPECT_EQ(weapons.GetCurrentClipAmmo(), 19U);

    // Rapid second shot must be throttled by RPM cooldown
    bool fired_immediately = weapons.TryFire(glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f), trace);
    EXPECT_FALSE(fired_immediately);

    // Advance cooldown
    for (int tick = 0; tick < 3; ++tick) {
        weapons.Update(1.0 / 30.0, true);
    }
    bool fired_after_cooldown = weapons.TryFire(glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f), trace);
    EXPECT_TRUE(fired_after_cooldown);
    EXPECT_EQ(weapons.GetCurrentClipAmmo(), 18U);
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

TEST(RuntimeAiTest, PatrolMovementRespectsSolidGeometryBoundary) {
    AiSystem ai;
    ai.SetMovementCollisionQuery([](const glm::vec3& position) {
        return position.y > 1.0f;
    });

    AiGuardEntity guard;
    guard.id = 12;
    guard.position = glm::vec3(0.0f);
    guard.current_waypoint = 1;
    guard.waypoints = {
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 100000.0f, 0.0f),
    };
    ai.RegisterGuard(guard);

    ai.Update(GameClock::TICK_INTERVAL_SECONDS, glm::vec3(100000.0f), false);

    ASSERT_EQ(ai.GetGuards().size(), 1U);
    EXPECT_FLOAT_EQ(ai.GetGuards()[0].position.y, 0.0f);
}

TEST(RuntimeAiTest, RetailInvulnerabilityFlagBlocksDamage) {
    AiSystem ai;
    AiGuardEntity guard;
    guard.id = 13;
    guard.script_invulnerable = true;
    ai.RegisterGuard(guard);

    ai.ApplyDamage(guard.id, 25.0f);

    ASSERT_EQ(ai.GetGuards().size(), 1U);
    EXPECT_FLOAT_EQ(ai.GetGuards()[0].health, 100.0f);
    EXPECT_EQ(ai.GetGuards()[0].state, AiGuardState::Patrol);
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

TEST(RuntimeLevelFlowTest, ActiveObjectiveAdvancesThroughInteraction) {
    LevelFlow flow;
    flow.InitializeMission(1);

    EXPECT_EQ(flow.GetObjectiveDisplayText(),
              "Infiltrate the trainyard and download train schedule");
    EXPECT_TRUE(flow.CompleteFirstPendingPrimaryObjective());
    EXPECT_EQ(flow.GetObjectiveDisplayText(), "Reach the extraction zone");
    EXPECT_FALSE(flow.CompleteFirstPendingPrimaryObjective());
}

TEST(RuntimeInputTest, ActivateIsMomentaryAndFocusGated) {
    WindowInputRouter router;
    router.SetFocus(WindowFocusTarget::GameplayWindow);
    router.OnKeyboardKey('E', true);

    PlayerInputCmd command = router.ConsumeGameplayInput();
    EXPECT_TRUE(command.interact);
    EXPECT_FALSE(router.ConsumeGameplayInput().interact);

    router.SetFocus(WindowFocusTarget::EditorWindow);
    router.OnKeyboardKey('E', true);
    EXPECT_FALSE(router.ConsumeGameplayInput().interact);
}

TEST(RuntimeInputTest, RightMouseIsAimWithoutImplicitReload) {
    WindowInputRouter router;
    router.SetFocus(WindowFocusTarget::GameplayWindow);
    router.OnMouseButton(2, true);

    const PlayerInputCmd command = router.ConsumeGameplayInput();
    EXPECT_TRUE(command.zoom);
    EXPECT_FALSE(command.reload);
}

TEST(RuntimeInputTest, OpposingMovementKeysRemainIndependent) {
    WindowInputRouter router;
    router.SetFocus(WindowFocusTarget::GameplayWindow);

    router.OnKeyboardKey('W', true);
    router.OnKeyboardKey('S', true);
    router.OnKeyboardKey('W', false);

    EXPECT_FLOAT_EQ(router.ConsumeGameplayInput().forward, -1.0f);

    router.OnKeyboardKey('S', false);
    EXPECT_FLOAT_EQ(router.ConsumeGameplayInput().forward, 0.0f);
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

TEST(RuntimeWorldTest, ProjectileDetonationAppliesBlastDamageToNearbyGuard) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain);

    AiGuardEntity guard;
    guard.id = 28;
    guard.position = glm::vec3(0.0f, 1.0f * PlayerController::WORLD_METER, 0.0f);
    guard.health = 100.0f;
    world.GetAi().RegisterGuard(guard);

    ProjectileLaunch launch;
    launch.position = glm::vec3(0.0f);
    launch.type = ProjectileType::FragGrenade;
    launch.fuse_ticks = 1;
    launch.damage = 100.0f;
    launch.explosion_radius_units = 5.0f * PlayerController::WORLD_METER;
    ASSERT_TRUE(world.GetProjectiles().Spawn(launch));

    world.UpdateSimulationTick(0, PlayerInputCmd());

    ASSERT_EQ(world.GetAi().GetGuards().size(), 1U);
    EXPECT_LE(world.GetAi().GetGuards()[0].health, 0.0f);
    EXPECT_EQ(world.GetProjectiles().GetDetonations().size(), 1U);
}

TEST(RuntimeWorldTest, ProjectileWeaponLaunchesOncePerTriggerPress) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain);
    ASSERT_TRUE(world.GetWeapons().SelectWeapon(14));

    PlayerInputCmd fire_command;
    fire_command.fire = true;
    world.UpdateSimulationTick(0, fire_command);
    world.UpdateSimulationTick(1, fire_command);

    EXPECT_EQ(world.GetProjectiles().GetProjectiles().size(), 1U);
    EXPECT_EQ(world.GetWeapons().GetCurrentClipAmmo(), 0U);

    world.UpdateSimulationTick(2, PlayerInputCmd());
    EXPECT_EQ(world.GetProjectiles().GetProjectiles().size(), 1U);
}

TEST(RuntimeWorldTest, MissionRestartRestoresTheInitialWeaponLoadout) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain);
    ASSERT_TRUE(world.GetWeapons().SelectWeaponSlot(6)); // MP5SD

    BulletTrace trace;
    ASSERT_TRUE(world.GetWeapons().TryFire(
        glm::vec3(0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        trace));
    EXPECT_EQ(world.GetWeapons().GetCurrentClipAmmo(), 31U);

    world.Reset();

    ASSERT_TRUE(world.GetWeapons().SelectWeaponSlot(6));
    EXPECT_EQ(world.GetWeapons().GetCurrentClipAmmo(), 32U);
}

TEST(RuntimeWorldTest, SolidGeometryOccludesPlayerFire) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain, WallAtOneMeter);
    world.GetPlayer().Reset(glm::vec3(0.0f, 0.0f, 0.0f));
    world.GetPlayer().SetOrientation(0.0f, 0.0f);

    AiGuardEntity guard;
    guard.id = 18;
    guard.position = glm::vec3(0.0f, 2.0f * PlayerController::WORLD_METER, 0.0f);
    guard.yaw = 180.0f;
    guard.health = 100.0f;
    world.GetAi().RegisterGuard(guard);

    PlayerInputCmd input;
    input.fire = true;
    world.UpdateSimulationTick(0, input);

    ASSERT_EQ(world.GetAi().GetGuards().size(), 1U);
    EXPECT_FLOAT_EQ(world.GetAi().GetGuards()[0].health, 100.0f);
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

TEST(RuntimeWorldTest, CombatGuardFiresTheRuntimeWeaponAtThePlayer) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain);
    world.GetPlayer().ApplyTuning(100.0f, 0.0f);
    world.GetPlayer().Reset(glm::vec3(0.0f, 0.0f, 0.0f));

    AiGuardEntity guard;
    guard.id = 27;
    guard.position = glm::vec3(0.0f, 2.0f * PlayerController::WORLD_METER, 0.0f);
    guard.yaw = 180.0f;
    guard.state = AiGuardState::Combat;
    guard.health = 100.0f;
    world.GetAi().RegisterGuard(guard);

    world.UpdateSimulationTick(0, PlayerInputCmd());

    EXPECT_FLOAT_EQ(world.GetPlayer().GetHealth(), 65.0f);
}

TEST(RuntimeWorldTest, SolidGeometryBlocksGuardLineOfSightDamage) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain, WallAtOneMeter);
    world.GetPlayer().ApplyTuning(100.0f, 0.0f);
    world.GetPlayer().Reset(glm::vec3(0.0f, 0.0f, 0.0f));

    AiGuardEntity guard;
    guard.id = 24;
    guard.position = glm::vec3(0.0f, 2.0f * PlayerController::WORLD_METER, 0.0f);
    guard.yaw = 180.0f;
    guard.state = AiGuardState::Combat;
    guard.health = 100.0f;
    world.GetAi().RegisterGuard(guard);

    const float initial_health = world.GetPlayer().GetHealth();
    world.UpdateSimulationTick(30, PlayerInputCmd());

    EXPECT_FLOAT_EQ(world.GetPlayer().GetHealth(), initial_health);
}

TEST(RuntimeWorldTest, SolidGeometryBlocksGuardPerception) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain, WallAtOneMeter);
    world.GetPlayer().Reset(glm::vec3(0.0f, 0.0f, 0.0f));

    AiGuardEntity guard;
    guard.id = 25;
    guard.position = glm::vec3(0.0f, 2.0f * PlayerController::WORLD_METER, 0.0f);
    guard.yaw = 180.0f;
    guard.state = AiGuardState::Patrol;
    world.GetAi().RegisterGuard(guard);

    world.UpdateSimulationTick(0, PlayerInputCmd());

    ASSERT_EQ(world.GetAi().GetGuards().size(), 1U);
    EXPECT_EQ(world.GetAi().GetGuards()[0].state, AiGuardState::Patrol);
    EXPECT_FLOAT_EQ(world.GetAi().GetGuards()[0].suspicion, 0.0f);
}

TEST(RuntimeWorldTest, StaticGeometryRoofPreventsPlayerFromStanding) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain, LowRoofAtStandingHeight);
    world.GetPlayer().Reset(glm::vec3(0.0f, 0.0f, 0.0f));

    PlayerInputCmd crouch_command;
    crouch_command.crouch = true;
    world.UpdateSimulationTick(0, crouch_command);
    EXPECT_EQ(world.GetPlayer().GetStance(), PlayerStanceState::Crouching);

    world.UpdateSimulationTick(1, PlayerInputCmd());

    EXPECT_EQ(world.GetPlayer().GetStance(), PlayerStanceState::Crouching);
    EXPECT_LT(world.GetPlayer().GetEyeHeight(), PlayerController::STANDING_EYE_HEIGHT);
}

TEST(RuntimeWorldTest, RetailAiQvmDrivesGuardActionsOnFixedTicks) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain);

    AiGuardEntity guard;
    guard.id = 26;
    guard.position = glm::vec3(0.0f, 100.0f * PlayerController::WORLD_METER, 0.0f);
    guard.state = AiGuardState::Patrol;
    guard.patrol_routes[700] = {
        AiPatrolCommand{AiPatrolCommandKind::Delay, 3},
    };
    world.GetAi().RegisterGuard(guard);

    ASSERT_TRUE(world.AttachGuardScript(guard.id, BuildRetailAiPatrolScript()));

    world.UpdateSimulationTick(0, PlayerInputCmd());
    ASSERT_EQ(world.GetAi().GetGuards().size(), 1U);
    EXPECT_EQ(world.GetAi().GetGuards()[0].script_last_event_type, 0);
    EXPECT_EQ(world.GetAi().GetGuards()[0].script_patrol_path_id, -1);

    world.UpdateSimulationTick(1, PlayerInputCmd());
    EXPECT_EQ(world.GetAi().GetGuards()[0].script_last_event_type, 4);
    EXPECT_EQ(world.GetAi().GetGuards()[0].script_patrol_path_id, 700);
    EXPECT_EQ(world.GetAi().GetGuards()[0].active_patrol_path_id, 700);
    ASSERT_EQ(world.GetAi().GetGuards()[0].patrol_commands.size(), 1U);
    EXPECT_EQ(world.GetAi().GetGuards()[0].patrol_commands[0].operand, 3);
}

TEST(RuntimeWorldTest, ActivateCompletesTheCurrentMissionObjective) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain);
    world.SetExtractionZone(glm::vec3(100000.0f, 0.0f, 0.0f), 100.0f);
    world.GetPlayer().Reset(glm::vec3(0.0f, 0.0f, 0.0f));

    PlayerInputCmd input;
    input.interact = true;
    world.UpdateSimulationTick(0, input);

    ASSERT_FALSE(world.GetLevelFlow().GetObjectives().empty());
    EXPECT_EQ(world.GetLevelFlow().GetObjectives()[0].state, ObjectiveState::Completed);
    EXPECT_EQ(world.GetLevelFlow().GetStatus(), MissionStatus::InProgress);

    world.GetPlayer().SetPosition(glm::vec3(100000.0f, 0.0f, 0.0f));
    world.UpdateSimulationTick(1, PlayerInputCmd());
    EXPECT_EQ(world.GetLevelFlow().GetStatus(), MissionStatus::Success);
}

TEST(RuntimeWorldTest, InteractionProviderSeparatesDoorUseFromObjectiveCompletion) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain);
    world.SetInteractionQuery(
        [](const glm::vec3&, const glm::vec3&) {
            return RuntimeInteractionResult{true, false};
        });

    PlayerInputCmd input;
    input.interact = true;
    world.UpdateSimulationTick(0, input);

    ASSERT_FALSE(world.GetLevelFlow().GetObjectives().empty());
    EXPECT_EQ(world.GetLevelFlow().GetObjectives()[0].state, ObjectiveState::Pending);
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

TEST(RuntimeHostTest, PausingFreezesTicksAndRepeatedSessionsStayIsolated) {
    GameplayHost host;
    host.Initialize(FlatTerrain);

    EditorSnapshot snapshot;
    ASSERT_TRUE(host.OpenGameplay(snapshot));
    EXPECT_FALSE(host.OpenGameplay(snapshot));

    host.GetInputRouter().OnKeyboardKey('W', true);
    host.Update(100);
    const glm::vec3 position_after_running_tick = host.GetWorld().GetPlayer().GetPosition();

    host.SetPaused(true);
    host.Update(1000);
    EXPECT_EQ(host.GetWorld().GetPlayer().GetPosition(), position_after_running_tick);

    host.SetPaused(false);
    host.Update(1100);
    EXPECT_NE(host.GetWorld().GetPlayer().GetPosition(), position_after_running_tick);

    EditorSnapshot restored;
    ASSERT_TRUE(host.CloseGameplay(restored));
    EXPECT_EQ(host.GetInputRouter().GetFocus(), WindowFocusTarget::EditorWindow);
    EXPECT_FALSE(host.IsPaused());

    ASSERT_TRUE(host.OpenGameplay(snapshot));
    EXPECT_EQ(host.GetWorld().GetPlayer().GetPosition(), glm::vec3(0.0f));
    ASSERT_TRUE(host.CloseGameplay(restored));
}
