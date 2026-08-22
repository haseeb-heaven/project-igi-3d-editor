#pragma once
#include <map>
#include <string>
#include <vector>
#include "light_fixture.h"

// Reviewer store for proposed light fixtures (open-igi LightFixtureStore equivalent).
// Singleton because the fixture list is a per-session review workspace, like the editor's
// other tool caches: extraction repopulates it, reviewer decisions (ignore/false-positive)
// must survive re-extraction of the same level, and there is exactly one viewport.
//
// PERSISTENCE NOTE: reviewer flags are in-memory for this pass. The natural QED-config
// persistence point is the same place QEDWeather*/QED* keys are written (source/config.cpp
// Config struct + Save/Load) — key fixtures by LightFixture::IgnoreKey() and store the set
// of ignored keys as a string list. Deliberately deferred so this pass stays scoped to the
// extractor + store (issue #63 acceptance: "persist per-session").
namespace igi {

class LightFixtureStore {
public:
    static LightFixtureStore& Get();

    // Replaces the workspace with freshly extracted proposals. Reviewer flags from the
    // previous population are re-applied to matching keys (same texture at the same place).
    void SetFixtures(std::vector<LightFixture> fixtures);

    const std::vector<LightFixture>& Fixtures() const { return fixtures_; }

    // Reviewer flow: mark a proposal as a false positive (reflective sign, sun-catch, decal).
    void SetIgnored(size_t index, bool ignored);
    bool IsIgnored(size_t index) const;

    int LevelNo() const { return level_no_; }
    void SetLevelNo(int level_no) { level_no_ = level_no; }

    // Count of non-ignored proposals — what a future bake (#43) would consume.
    int ActiveCount() const;

    void Clear();

private:
    LightFixtureStore() = default;
    std::vector<LightFixture> fixtures_;
    std::map<std::string, bool> ignored_by_key_; // IgnoreKey -> ignored
    int level_no_ = -1;
};

} // namespace igi
