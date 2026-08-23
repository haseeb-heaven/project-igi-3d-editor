// Unit tests for the retail magic-object table (#67) — open-igi MagicObjectRegistry.cs
// semantics: DefineMagicObj("name", "model", TASKTYPE_X) lines from decompiled
// magicobj.qsc; first-match lookup; ids allocated in first-seen order.
#include <gtest/gtest.h>
#include "../source/renderer/magic_object_registry.h"

using igi::MagicObjectDefinition;
using igi::MagicObjectRegistry;

namespace {
const char* kSampleQsc =
    "Function DefineMagicObj(\"400_03_1\", \"400_03_1\", TASKTYPE_STATIC)\n"
    "Function DefineMagicObj(\"barrel01\", \"412_01_2\", TASKTYPE_CONTAINER)\n"
    "Function DefineMagicObj(\"lamp\", \"400_03_1\", TASKTYPE_STATIC)\n";
} // namespace

TEST(MagicObjectRegistryTest, ParsesDefineMagicObjLines) {
    MagicObjectRegistry& reg = MagicObjectRegistry::Get();
    reg.Clear();
    const int added = reg.LoadFromDecompiledText(kSampleQsc);
    EXPECT_EQ(added, 3);
    EXPECT_EQ(reg.Count(), 3);
}

TEST(MagicObjectRegistryTest, LookupReturnsDefinedModelAndTaskType) {
    MagicObjectRegistry& reg = MagicObjectRegistry::Get();
    reg.Clear();
    reg.LoadFromDecompiledText(kSampleQsc);

    MagicObjectDefinition def;
    ASSERT_TRUE(reg.TryGet("barrel01", def));
    EXPECT_EQ(def.model, "412_01_2"); // model differs from name — the whole point
    EXPECT_EQ(def.task_type_name, "TASKTYPE_CONTAINER");
}

TEST(MagicObjectRegistryTest, LookupIsCaseInsensitive) {
    MagicObjectRegistry& reg = MagicObjectRegistry::Get();
    reg.Clear();
    reg.LoadFromDecompiledText(kSampleQsc);
    MagicObjectDefinition def;
    EXPECT_TRUE(reg.TryGet("BARREL01", def));
    EXPECT_EQ(def.model, "412_01_2");
}

TEST(MagicObjectRegistryTest, UnknownNameMisses) {
    MagicObjectRegistry& reg = MagicObjectRegistry::Get();
    reg.Clear();
    reg.LoadFromDecompiledText(kSampleQsc);
    MagicObjectDefinition def;
    EXPECT_FALSE(reg.TryGet("not_in_table", def));
}

TEST(MagicObjectRegistryTest, TaskTypeIdsAllocatedInFirstSeenOrder) {
    MagicObjectRegistry& reg = MagicObjectRegistry::Get();
    reg.Clear();
    reg.LoadFromDecompiledText(kSampleQsc);
    // STATIC seen first (id 0), CONTAINER second (id 1).
    EXPECT_EQ(reg.TaskTypeName(0), "TASKTYPE_STATIC");
    EXPECT_EQ(reg.TaskTypeName(1), "TASKTYPE_CONTAINER");
}
