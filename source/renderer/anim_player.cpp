#include "anim_player.h"

#include "../animation.h"
#include <algorithm>
#include <cmath>

namespace igi {

// ── Translation sampler — port of BoneTrackSampler.cs (0x4D4840) ────────────

namespace {

// Linear scan for the first key at or after the time — the original's loop, and
// both edges (exact hit; loop variable left at count) matter below.
size_t FirstKeyAtOrAfter(const std::vector<AnimTransKey>& keys, float time) {
    size_t index = 0;
    while (index < keys.size() && keys[index].time < time) ++index;
    return index;
}

} // namespace

glm::vec3 AnimTranslationSampler::Sample(const AnimTranslationTrack& track,
                                         float time, bool spline) {
    const auto& keys = track.keys;
    if (keys.empty()) {
        return glm::vec3(0.f); // empty track -> zero vector
    }

    const size_t index = FirstKeyAtOrAfter(keys, time);

    if (index == keys.size()) {
        // Past the last key: hold it. Also what a one-key track always does.
        return keys.back().position;
    }
    if (index == 0 || keys[index].time == time) {
        // Exact hit takes the key untouched, with no interpolation at all.
        return keys[index].position;
    }

    const AnimTransKey& previous = keys[index - 1];
    const AnimTransKey& next = keys[index];
    const float duration = next.time - previous.time;
    if (duration <= 0.f) {
        return next.position;
    }

    const float s = 1.f - ((next.time - time) / duration);

    if (!spline) {
        return glm::mix(previous.position, next.position, s);
    }

    // Standard cubic Hermite basis. Tangents are stored per unit time, so each
    // is scaled by the segment duration before use — which is why a long
    // segment does not overshoot.
    const float s2 = s * s;
    const float s3 = s2 * s;
    const float h00 = (2.f * s3) - (3.f * s2) + 1.f;
    const float h01 = (3.f * s2) - (2.f * s3);
    const float h10 = s - (2.f * s2) + s3;
    const float h11 = s2 - s3;

    return previous.position * h00 +
           next.position * h01 +
           previous.out_tangent * (duration * h10) +
           next.in_tangent * (duration * h11);
}

// ── Rotation sampler — port of BoneRotationSampler.cs (0x4D5180) ────────────

glm::quat AnimRotationSampler::Slerp(const glm::quat& from, const glm::quat& to_in, float s) {
    glm::quat to = to_in;
    float dot = glm::dot(from, to);

    // The baked table signals the shorter arc by storing the angle plus a full
    // turn; computed here it is just the sign of the dot product.
    if (dot < 0.f) {
        to = -to;
        dot = -dot;
    }

    const float angle = std::acos(std::clamp(dot, -1.f, 1.f));
    if (angle == 0.f) {
        return from;
    }

    float from_weight;
    float to_weight;
    if (angle >= kMinimumSlerpAngle) {
        const float inverse_sine = 1.f / std::sin(angle);
        from_weight = std::sin(angle - (angle * s)) * inverse_sine;
        to_weight = std::sin(angle * s) * inverse_sine;
    } else {
        from_weight = 1.f - s;
        to_weight = s;
    }

    return glm::quat(
        (from.w * from_weight) + (to.w * to_weight),
        (from.x * from_weight) + (to.x * to_weight),
        (from.y * from_weight) + (to.y * to_weight),
        (from.z * from_weight) + (to.z * to_weight));
}

glm::quat AnimRotationSampler::Blend(const glm::quat& first, const glm::quat& second, float weight) {
    if (weight <= kBlendLowerCutoff) {
        return second;
    }
    if (weight >= kBlendUpperCutoff) {
        return first;
    }
    return Slerp(first, second, 1.f - weight);
}

glm::quat AnimRotationSampler::Sample(const AnimRotationTrack& track, float time,
                                      float duration, bool cyclic, bool squad) {
    const auto& keys = track.keys;
    if (keys.empty()) {
        return glm::quat(1.f, 0.f, 0.f, 0.f); // identity for an empty track
    }
    if (keys.size() == 1) {
        return keys[0].rotation;
    }

    size_t index = 0;
    while (index < keys.size() && keys[index].time < time) ++index;

    if (index < keys.size() && keys[index].time == time) {
        return keys[index].rotation;
    }

    size_t from;
    size_t to;
    float s;

    if (index == keys.size()) {
        const float last_time = keys.back().time;
        if (!cyclic || duration <= last_time) {
            return keys.back().rotation;
        }
        // Cyclic clip bridges last key -> key 0 over whatever time remains
        // before the duration (why the two need not be equal in the data).
        from = keys.size() - 1;
        to = 0;
        s = (time - last_time) / (duration - last_time);
    } else if (index == 0) {
        return keys[0].rotation;
    } else {
        from = index - 1;
        to = index;
        const float span = keys[to].time - keys[from].time;
        if (span <= 0.f) {
            return keys[to].rotation;
        }
        s = 1.f - ((keys[to].time - time) / span);
    }

    const glm::quat rotation = Slerp(keys[from].rotation, keys[to].rotation, s);
    if (!squad) {
        return rotation;
    }

    // SQUAD: the key slerp and the tangent slerp, blended by 2s(1-s).
    const glm::quat tangent = Slerp(keys[from].out_tangent, keys[to].in_tangent, s);
    return Blend(rotation, tangent, 1.f - (2.f * s * (1.f - s)));
}

// ── Clip clock — port of BoneClip.cs Tick (transcribed from 0x4D4B60) ───────

AnimClipClock::AnimClipClock(float duration, bool loops, bool backwards)
    : duration_(duration),
      loops_(loops),
      backwards_(backwards),
      rate_(backwards ? -kAnimFrameStep : kAnimFrameStep),
      time_(backwards ? duration : 0.f) {}

void AnimClipClock::Tick() {
    const float before_time = time_;
    const bool playing_backwards = rate_ < 0.f || (rate_ == 0.f && backwards_);

    time_ += rate_;
    ended_this_tick_ = false;
    bool wrapped = false;

    if ((!playing_backwards && time_ >= duration_) ||
        (playing_backwards && time_ <= 0.f)) {
        ended_ = true;
        ended_this_tick_ = true;
        if (loops_) {
            // Adjust by ONE duration rather than taking a remainder — the
            // original does not guard rates longer than the clip either.
            time_ += playing_backwards ? duration_ : -duration_;
            wrapped = true;
        } else {
            time_ = playing_backwards ? 0.f : duration_;
        }
    }

    interval_start_ = before_time;
    interval_end_ = time_;
    interval_backwards_ = playing_backwards;
    interval_wrapped_ = wrapped;
    has_interval_ = rate_ != 0.f;
}

bool AnimClipClock::CrossedEvent(int event_id, const std::vector<AnimClipEvent>& events) const {
    if (!has_interval_) {
        return false;
    }
    for (const AnimClipEvent& e : events) {
        if (e.event_id != event_id) {
            continue;
        }
        const float t = e.time;
        bool crossed;
        if (interval_backwards_) {
            crossed = interval_wrapped_
                ? (t < interval_start_ || t >= interval_end_)
                : (t < interval_start_ && t >= interval_end_);
        } else {
            crossed = interval_wrapped_
                ? (t > interval_start_ || t <= interval_end_)
                : (t > interval_start_ && t <= interval_end_);
        }
        if (crossed) {
            return true;
        }
    }
    return false;
}

// ── Skinning — port of MefSkinner.cs (as 0x49B700 does) ─────────────────────

AnimSkinResult SkinMesh(const std::vector<AnimSkinBase>& base_vertices,
                        const std::vector<AnimSkinExtra>& extras,
                        const AnimSkinPalette& palette) {
    AnimSkinResult result;
    const int base_count = static_cast<int>(base_vertices.size());
    if (base_count > kAnimMaximumVertices) {
        result.written = -1; // "Bone model too large" is fatal in the original
        return result;
    }
    for (const AnimSkinBase& v : base_vertices) {
        if (v.bone_index < 0 || v.bone_index >= static_cast<int>(palette.rotations.size())) {
            result.written = -1; // rig names a bone the pose lacks
            return result;
        }
    }

    result.vertices.assign(static_cast<size_t>(base_count), AnimSkinnedVertex{});

    // 1. Base vertices: placed by their one bone, then scaled by their own
    //    trailing float AFTER the bone transform. Normals rotate but are never
    //    translated or scaled.
    for (int i = 0; i < base_count; ++i) {
        const AnimSkinBase& v = base_vertices[static_cast<size_t>(i)];
        const glm::mat3& rotation = palette.rotations[static_cast<size_t>(v.bone_index)];
        const glm::vec3& translation = palette.positions[static_cast<size_t>(v.bone_index)];

        const glm::vec3 position = (rotation * v.position + translation) * v.scale;
        AnimSkinnedVertex& out = result.vertices[static_cast<size_t>(i)];
        out.position = position;
        out.normal = rotation * v.normal;
    }

    // 2. Extra influences: each names a base vertex it adds to, its own bone,
    //    and a weight. No scale, and only the position accumulates.
    for (const AnimSkinExtra& extra : extras) {
        if (extra.target_vertex < 0 || extra.target_vertex >= base_count) {
            continue;
        }
        if (extra.bone_index < 0 || extra.bone_index >= static_cast<int>(palette.rotations.size())) {
            continue;
        }
        const glm::mat3& rotation = palette.rotations[static_cast<size_t>(extra.bone_index)];
        const glm::vec3& translation = palette.positions[static_cast<size_t>(extra.bone_index)];
        const glm::vec3 contribution = rotation * extra.position + translation;

        AnimSkinnedVertex& target = result.vertices[static_cast<size_t>(extra.target_vertex)];
        target.position += extra.weight * contribution;
        // Normal intentionally untouched.
    }

    result.written = base_count;
    return result;
}

// ── Editor .BEF clip adapters ──────────────────────────────────────────────

AnimRotationTrack MakeRotationTrack(const AnimationClip& clip, int bone) {
    AnimRotationTrack track;
    for (const AnimRotationKey& k : clip.rotationKeys) {
        if (k.bone != bone) continue;
        track.keys.push_back({k.q0, static_cast<float>(k.time_ms), k.q1, k.q2});
    }
    std::sort(track.keys.begin(), track.keys.end(),
              [](const AnimRotKey& a, const AnimRotKey& b) { return a.time < b.time; });
    return track;
}

AnimTranslationTrack MakeTranslationTrack(const AnimationClip& clip) {
    AnimTranslationTrack track;
    for (const AnimTranslationKey& k : clip.translationKeys) {
        // .BEF translation keys carry no Hermite tangents — zero, matching the
        // one-key retail convention for tracks with nothing to interpolate.
        track.keys.push_back({k.pos, static_cast<float>(k.time_ms), glm::vec3(0.f), glm::vec3(0.f)});
    }
    std::sort(track.keys.begin(), track.keys.end(),
              [](const AnimTransKey& a, const AnimTransKey& b) { return a.time < b.time; });
    return track;
}

// ── Playback controller ─────────────────────────────────────────────────────

AnimMoveState AnimPlayerController::StateFromSpeed(float speed_units_per_sec,
                                                   float walk_max, float run_min) {
    if (speed_units_per_sec <= walk_max) return AnimMoveState::Idle;
    if (speed_units_per_sec >= run_min) return AnimMoveState::Run;
    return AnimMoveState::Walk;
}

void AnimPlayerController::SetClips(const AnimRotationTrack* idle,
                                    const AnimRotationTrack* walk,
                                    const AnimRotationTrack* run, float duration) {
    clips_[0] = idle;
    clips_[1] = walk;
    clips_[2] = run;
    duration_ = duration;
    clock_.reset();
}

int AnimPlayerController::ActiveClip() const {
    if (manual_override_ >= 0 && manual_override_ <= 2) {
        return manual_override_;
    }
    switch (state_) {
        case AnimMoveState::Walk: return 1;
        case AnimMoveState::Run:  return 2;
        default:                  return 0;
    }
}

void AnimPlayerController::SetManualOverride(int clip_index) {
    manual_override_ = clip_index;
    clock_.reset(); // restart the new clip's clock at its start
}

void AnimPlayerController::SetMovementState(AnimMoveState state) {
    if (state != state_) {
        state_ = state;
        if (manual_override_ < 0) {
            clock_.reset(); // state change switches clips -> fresh clock
        }
    }
}

void AnimPlayerController::Play() {
    if (!clock_) {
        const AnimRotationTrack* active = clips_[ActiveClip()];
        if (!active) return;
        clock_ = std::make_unique<AnimClipClock>(duration_, /*loops=*/true);
    }
    playing_ = true;
}

void AnimPlayerController::Pause() { playing_ = false; }
void AnimPlayerController::SetPaused(bool paused) { playing_ = !paused; }

void AnimPlayerController::SetSpeed(float speed) { speed_ = speed; }

void AnimPlayerController::Tick() {
    if (!playing_) return;
    Play(); // ensure a clock exists for the active clip
    const float steps = speed_; // whole ticks per call; fractional rates via repeated calls
    for (float i = 0; i < steps; i += 1.f) {
        clock_->Tick();
    }
}

void AnimPlayerController::FrameStepForward() {
    Play();
    clock_->SkipTo(clock_->Time() + kAnimFrameStep * speed_);
}

void AnimPlayerController::FrameStepBackward() {
    Play();
    clock_->SkipTo(clock_->Time() - kAnimFrameStep * speed_);
}

void AnimPlayerController::Reset() {
    clock_.reset();
    playing_ = false;
}

} // namespace igi
