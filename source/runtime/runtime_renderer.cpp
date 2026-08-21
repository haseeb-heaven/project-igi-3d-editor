#include "runtime_renderer.h"

#include "runtime_world.h"
#include <utility>

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
    next_snapshot.clip_ammo = weapons.GetCurrentClipAmmo();
    next_snapshot.clip_capacity = active_weapon.clip_capacity;
    next_snapshot.reserve_ammo = weapons.GetReserveAmmo();

    next_snapshot.zoom_active = world.IsZoomActive();
    next_snapshot.objective_text = world.GetLevelFlow().GetObjectiveDisplayText();
    next_snapshot.flash_effect_strength = world.GetFlashEffectStrength();

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
