// weapon_system.cpp - Runtime weapon state machines, recoil, and ballistics simulation implementation
#include "weapon_system.h"
#include <algorithm>

namespace igi {

WeaponSystem::WeaponSystem() {
    SelectWeapon(0);
}

void WeaponSystem::SelectWeapon(uint32_t weapon_id) {
    active_weapon_.id = weapon_id;
    switch (weapon_id) {
        case 0:
            active_weapon_.name = "M16A2 Assault Rifle";
            active_weapon_.muzzle_velocity = 900.0f;
            active_weapon_.rounds_per_minute = 600.0f;
            active_weapon_.clip_capacity = 30;
            active_weapon_.recoil_pitch = 1.2f;
            active_weapon_.recoil_yaw = 0.4f;
            active_weapon_.base_spread = 0.015f;
            active_weapon_.damage = 35.0f;
            active_weapon_.is_automatic = true;
            reserve_ammo_ = 120;
            break;
        case 1:
            active_weapon_.name = "Glock 17 9mm";
            active_weapon_.muzzle_velocity = 375.0f;
            active_weapon_.rounds_per_minute = 400.0f;
            active_weapon_.clip_capacity = 17;
            active_weapon_.recoil_pitch = 1.0f;
            active_weapon_.recoil_yaw = 0.2f;
            active_weapon_.base_spread = 0.02f;
            active_weapon_.damage = 25.0f;
            active_weapon_.is_automatic = false;
            reserve_ammo_ = 68;
            break;
        case 2:
            active_weapon_.name = "MP5SD3 Submachine Gun";
            active_weapon_.muzzle_velocity = 285.0f;
            active_weapon_.rounds_per_minute = 800.0f;
            active_weapon_.clip_capacity = 30;
            active_weapon_.recoil_pitch = 0.8f;
            active_weapon_.recoil_yaw = 0.3f;
            active_weapon_.base_spread = 0.025f;
            active_weapon_.damage = 28.0f;
            active_weapon_.is_automatic = true;
            reserve_ammo_ = 150;
            break;
        case 3:
            active_weapon_.name = "AK-47 Assault Rifle";
            active_weapon_.muzzle_velocity = 715.0f;
            active_weapon_.rounds_per_minute = 600.0f;
            active_weapon_.clip_capacity = 30;
            active_weapon_.recoil_pitch = 1.8f;
            active_weapon_.recoil_yaw = 0.6f;
            active_weapon_.base_spread = 0.03f;
            active_weapon_.damage = 42.0f;
            active_weapon_.is_automatic = true;
            reserve_ammo_ = 120;
            break;
        case 4:
            active_weapon_.name = "Dragunov SVD Sniper";
            active_weapon_.muzzle_velocity = 830.0f;
            active_weapon_.rounds_per_minute = 150.0f;
            active_weapon_.clip_capacity = 10;
            active_weapon_.recoil_pitch = 3.5f;
            active_weapon_.recoil_yaw = 1.0f;
            active_weapon_.base_spread = 0.002f;
            active_weapon_.damage = 95.0f;
            active_weapon_.is_automatic = false;
            reserve_ammo_ = 30;
            break;
        case 5:
        default:
            active_weapon_.name = "Combat Knife";
            active_weapon_.muzzle_velocity = 50.0f;
            active_weapon_.rounds_per_minute = 100.0f;
            active_weapon_.clip_capacity = 1;
            active_weapon_.recoil_pitch = 0.1f;
            active_weapon_.recoil_yaw = 0.1f;
            active_weapon_.base_spread = 0.0f;
            active_weapon_.damage = 100.0f;
            active_weapon_.is_automatic = false;
            reserve_ammo_ = 1;
            break;
    }

    current_clip_ammo_ = active_weapon_.clip_capacity;
    shot_cooldown_ = 0.0;
    is_reloading_ = false;
    reload_timer_ = 0.0;
}

bool WeaponSystem::TryFire(const glm::vec3& muzzle_pos, const glm::vec3& aim_dir, BulletTrace& out_trace) {
    if (is_reloading_ || shot_cooldown_ > 0.0 || current_clip_ammo_ == 0) {
        return false;
    }

    current_clip_ammo_--;
    shot_cooldown_ = 60.0 / active_weapon_.rounds_per_minute;

    // Simulate ballistic bullet raycast trace
    out_trace.origin = muzzle_pos;
    out_trace.direction = aim_dir;
    out_trace.damage = active_weapon_.damage;
    out_trace.hit = true;
    out_trace.distance = 500.0f; // Max effective range in meters
    out_trace.hit_position = muzzle_pos + aim_dir * out_trace.distance;

    return true;
}

void WeaponSystem::Reload() {
    if (is_reloading_ || current_clip_ammo_ >= active_weapon_.clip_capacity || reserve_ammo_ == 0) {
        return;
    }
    is_reloading_ = true;
    reload_timer_ = 2.2; // 2.2 second reload animation time
}

void WeaponSystem::Update(double delta_seconds) {
    if (shot_cooldown_ > 0.0) {
        shot_cooldown_ = std::max(0.0, shot_cooldown_ - delta_seconds);
    }

    if (is_reloading_) {
        reload_timer_ -= delta_seconds;
        if (reload_timer_ <= 0.0) {
            uint32_t needed = active_weapon_.clip_capacity - current_clip_ammo_;
            uint32_t transferred = std::min(needed, reserve_ammo_);
            reserve_ammo_ -= transferred;
            current_clip_ammo_ += transferred;
            is_reloading_ = false;
        }
    }
}

} // namespace igi
