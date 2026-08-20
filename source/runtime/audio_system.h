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
    static void Play(SoundEffect sfx);
    static void PlayWavFile(const std::string& path);

private:
    static std::filesystem::path FindExistingPath(const std::string& relative_path);
    static std::string game_root_;
};

} // namespace igi
