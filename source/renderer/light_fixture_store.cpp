#include "light_fixture_store.h"

namespace igi {

LightFixtureStore& LightFixtureStore::Get() {
    static LightFixtureStore s_instance;
    return s_instance;
}

void LightFixtureStore::SetFixtures(std::vector<LightFixture> fixtures) {
    fixtures_ = std::move(fixtures);
    // Re-apply reviewer decisions: same texture at the same place = same proposal.
    for (auto& f : fixtures_) {
        auto it = ignored_by_key_.find(f.IgnoreKey());
        f.ignored = (it != ignored_by_key_.end()) && it->second;
    }
}

void LightFixtureStore::SetIgnored(size_t index, bool ignored) {
    if (index >= fixtures_.size()) return;
    fixtures_[index].ignored = ignored;
    ignored_by_key_[fixtures_[index].IgnoreKey()] = ignored;
}

bool LightFixtureStore::IsIgnored(size_t index) const {
    return index < fixtures_.size() && fixtures_[index].ignored;
}

int LightFixtureStore::ActiveCount() const {
    int n = 0;
    for (const auto& f : fixtures_) {
        if (!f.ignored) ++n;
    }
    return n;
}

void LightFixtureStore::Clear() {
    fixtures_.clear();
    level_no_ = -1;
    // ignored_by_key_ intentionally survives Clear(): flags are reviewer decisions about
    // places in a level, not properties of one extraction run.
}

} // namespace igi
