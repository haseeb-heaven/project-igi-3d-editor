#include "audio_system.h"
#include <filesystem>
#include <algorithm>
#include <vector>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")
#endif

namespace igi {

std::string AudioSystem::game_root_;
int AudioSystem::active_level_number_ = 1;

void AudioSystem::Initialize(const std::string& game_root) {
    game_root_ = game_root;
    active_level_number_ = 1;
}

void AudioSystem::SetActiveLevel(int level_number) {
    active_level_number_ = std::max(1, level_number);
}

std::filesystem::path AudioSystem::FindExistingPath(const std::string& relative_path) {
    const std::filesystem::path requested_path(relative_path);
    if (requested_path.is_absolute() && std::filesystem::exists(requested_path)) {
        return requested_path;
    }

    std::vector<std::filesystem::path> candidates;
    if (!game_root_.empty()) {
        candidates.emplace_back(std::filesystem::path(game_root_) / requested_path);
    }
    candidates.emplace_back(requested_path);
#if defined(_WIN32)
    candidates.emplace_back(std::filesystem::path("D:\\IGI1") / requested_path);
#endif

    for (const auto& candidate : candidates) {
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
    }
    return {};
}

void AudioSystem::PlayWavFile(const std::string& path) {
#if defined(_WIN32)
    const std::filesystem::path resolved_path = FindExistingPath(path);
    if (!resolved_path.empty()) {
        const std::string native_path = resolved_path.string();
        PlaySoundA(native_path.c_str(), NULL, SND_ASYNC | SND_FILENAME | SND_NODEFAULT);
    }
#else
    (void)path;
#endif
}

void AudioSystem::PlayWeaponFire(
    const std::string& authored_sound,
    SoundEffect fallback) {
    if (authored_sound.empty()) {
        Play(fallback);
        return;
    }

    std::string sound_filename = authored_sound;
    if (sound_filename.size() < 4 ||
        sound_filename.substr(sound_filename.size() - 4) != ".wav") {
        sound_filename += ".wav";
    }

    const std::string level_sound_directory =
        "missions/location0/level" + std::to_string(active_level_number_) +
        "/sounds/";
    const std::vector<std::string> candidate_paths = {
        level_sound_directory + sound_filename,
        "sounds/" + sound_filename,
        sound_filename,
    };
    for (const std::string& candidate_path : candidate_paths) {
        const std::filesystem::path resolved_path = FindExistingPath(candidate_path);
        if (!resolved_path.empty()) {
            PlayWavFile(resolved_path.string());
            return;
        }
    }

    // Missing authored sound assets are expected in headless/editor setups;
    // retain an audible Windows fallback instead of dropping the fire event.
    Play(fallback);
}

void AudioSystem::Play(SoundEffect sfx) {
    std::string relative_path;
    switch (sfx) {
        case SoundEffect::Gunshot:
            relative_path = "missions/location0/level10/sounds/m10o_ak1.wav";
            break;
        case SoundEffect::BulletImpact:
        case SoundEffect::Pain:
            relative_path = "Assets/sounds/bullet-impact.wav";
            break;
        case SoundEffect::Jump:
        case SoundEffect::Footstep:
        case SoundEffect::Reload:
        case SoundEffect::ObjectiveComplete:
        case SoundEffect::ProjectileLaunch:
            relative_path = "missions/location0/level1/sounds/m1_beeps01.wav";
            break;
        case SoundEffect::Explosion:
        case SoundEffect::Flashbang:
            relative_path = "missions/location0/level1/sounds/m1_beeps01.wav";
            break;
    }

    std::filesystem::path resolved_path = FindExistingPath(relative_path);
    if (resolved_path.empty() &&
        (sfx == SoundEffect::BulletImpact || sfx == SoundEffect::Pain)) {
        // The vanilla install does not expose a shared impact WAV at a stable
        // path; retain an audible mission-sound fallback instead of silently
        // dropping the event.
        resolved_path = FindExistingPath(
            "missions/location0/level1/sounds/m1_beeps01.wav");
    }
    if (!resolved_path.empty()) {
        PlayWavFile(resolved_path.string());
    } else {
#if defined(_WIN32)
        MessageBeep(MB_OK);
#endif
    }
}

} // namespace igi
