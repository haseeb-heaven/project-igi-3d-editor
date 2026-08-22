#pragma once
#include <string>

// Minimal winmm waveOut playback engine for the editor (issue #70) — preview-grade
// counterpart of open-igi's SoundSystem/OpenAlAudioDevice. Windows-first; on
// non-Windows builds every call is a graceful no-op so the rest of the editor is
// unaffected.
//
// Semantics mirrored from open-igi:
//   * master volume as a 0..1 multiplier applied to samples at mix time;
//   * a voice cap of 32 concurrent one-shot sounds (AudioDeviceOptions.VoiceCount
//     default 32) — a play request beyond the cap steals nothing, it just fails;
//   * ambience loops until stopped; only ONE ambience voice exists.

namespace igi {

class EditorAudio {
public:
    static constexpr int kVoiceCap = 32;

    static EditorAudio& Get();

    // Opens a waveOut device at the given rate/width/channels. Safe to call again;
    // reopens only when the format changed. Returns false when no device is
    // available (all other calls then behave as silent no-ops).
    bool Init(int sample_rate, int channels, int bits_per_sample);

    void Shutdown();

    bool IsReady() const;

    // Master volume, clamped to 0..1.
    void SetMasterVolume(float volume);
    float MasterVolume() const { return master_volume_; }

    // Plays a resident sound by bank name (upper-cased bare stem). Returns false when
    // audio is not ready, the name is unknown, or all voices are busy.
    bool PlaySound(const std::string& name);

    // Starts looping an ambience sound; replaces any current ambience voice.
    bool PlayAmbience(const std::string& name);

    void StopAmbience();
    void StopAllVoices();

    // Currently active one-shot voices (diagnostics).
    int ActiveVoiceCount() const;

private:
    EditorAudio() = default;
    struct Impl;
    Impl& Impl_();

    float master_volume_ = 0.8f;
};

} // namespace igi
