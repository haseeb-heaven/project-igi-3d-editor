#pragma once
// weather_math.h — vanilla-parity weather constants and pure math shared by the
// rain/snow GPU pipeline (renderer_rain.cpp) and the fixture-independent unit
// tests. All constants are ported from open-igi (written from igi2.pdb symbols):
//   apps/OpenIGI.Desktop/Rendering/RainRenderer.cs  (retail-verified rain)
//   apps/OpenIGI.Desktop/Rendering/SnowRenderer.cs  (retail-verified snow)
// Header is self-contained (no pch.h/GL) so tests can include it standalone.

#include <cmath>
#include <algorithm>

namespace igi::weather {

// --- open-igi RainRenderer.cs constants ---
constexpr int   kRainDrops          = 1200;  // DefaultParticleCount
constexpr float kBoxMeters          = 50.0f; // DefaultBoxSizeMeters
constexpr float kRainStreakMeters   = 0.08f; // StreakLengthMeters
constexpr float kRainMinSpeedMul    = 0.08f; // MinimumSpeedMultiplier (fraction of fall band / second)
constexpr float kRainMaxSpeedMul    = 0.18f; // MaximumSpeedMultiplier
constexpr float kRainMaxAlpha       = 0.28f; // MaximumStreakAlpha
constexpr float kRainAlphaBoost     = 1.25f; // authored alpha scale (thin-line friendly)

// --- open-igi SnowRenderer.cs constants ---
constexpr int   kSnowFlakes         = 900;   // DefaultParticleCount
constexpr float kSnowFlakeMeters    = 0.045f;// FlakeSizeMeters
constexpr float kSnowMinSpeedMul    = 0.025f;// MinimumSpeedMultiplier
constexpr float kSnowMaxSpeedMul    = 0.065f;// MaximumSpeedMultiplier
constexpr float kSnowMaxAlpha       = 0.42f; // MaximumAlpha
constexpr float kSnowMinAlpha       = 0.10f; // floor so flakes stay visible on white terrain
constexpr float kSnowAlphaBoost     = 1.75f; // authored alpha scale
constexpr float kSnowDriftMeters    = 0.35f; // DriftMeters (sway amplitude)

// Fall speed as world-meters of the authored band per second, for a seed in
// [0,1]. Mirrors RainRenderer/SnowRenderer.CalculateFallSpeed.
inline float FallSpeed(float fallRangeMeters, float seed, float minMul, float maxMul) {
    float s = std::clamp(seed, 0.0f, 1.0f);
    return (minMul + s * (maxMul - minMul)) * fallRangeMeters;
}

inline float RainFallSpeed(float fallRangeMeters, float seed) {
    return FallSpeed(fallRangeMeters, seed, kRainMinSpeedMul, kRainMaxSpeedMul);
}

inline float SnowFallSpeed(float fallRangeMeters, float seed) {
    return FallSpeed(fallRangeMeters, seed, kSnowMinSpeedMul, kSnowMaxSpeedMul);
}

// Streak/flake alpha from the authored "Rain Alpha" descriptor value.
inline float RainStreakAlpha(float authoredAlpha) {
    return std::clamp(authoredAlpha * kRainAlphaBoost, 0.0f, kRainMaxAlpha);
}

inline float SnowFlakeAlpha(float authoredAlpha) {
    return std::clamp(authoredAlpha * kSnowAlphaBoost, kSnowMinAlpha, kSnowMaxAlpha);
}

// Snow sway offsets in meters (open-igi SnowRenderer.Draw):
//   driftX = sin(seconds * 0.45 + seedX * 17) * DriftMeters
//   driftY = cos(seconds * 0.35 + seedY * 19) * DriftMeters
inline float SnowDriftX(float seconds, float seedX) {
    return std::sin(seconds * 0.45f + seedX * 17.0f) * kSnowDriftMeters;
}

inline float SnowDriftY(float seconds, float seedY) {
    return std::cos(seconds * 0.35f + seedY * 19.0f) * kSnowDriftMeters;
}

// Wrap a value into [0, modulus) (open-igi Mod helper).
inline float Wrap(float value, float modulus) {
    float result = std::fmod(value, modulus);
    return result < 0.0f ? result + modulus : result;
}

} // namespace igi::weather
