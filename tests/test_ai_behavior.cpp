// AI behaviour tests â€” pins the vanilla engagement model:
//   * soldiers detect on sight and move to a SHOOTING POSITION (never flee)
//   * civilians panic-run AWAY from the player
//   * undetected guards patrol or hold their original position
//   * heard stimuli are investigated, then the routine resumes
#include <gtest/gtest.h>
#include <random>
#include <cmath>
#include "../source/ai_system.h"
#include "../source/game_clock.h"
#include "../source/renderer/graph_writer.h"

using namespace igi;

namespace {

constexpr float kMeters = 4096.0f;

glm::vec3 EyeAt(const glm::vec3& feet) {
    return feet + glm::vec3(0.0f, 0.0f, PlayerController::STANDING_EYE_HEIGHT);
}

AiGuardEntity MakeGuard(uint32_t id, const glm::vec3& position, float yaw_degrees = 0.0f) {
    AiGuardEntity guard;
    guard.id = id;
    guard.position = position;
    guard.yaw = yaw_degrees;
    guard.state = AiGuardState::Patrol;
    return guard;
}

// Line-of-sight always clear unless a test says otherwise.
void AllowAllSight(AiSystem& ai) {
    ai.SetLineOfSightQuery([](const glm::vec3&, const glm::vec3&) { return true; });
}

// Builds a nav graph whose nodes sit on the +Y axis at 0..(n-1) metres with a
// route table that steps one node toward the destination per hop.
std::shared_ptr<GraphFile> MakeLineGraph(int nodeCount) {
    auto g = std::make_shared<GraphFile>();
    g->max_nodes = nodeCount;
    g->valid = true;
    for (int i = 0; i < nodeCount; ++i) {
        GraphNode n;
        n.id = i;
        n.x = 0.0;
        n.y = i * 4096.0;
        n.z = 0.0;
        n.radius = 100.0f;
        g->nodes.push_back(n);
    }
    g->route_table.resize((size_t)nodeCount * nodeCount, GraphRouteEntry{-1, -1.0f});
    for (int from = 0; from < nodeCount; ++from) {
        for (int to = 0; to < nodeCount; ++to) {
            if (from == to) continue;
            int next = from < to ? from + 1 : from - 1;
            g->route_table[(size_t)from + (size_t)to * nodeCount] =
                GraphRouteEntry{next, 1000.0f};
        }
    }
    return g;
}

} // namespace

// ---- Archetype resolution -------------------------------------------------

TEST(AiArchetypeTest, CivilianTypesResolveToCivilian) {
    EXPECT_EQ(ResolveAiArchetype("AITYPE_CIVILIAN"), AiArchetype::Civilian);
}

TEST(AiArchetypeTest, SoldierTypesResolveToSoldier) {
    EXPECT_EQ(ResolveAiArchetype("AITYPE_GUARD_AK"), AiArchetype::Soldier);
    EXPECT_EQ(ResolveAiArchetype("AITYPE_PATROL_AK"), AiArchetype::Soldier);
    EXPECT_EQ(ResolveAiArchetype("AITYPE_SNIPER"), AiArchetype::Soldier);
    EXPECT_EQ(ResolveAiArchetype("AITYPE_RPG"), AiArchetype::Soldier);
    EXPECT_EQ(ResolveAiArchetype("AITYPE_GUNNER"), AiArchetype::Soldier);
    EXPECT_EQ(ResolveAiArchetype("AITYPE_SPETNAZ_GUARD_AK"), AiArchetype::Soldier);
    EXPECT_EQ(ResolveAiArchetype("AITYPE_MAFIA_PATROL_UZI"), AiArchetype::Soldier);
}

TEST(AiArchetypeTest, UnknownOrEmptyResolvesToSoldier) {
    // Every HumanAI without an explicit CIVILIAN marker fights like retail.
    EXPECT_EQ(ResolveAiArchetype(""), AiArchetype::Soldier);
    EXPECT_EQ(ResolveAiArchetype("AITYPE_ANYA"), AiArchetype::Soldier);
}

// ---- Detection: sight alone escalates to combat ---------------------------

TEST(AiBehaviorTest, SoldierDetectsPlayerBySightWithoutAnyAlarm) {
    AiSystem ai;
    ai.SetDetectionEnabled(true);
    AllowAllSight(ai);
    AiGuardEntity guard = MakeGuard(1, glm::vec3(0.0f));
    ai.RegisterGuard(guard);

    const glm::vec3 player_feet(0.0f, 10.0f * kMeters, 0.0f); // straight ahead
    ai.Update(GameClock::TICK_INTERVAL_SECONDS, player_feet, EyeAt(player_feet), true);

    ASSERT_FALSE(ai.GetGuards().empty());
    EXPECT_EQ(ai.GetGuards()[0].state, AiGuardState::Combat);
}

TEST(AiBehaviorTest, PlayerBehindWallIsNotDetected) {
    AiSystem ai;
    ai.SetDetectionEnabled(true);
    ai.SetLineOfSightQuery([](const glm::vec3&, const glm::vec3&) { return false; });
    AiGuardEntity guard = MakeGuard(2, glm::vec3(0.0f));
    ai.RegisterGuard(guard);

    const glm::vec3 player_feet(0.0f, 10.0f * kMeters, 0.0f);
    ai.Update(GameClock::TICK_INTERVAL_SECONDS, player_feet, EyeAt(player_feet), true);

    ASSERT_FALSE(ai.GetGuards().empty());
    EXPECT_NE(ai.GetGuards()[0].state, AiGuardState::Combat);
    EXPECT_FLOAT_EQ(ai.GetGuards()[0].suspicion, 0.0f);
}

// ---- Combat: shooting position, never running away ------------------------

TEST(AiBehaviorTest, EngagedSoldierHoldsShootingPositionAndFacesPlayer) {
    AiSystem ai;
    ai.SetDetectionEnabled(true);
    AllowAllSight(ai);
    AiGuardEntity guard = MakeGuard(3, glm::vec3(0.0f));
    ai.RegisterGuard(guard);

    const glm::vec3 player_feet(0.0f, 10.0f * kMeters, 0.0f); // inside engage range
    const glm::vec3 player_eye = EyeAt(player_feet);

    ai.Update(GameClock::TICK_INTERVAL_SECONDS, player_feet, player_eye, true);
    ASSERT_EQ(ai.GetGuards()[0].state, AiGuardState::Combat);

    const glm::vec3 held_position = ai.GetGuards()[0].position;
    for (int i = 0; i < 30; ++i) { // one second of firefight
        ai.Update(GameClock::TICK_INTERVAL_SECONDS, player_feet, player_eye, true);
    }

    EXPECT_EQ(ai.GetGuards()[0].state, AiGuardState::Combat);
    EXPECT_FLOAT_EQ(ai.GetGuards()[0].position.x, held_position.x);
    EXPECT_FLOAT_EQ(ai.GetGuards()[0].position.y, held_position.y);
    // Facing the player: yaw 0 already points +Y at him.
    EXPECT_NEAR(ai.GetGuards()[0].yaw, 0.0f, 1.0f);
}

TEST(AiBehaviorTest, SeenSoldierHoldsAndFiresInsteadOfChargingLikePanic) {
    AiSystem ai;
    ai.SetDetectionEnabled(true);
    AllowAllSight(ai);
    auto graph = MakeLineGraph(64); // nodes every metre along +Y
    AiGuardEntity guard = MakeGuard(4, glm::vec3(0.0f));
    guard.graph = graph;
    guard.graph_offset = glm::vec3(0.0f);
    guard.current_node = 0;
    ai.RegisterGuard(guard);

    const glm::vec3 player_feet(0.0f, 28.0f * kMeters, 0.0f); // seen, inside weapon range
    const glm::vec3 player_eye = EyeAt(player_feet);

    const float initial_distance = glm::length(ai.GetGuards()[0].position - player_feet);
    for (int i = 0; i < 300; ++i) { // ten seconds under mutual sight
        ai.Update(GameClock::TICK_INTERVAL_SECONDS, player_feet, player_eye, true);
        const float distance = glm::length(glm::vec2(
            ai.GetGuards()[0].position.x - player_feet.x,
            ai.GetGuards()[0].position.y - player_feet.y));
        ASSERT_LT(distance, initial_distance + kMeters)
            << "guard drifted AWAY from a visible target at tick " << i;
    }

    EXPECT_EQ(ai.GetGuards()[0].state, AiGuardState::Combat);
    // He holds his firing position instead of sprinting around like a
    // panicking civilian — distance stays put (within one metre of start).
    const float final_distance = glm::length(ai.GetGuards()[0].position - player_feet);
    EXPECT_NEAR(final_distance, initial_distance, kMeters);
}

TEST(AiBehaviorTest, FightingSoldierNeverIncreasesDistanceFromThePlayer) {
    AiSystem ai;
    ai.SetDetectionEnabled(true);
    AllowAllSight(ai);
    auto graph = MakeLineGraph(64);
    AiGuardEntity guard = MakeGuard(5, glm::vec3(0.0f));
    guard.graph = graph;
    guard.current_node = 0;
    ai.RegisterGuard(guard);

    const glm::vec3 player_start(0.0f, 12.0f * kMeters, 0.0f);
    const glm::vec3 player_end(6.0f * kMeters, 20.0f * kMeters, 0.0f);

    ai.Update(GameClock::TICK_INTERVAL_SECONDS, player_start, EyeAt(player_start), true);
    ASSERT_EQ(ai.GetGuards()[0].state, AiGuardState::Combat);

    float worst_distance = 0.0f;
    for (int i = 0; i < 120; ++i) {
        // The player strafes away mid-fight; a panicking AI would open the gap.
        const float t = (float)i / 119.0f;
        const glm::vec3 feet = player_start + (player_end - player_start) * t;
        ai.Update(GameClock::TICK_INTERVAL_SECONDS, feet, EyeAt(feet), true);
        worst_distance = std::max(worst_distance,
            glm::length(ai.GetGuards()[0].position - feet));
    }

    EXPECT_LT(worst_distance, 25.0f * kMeters)
        << "the soldier let the player escape instead of holding pursuit";
}

TEST(AiBehaviorTest, SoldierLosingSightHuntsLastKnownPositionThenStandsDown) {
    AiSystem ai;
    ai.SetDetectionEnabled(true);
    AllowAllSight(ai);
    auto graph = MakeLineGraph(32);
    AiGuardEntity guard = MakeGuard(6, glm::vec3(0.0f));
    guard.graph = graph;
    guard.current_node = 0;
    ai.RegisterGuard(guard);

    const glm::vec3 player_visible(0.0f, 15.0f * kMeters, 0.0f);
    ai.Update(GameClock::TICK_INTERVAL_SECONDS, player_visible, EyeAt(player_visible), true);
    ASSERT_EQ(ai.GetGuards()[0].state, AiGuardState::Combat);

    // The player vanishes (behind cover): the soldier keeps hunting.
    for (int i = 0; i < 60; ++i) {
        ai.Update(GameClock::TICK_INTERVAL_SECONDS, player_visible, EyeAt(player_visible), false);
    }
    EXPECT_EQ(ai.GetGuards()[0].state, AiGuardState::Combat)
        << "a hunting soldier must not instantly forget his target";

    // ...but not forever: after the hunt window he returns to routine.
    for (int i = 0; i < 400; ++i) {
        ai.Update(GameClock::TICK_INTERVAL_SECONDS, player_visible, EyeAt(player_visible), false);
    }
    EXPECT_EQ(ai.GetGuards()[0].state, AiGuardState::Patrol);
}

// ---- Civilians panic instead of fighting ----------------------------------

TEST(AiBehaviorTest, CivilianPanicsOnSightAndRunsAwayFromThePlayer) {
    AiSystem ai;
    ai.SetDetectionEnabled(true);
    AllowAllSight(ai);
    AiGuardEntity civilian = MakeGuard(7, glm::vec3(0.0f));
    civilian.archetype = ResolveAiArchetype("AITYPE_CIVILIAN");
    civilian.ai_type_name = "AITYPE_CIVILIAN";
    ai.RegisterGuard(civilian);

    const glm::vec3 player_feet(0.0f, 8.0f * kMeters, 0.0f);
    const glm::vec3 player_eye = EyeAt(player_feet);
    ai.Update(GameClock::TICK_INTERVAL_SECONDS, player_feet, player_eye, true);

    ASSERT_EQ(ai.GetGuards().size(), 1U);
    EXPECT_EQ(ai.GetGuards()[0].state, AiGuardState::Panic)
        << "civilians must never stand and fight";

    const float start_distance = glm::length(
        ai.GetGuards()[0].position - player_feet);
    for (int i = 0; i < 60; ++i) { // two seconds of flight
        ai.Update(GameClock::TICK_INTERVAL_SECONDS, player_feet, player_eye, true);
    }
    const float end_distance = glm::length(
        ai.GetGuards()[0].position - player_feet);

    EXPECT_GT(end_distance, start_distance + 4.0f * kMeters)
        << "the civilian did not actually flee";
}

TEST(AiBehaviorTest, CivilianShotWoundKeepsItFleeingInsteadOfFightingBack) {
    AiSystem ai;
    ai.SetDetectionEnabled(true);
    AiGuardEntity civilian = MakeGuard(8, glm::vec3(0.0f));
    civilian.archetype = ResolveAiArchetype("AITYPE_CIVILIAN");
    ai.RegisterGuard(civilian);

    ai.ApplyDamage(8, 40.0f);

    ASSERT_EQ(ai.GetGuards().size(), 1U);
    EXPECT_FLOAT_EQ(ai.GetGuards()[0].health, 60.0f);
    EXPECT_EQ(ai.GetGuards()[0].state, AiGuardState::Panic);
}

// STEALTH MODEL: gunfire alone never alerts anyone — soldiers keep their
// routine and civilians only flee what they SEE.
TEST(AiBehaviorTest, GunshotAloneKeepsEveryoneInStealth) {
    AiSystem civilian_ai;
    AiGuardEntity civilian = MakeGuard(9, glm::vec3(5.0f * kMeters, 0.0f, 0.0f));
    civilian.archetype = ResolveAiArchetype("AITYPE_CIVILIAN");
    civilian_ai.RegisterGuard(civilian);

    AiStimulusEvent gunshot;
    gunshot.type = AiEventType::Gunshot;
    gunshot.position = glm::vec3(0.0f);
    gunshot.loudness = 1.0f;
    // Retail gunshot audibility: HUMANAI_DETECTIONEVENT_GUNSHOT_RANGE (30 m).
    gunshot.hearing_radius_units = 30.0f * kMeters;
    civilian_ai.GetEventQueue().Post(gunshot);
    civilian_ai.Update(GameClock::TICK_INTERVAL_SECONDS, glm::vec3(100000.0f), false);
    EXPECT_EQ(civilian_ai.GetGuards()[0].state, AiGuardState::Patrol)
        << "a heard shot must not panic an unaware civilian";

    AiSystem soldier_ai;
    AiGuardEntity soldier = MakeGuard(10, glm::vec3(5.0f * kMeters, 0.0f, 0.0f));
    soldier_ai.RegisterGuard(soldier);
    soldier_ai.GetEventQueue().Post(gunshot);
    soldier_ai.Update(GameClock::TICK_INTERVAL_SECONDS, glm::vec3(100000.0f), false);
    EXPECT_EQ(soldier_ai.GetGuards()[0].state, AiGuardState::Patrol)
        << "a heard shot must not alert an unaware soldier";
}

// ---- Undisturbed routines --------------------------------------------------

TEST(AiBehaviorTest, UndetectedPatrolGuardKeepsWalkingHisWaypoints) {
    AiSystem ai;
    AiGuardEntity guard = MakeGuard(11, glm::vec3(0.0f));
    guard.waypoints = {
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 8.0f * kMeters, 0.0f),
    };
    ai.RegisterGuard(guard);

    const glm::vec3 far_away(100000.0f, 100000.0f, 0.0f);
    float travelled = 0.0f;
    glm::vec3 last = ai.GetGuards()[0].position;
    for (int i = 0; i < 90; ++i) { // three seconds
        ai.Update(GameClock::TICK_INTERVAL_SECONDS, far_away, EyeAt(far_away), false);
        travelled += glm::length(ai.GetGuards()[0].position - last);
        last = ai.GetGuards()[0].position;
    }

    EXPECT_GT(travelled, 2.0f * kMeters) << "patrol stopped walking";
    EXPECT_EQ(ai.GetGuards()[0].state, AiGuardState::Patrol);
}

TEST(AiBehaviorTest, UndetectedStationaryGuardStandsAtOriginalPosition) {
    AiSystem ai;
    AiGuardEntity guard = MakeGuard(12, glm::vec3(7.0f * kMeters, -3.0f * kMeters, 0.0f));
    guard.stationary = true;
    guard.state = AiGuardState::Idle;
    ai.RegisterGuard(guard);

    const glm::vec3 far_away(100000.0f, 100000.0f, 0.0f);
    for (int i = 0; i < 120; ++i) {
        ai.Update(GameClock::TICK_INTERVAL_SECONDS, far_away, EyeAt(far_away), false);
    }

    EXPECT_FLOAT_EQ(ai.GetGuards()[0].position.x, 7.0f * kMeters);
    EXPECT_FLOAT_EQ(ai.GetGuards()[0].position.y, -3.0f * kMeters);
    EXPECT_EQ(ai.GetGuards()[0].state, AiGuardState::Idle);
}

// ---- Suspicious investigation ---------------------------------------------

// STEALTH MODEL: an impact stimulus alone never pulls a guard off his
// routine — he keeps patrolling exactly as authored until he SEES the player.
TEST(AiBehaviorTest, StimulusAloneDoesNotPullGuardOffHisRoutine) {
    AiSystem ai;
    auto graph = MakeLineGraph(48); // nodes every metre along +Y
    AiGuardEntity guard = MakeGuard(13, glm::vec3(0.0f));
    guard.graph = graph;
    guard.current_node = 0;
    ai.RegisterGuard(guard);

    AiStimulusEvent impact;
    impact.type = AiEventType::GroundImpact;
    impact.position = glm::vec3(1.0f * kMeters, 6.0f * kMeters, 0.0f);
    impact.hearing_radius_units = 20.0f * kMeters;
    ai.GetEventQueue().Post(impact);

    for (int i = 0; i < 300; ++i) { // ten seconds of noise nearby
        ai.Update(GameClock::TICK_INTERVAL_SECONDS, glm::vec3(100000.0f), false);
    }

    EXPECT_EQ(ai.GetGuards()[0].state, AiGuardState::Patrol)
        << "sound alone must not break stealth";
    EXPECT_FLOAT_EQ(ai.GetGuards()[0].suspicion, 0.0f);
}

// A glimpse OUTSIDE the primary cone (peripheral only) does not detect:
// the guard stays in stealth unless the player crosses his real sight.
TEST(AiBehaviorTest, PeripheralGlimpseAloneKeepsGuardInStealth) {
    AiSystem ai;
    AllowAllSight(ai);
    AiGuardEntity guard = MakeGuard(14, glm::vec3(0.0f)); // facing +Y
    ai.RegisterGuard(guard);

    const glm::vec3 player_feet(
        17.32f * kMeters, 10.0f * kMeters, 0.0f); // ~60 deg off-axis
    const glm::vec3 player_eye = EyeAt(player_feet);

    ASSERT_EQ(ai.CheckVision(guard, player_eye, false),
              AiVisionResult::Peripheral);

    for (int i = 0; i < 90; ++i) { // three seconds of side exposure
        ai.Update(GameClock::TICK_INTERVAL_SECONDS, player_feet, player_eye, true);
    }

    EXPECT_EQ(ai.GetGuards()[0].state, AiGuardState::Patrol)
        << "a peripheral glimpse must not escalate";
}

// ---- Master switch: detection OFF (default) = total stealth ----------------

TEST(AiBehaviorTest, DefaultSystemHasZeroDetectionAnywhere) {
    AiSystem ai;                       // detection NOT enabled
    AllowAllSight(ai);
    AiGuardEntity guard = MakeGuard(50, glm::vec3(0.0f));
    ai.RegisterGuard(guard);

    // Player stands dead-centre in front of the guard's face for 5 s.
    const glm::vec3 player_feet(0.0f, 8.0f * kMeters, 0.0f);
    for (int i = 0; i < 150; ++i) {
        ai.Update(GameClock::TICK_INTERVAL_SECONDS,
                  player_feet, EyeAt(player_feet), true);
    }

    EXPECT_EQ(ai.GetGuards()[0].state, AiGuardState::Patrol)
        << "default build must be pure stealth: nothing may detect the player";
    EXPECT_FALSE(ai.GetGuards()[0].saw_player_this_tick);

    // Even being shot must not aggro in stealth mode.
    ai.ApplyDamage(50, 30.0f);
    EXPECT_EQ(ai.GetGuards()[0].state, AiGuardState::Patrol);
    EXPECT_FLOAT_EQ(ai.GetGuards()[0].health, 70.0f);
}
