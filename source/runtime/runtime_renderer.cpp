#include "runtime_renderer.h"

#include "runtime_world.h"
#include <utility>

#include <algorithm>
#include <cmath>

namespace igi {

void RuntimeRenderer::Capture(
    const RuntimeWorld& world,
    const RuntimeRenderCamera& camera) {
    RuntimeRenderSnapshot next_snapshot;
    next_snapshot.camera = camera;

    const PlayerController& player = world.GetPlayer();
    next_snapshot.player_position = player.GetPosition();
    next_snapshot.player_eye_position = player.GetEyePosition();
    next_snapshot.player_yaw = player.GetYaw();
    next_snapshot.player_pitch = player.GetPitch();
    next_snapshot.player_health = player.GetHealth();
    next_snapshot.player_maximum_health = player.GetMaximumHealth();
    next_snapshot.player_armor = player.GetArmor();
    next_snapshot.player_maximum_armor = player.GetMaximumArmor();
    next_snapshot.player_alive = player.IsAlive();

    const WeaponSystem& weapons = world.GetWeapons();
    const WeaponDefinition& active_weapon = weapons.GetActiveWeapon();
    next_snapshot.active_weapon_name = active_weapon.name;
    next_snapshot.active_weapon_model_id = active_weapon.model_id;
    next_snapshot.weapon_view_pitch_radians =
        world.GetWeaponViewSway().GetPitchRadians();
    next_snapshot.weapon_view_yaw_radians =
        world.GetWeaponViewSway().GetYawRadians();
    next_snapshot.weapon_recoil_pitch_radians =
        world.GetWeaponViewRecoil().GetPitchRadians();
    next_snapshot.weapon_recoil_yaw_radians =
        world.GetWeaponViewRecoil().GetYawRadians();
    next_snapshot.weapon_view_transitioning =
        world.IsWeaponViewTransitioning();
    next_snapshot.clip_ammo = weapons.GetCurrentClipAmmo();
    next_snapshot.clip_capacity = active_weapon.clip_capacity;
    next_snapshot.reserve_ammo = weapons.GetReserveAmmo();

    next_snapshot.zoom_active = world.IsZoomActive();
    next_snapshot.objective_text = world.GetLevelFlow().GetObjectiveDisplayText();
    if (world.GetLevelFlow().HasAuthoredMissionFlow() &&
        world.GetLevelFlow().IsInterfaceTimerEnabled() &&
        world.GetLevelFlow().GetMaximumLevelPlayTimeSeconds() > 0.0) {
        const int64_t maximum_ticks = static_cast<int64_t>(std::llround(
            world.GetLevelFlow().GetMaximumLevelPlayTimeSeconds() *
            static_cast<double>(GameClock::TICK_RATE_HZ)));
        next_snapshot.mission_timer_remaining_ticks = std::max<int64_t>(
            0,
            maximum_ticks - static_cast<int64_t>(
                world.GetLevelFlow().GetMissionFlowTick()));
    }
    next_snapshot.mission_status_messages =
        world.GetDisplayedMissionStatusMessages();
    next_snapshot.flash_effect_strength = world.GetFlashEffectStrength();
    next_snapshot.muzzle_flash_strength = world.GetMuzzleFlashStrength();
    next_snapshot.explosions = world.GetExplosionRenderStates();

    const auto& live_projectiles = world.GetProjectiles().GetProjectiles();
    next_snapshot.projectiles.reserve(live_projectiles.size());
    for (const LiveProjectile& projectile : live_projectiles) {
        next_snapshot.projectiles.push_back({
            projectile.type,
            projectile.position,
            projectile.tumble_ticks,
        });
    }

    snapshot_ = std::move(next_snapshot);
}

void RuntimeRenderer::Clear() {
    snapshot_ = RuntimeRenderSnapshot();
}

} // namespace igi
