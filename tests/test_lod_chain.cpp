// Unit tests for the LOD virtual-model chain resolution rule (issue #62).
// Reference: open-igi LodModelChain.cs documenting 0x4CED50 (igi2.pdb evidence):
// increment-last-character, stop at first unresolved name or five levels.
#include <gtest/gtest.h>
#include <set>
#include <string>
#include "../source/renderer/lod_model_chain.h"

using igi::IncrementModelNameLastChar;
using igi::ResolveLodChain;
using igi::kLodMaxLevels;

TEST(LodChainTest, IncrementLastCharBasic) {
    EXPECT_EQ(IncrementModelNameLastChar("435_01_1"), "435_01_2");
    EXPECT_EQ(IncrementModelNameLastChar("435_01_4"), "435_01_5");
}

TEST(LodChainTest, IncrementIsCharacterArithmeticNotNumeric) {
    // '9' increments to ':' — the chain simply stops because ':' never resolves.
    EXPECT_EQ(IncrementModelNameLastChar("mesh9"), "mesh:");
    // Non-digit endings work identically.
    EXPECT_EQ(IncrementModelNameLastChar("abc"), "abd");
    // Empty name is returned unchanged rather than crashing.
    EXPECT_EQ(IncrementModelNameLastChar(""), "");
}

TEST(LodChainTest, ChainAlwaysContainsLevelZero) {
    auto chain = ResolveLodChain("435_01_1", [](const std::string&) { return false; });
    ASSERT_EQ(chain.size(), 1u);
    EXPECT_EQ(chain[0], "435_01_1");
}

TEST(LodChainTest, ChainFollowsIncrementsWhileTheyResolve) {
    std::set<std::string> available = {"435_01_1", "435_01_2", "435_01_3"};
    auto chain = ResolveLodChain("435_01_1", [&](const std::string& n) {
        return available.count(n) != 0;
    });
    ASSERT_EQ(chain.size(), 3u);
    EXPECT_EQ(chain[0], "435_01_1");
    EXPECT_EQ(chain[1], "435_01_2");
    EXPECT_EQ(chain[2], "435_01_3");
}

TEST(LodChainTest, ChainCapsAtFiveLevels) {
    // Every conceivable name resolves — the loop bound must still cap at 5.
    auto chain = ResolveLodChain("a", [](const std::string&) { return true; });
    EXPECT_EQ(chain.size(), static_cast<size_t>(kLodMaxLevels));
}

TEST(LodChainTest, GapEndsChainEvenIfLaterLevelsExist) {
    std::set<std::string> available = {"m_1", "m_2", "m_4"}; // m_3 missing
    auto chain = ResolveLodChain("m_1", [&](const std::string& n) {
        return available.count(n) != 0;
    });
    ASSERT_EQ(chain.size(), 2u); // stops at first unresolved name
}
