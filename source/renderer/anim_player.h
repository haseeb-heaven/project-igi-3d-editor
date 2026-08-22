#pragma once
// ── Retail-exact animation playback (issue #73) ─────────────────────────────
// C++ port of open-igi's igi2.pdb-derived animation pipeline:
//   * BoneTrackSampler.cs   — translation track sampling reproducing 0x4D4840
//                             (cubic Hermite vs plain lerp on a global toggle;
//                             tangents stored per unit time, scaled by the
//                             segment duration before use).
//   * BoneRotationSampler.cs— rotation track sampling reproducing 0x4D5180:
//                             plain slerp (SPLINE off) or SQUAD (key slerp and
//                             tangent slerp blended by 2s(1-s)) with the
//                             original's angle/blend thresholds.
//   * BoneClip.cs           — clip clock transcribed from 0x4D4B60: rate of
//                             one authoring frame step (160 units) per tick,
//                             loop adjusts by ONE duration (no remainder
//                             guard — retail-faithful), root-motion DELTA
//                             (not absolute sample) moves the body, seam-
//                             split delta across a loop wrap, interval-based
//                             event crossing.
//   * MefSkinner.cs         — CPU skinning as 0x49B700 does: one bone per
//                             base vertex plus weighted extra influences; a
//                             base vertex's own scale float applies AFTER the
//                             bone transform; normals are rotated but never
//                             translated or scaled; extra influences add to
//                             position only.
//
// Cyclic flag: BoneAnimation.CyclicFlag = 0x80000000 (BOAH+4 bit 31, tested at
// 0x4D5295) — a track past its last key bridges back to key 0 over the time
// remaining before the clip duration instead of holding.
//
// Editor integration: the existing AnimationRegistry parses .BEF clips whose
// AnimRotationKey carries {q0=value, q1=outTangent, q2=inTangent} and integer
// millisecond times. makeTrack() below converts those into the float-time
// tracks these samplers consume, so the F10 panel can drive retail-quality
// playback without changing the parser (hookup documented in anim_player.cpp).

#include "../animation.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cstdint>
#include <memory>
#include <vector>

namespace igi {

// ── Track data ──────────────────────────────────────────────────────────────

struct AnimRotKey {
    glm::quat rotation{1.f, 0.f, 0.f, 0.f};
    float     time = 0.f;      // abstract units (editor .BEF clips: milliseconds)
    glm::quat out_tangent{1.f, 0.f, 0.f, 0.f}; // segment STARTING at this key
    glm::quat in_tangent{1.f, 0.f, 0.f, 0.f};  // segment ENDING at this key
};

struct AnimTransKey {
    glm::vec3 position{0.f};
    float     time = 0.f;
    glm::vec3 out_tangent{0.f}; // m0 of the Hermite basis, per unit time
    glm::vec3 in_tangent{0.f};  // m1
};

struct AnimClipEvent {
    int       event_id = 0;
    int       bone_index = -1;
    float     time = 0.f;
    glm::vec3 position{0.f};
};

// The authoring step durations are quantised to (BoneAnimation.FrameStep).
inline constexpr float kAnimFrameStep = 160.f;

// BoneAnimation.CyclicFlag — BOAH+4 bit 31, tested at 0x4D5295.
inline constexpr uint32_t kAnimCyclicFlag = 0x80000000u;

// MefSkinner.MaximumVertices — "Bone model too large" is fatal in the original.
inline constexpr int kAnimMaximumVertices = 4000;

struct AnimRotationTrack {
    std::vector<AnimRotKey> keys;
};

struct AnimTranslationTrack {
    std::vector<AnimTransKey> keys;
};

// ── Translation sampler (BoneTrackSampler.cs, 0x4D4840) ────────────────────

struct AnimTranslationSampler {
    // Samples a translation track. `spline` selects the Hermite arm (true) or
    // the linear one — the original decides on a global toggle (dword_A54678).
    // Empty track -> zero vector; past the last key holds it; an exact key hit
    // returns that key untouched with no interpolation.
    static glm::vec3 Sample(const AnimTranslationTrack& track, float time, bool spline = true);
};

// ── Rotation sampler (BoneRotationSampler.cs, 0x4D5180) ────────────────────

struct AnimRotationSampler {
    // Angle below which the original abandons slerp for a lerp.
    static constexpr float kMinimumSlerpAngle = 0.0099999998f;
    // 0x4D4500 blend cutoffs guarding numerically-degenerate SQUAD parameters.
    static constexpr float kBlendLowerCutoff = 0.001f;
    static constexpr float kBlendUpperCutoff = 0.99900001f;

    // `cyclic`: BOAH bit 31 — past the last key, bridge back to key 0 over the
    // time remaining before `duration`. `squad`: false = plain slerp.
    static glm::quat Sample(const AnimRotationTrack& track, float time,
                            float duration, bool cyclic = false, bool squad = true);

    // Slerp with the original's near-zero-angle fallback and shorter-arc negate.
    static glm::quat Slerp(const glm::quat& from, const glm::quat& to, float s);

    // SQUAD outer blend (0x4D4500): cutoffs return an endpoint whole.
    static glm::quat Blend(const glm::quat& first, const glm::quat& second, float weight);
};

// ── Clip clock (BoneClip.cs, tick transcribed from 0x4D4B60) ───────────────

class AnimClipClock {
public:
    // `backwards` plays toward zero; rate is one frame step per tick, negated
    // when backwards (retail stores it signed and uses the flag only to pick
    // which end counts as the end).
    explicit AnimClipClock(float duration, bool loops = true, bool backwards = false);

    // Advance by one tick: time += rate; on crossing the end, set ended and
    // either adjust by ONE duration (looping) or clamp to the end. Neither
    // guards rates longer than the clip — neither did the original.
    void Tick();

    // sub_4D65B0: changes WHERE the clip plays from, never how fast.
    void SkipTo(float time) { time_ = time; }

    // True when the last tick crossed an authored event of `event_id`.
    // Interval open at the old clock, closed at the new; wrapping through zero
    // fires once; reverse playback mirrors the rule.
    bool CrossedEvent(int event_id, const std::vector<AnimClipEvent>& events) const;

    // Per-tick root-motion DELTA — the delta (not the absolute sampled root) is
    // what moves a body in the retail engine (clip instance +16/+28).
    const glm::vec3& RootDelta() const { return root_delta_; }

    float Time() const { return time_; }
    float Duration() const { return duration_; }
    float Rate() const { return rate_; }
    void  SetRate(float rate) { rate_ = rate; }
    bool  Loops() const { return loops_; }
    bool  Backwards() const { return backwards_; }
    bool  Ended() const { return ended_; }
    bool  EndedThisTick() const { return ended_this_tick_; }

private:
    float duration_;
    bool  loops_;
    bool  backwards_;
    float rate_;
    float time_ = 0.f;
    bool  ended_ = false;
    bool  ended_this_tick_ = false;
    glm::vec3 root_delta_{0.f};
    // Event-interval bookkeeping (last advanced interval).
    float interval_start_ = 0.f, interval_end_ = 0.f;
    bool  interval_backwards_ = false, interval_wrapped_ = false, has_interval_ = false;
};

// ── Skinning (MefSkinner.cs, as 0x49B700 does) ─────────────────────────────

// One base-vertex binding: the rig is one bone per vertex plus a list of extra
// weighted influences (NOT four-weights-per-vertex). `scale` is the trailing
// f32 at +32 of the 40-byte influence record.
struct AnimSkinBase {
    glm::vec3 position{0.f};
    glm::vec3 normal{0.f};
    int   bone_index = 0;
    float scale = 1.f;
};

// Extra influence: same 40 bytes read differently — weight/target/bone. Adds to
// POSITION only, never scaled, never touches the normal.
struct AnimSkinExtra {
    glm::vec3 position{0.f};
    int   target_vertex = -1;
    int   bone_index = 0;
    float weight = 0.f;
};

// Posed skeleton palette (object space): rotations apply M*v (rows dotted with
// the source vector — the convention BonePose places bones with).
struct AnimSkinPalette {
    std::vector<glm::mat3> rotations;
    std::vector<glm::vec3> positions;
};

struct AnimSkinnedVertex {
    glm::vec3 position{0.f};
    glm::vec3 normal{0.f};
};

struct AnimSkinResult {
    // Number of vertices written (= base count). -1 when the rig names a bone
    // the palette lacks, or when vertex_count exceeds kAnimMaximumVertices.
    int written = 0;
    std::vector<AnimSkinnedVertex> vertices;
};

// Skin a skinned model's base vertices against a posed palette.
// Hookup: mef_native's ParseRenderVertices fills per-vertex boneIndex for type-1
// models and hardcoded_bones.h names the palette; feed base bindings from the
// parsed vertices (scale defaults 1.0 until the +32 influence float is parsed)
// and extras from MEF skin-influence records when present.
AnimSkinResult SkinMesh(const std::vector<AnimSkinBase>& base_vertices,
                        const std::vector<AnimSkinExtra>& extras,
                        const AnimSkinPalette& palette);

// ── Editor .BEF clip adapters ───────────────────────────────────────────────

// Build a retail-faith rotation track for `bone` from an editor-parsed clip.
// AnimRotationKey stores {q0 = value, q1 = outTangent, q2 = inTangent} at
// integer millisecond times; times pass through as float ms unchanged.
AnimRotationTrack MakeRotationTrack(const AnimationClip& clip, int bone);

// Build the root-motion translation track (tangents zero — .BEF stores none).
AnimTranslationTrack MakeTranslationTrack(const AnimationClip& clip);

// ── Playback controller (F10-panel API; UI hookup documented, not built) ───

enum class AnimMoveState { Idle, Walk, Run };

class AnimPlayerController {
public:
    // Movement-state clip selection. Thresholds in units/sec; walk upper bound
    // is a documented editor default (tunable), idle below it, run above.
    static AnimMoveState StateFromSpeed(float speed_units_per_sec,
                                        float walk_max = 4000.f, float run_min = 9000.f);

    void SetClips(const AnimRotationTrack* idle, const AnimRotationTrack* walk,
                  const AnimRotationTrack* run, float duration);
    void SetManualOverride(int clip_index); // -1 = follow movement state
    void SetMovementState(AnimMoveState state);

    void Play();
    void Pause();
    void SetPaused(bool paused);
    void SetSpeed(float speed);        // multiplies the per-tick frame-step rate
    void FrameStepForward();           // advance exactly one authoring frame
    void FrameStepBackward();
    void Tick();                       // advance the active clip by one tick
    void Reset();

    bool IsPlaying() const { return playing_; }
    float Speed() const { return speed_; }
    float Time() const { return clock_ ? clock_->Time() : 0.f; }
    float Duration() const { return duration_; }
    AnimMoveState State() const { return state_; }
    int ActiveClip() const;            // 0=idle 1=walk 2=run (after override)

    const AnimClipClock* Clock() const { return clock_.get(); }

private:
    const AnimRotationTrack* clips_[3] = {nullptr, nullptr, nullptr};
    float duration_ = 0.f;
    int   manual_override_ = -1;
    AnimMoveState state_ = AnimMoveState::Idle;
    bool  playing_ = false;
    float speed_ = 1.f;
    std::unique_ptr<AnimClipClock> clock_;
};

} // namespace igi
