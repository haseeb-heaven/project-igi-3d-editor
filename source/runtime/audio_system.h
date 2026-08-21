#pragma once
#include <filesystem>
#include <string>

namespace igi {

enum class SoundEffect {
    Gunshot,
    Reload,
    BulletImpact,
    Jump,
    Footstep,
    Pain,
    ObjectiveComplete,
    ProjectileLaunch,
    Explosion,
    Flashbang
};

class AudioSystem {
public:
    static void Initialize(const std::string& game_root = {});
    static void SetActiveLevel(int level_number);
    static void Play(SoundEffect sfx);
    static void PlayWeaponFire(
        const std::string& authored_sound,
        SoundEffect fallback = SoundEffect::Gunshot);
    static void PlayWavFile(const std::string& path);

private:
    static std::filesystem::path FindExistingPath(const std::string& relative_path);
    static std::filesystem::path ResolveSoundPath(const std::string& authored_sound);
    static std::string game_root_;
    static std::filesystem::path audio_cache_directory_;
    static int active_level_number_;
};

} // namespace igi
