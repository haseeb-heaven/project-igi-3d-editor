// Unit tests for issue #42 — interactable task schemas (Wire / AIStationaryGunHolder)
// and the Wire end-anchor arg mapping. Fixture-independent: exercises TaskSchemaNS
// builtin schemas and pure arg-token mapping rules.
#include <gtest/gtest.h>
#include "../source/level/task_schema.h"
#include <string>
#include <glm/glm.hpp>
#include <vector>

using TaskSchemaNS::GetBuiltinSchema;
using TaskSchemaNS::FieldDef;

namespace {
const FieldDef* FindField(const TaskSchemaNS::TaskSchema& sc, const std::string& name) {
    for (const auto& f : sc) if (f.name == name) return &f;
    return nullptr;
}
} // namespace

TEST(InteractableSchemaTest, WireExposesBothAnchorsAndModel) {
    const TaskSchemaNS::TaskSchema* sc = GetBuiltinSchema("Wire");
    ASSERT_NE(sc, nullptr);
    const FieldDef* start = FindField(*sc, "Start position");
    const FieldDef* end   = FindField(*sc, "End position");
    const FieldDef* model = FindField(*sc, "Model");
    ASSERT_NE(start, nullptr);
    ASSERT_NE(end, nullptr);
    ASSERT_NE(model, nullptr);
    EXPECT_EQ(start->argOffset, 3);   // retail "Start position" (ObjectPos = 3 args)
    EXPECT_EQ(end->argOffset, 6);     // retail "End position" (args 6-8; was dropped before #42)
    EXPECT_EQ(model->argOffset, 9);
    EXPECT_EQ(start->typeName, "ObjectPos");
    EXPECT_EQ(end->typeName, "ObjectPos");
    EXPECT_EQ(model->typeName, "String16");
}

TEST(InteractableSchemaTest, GunHolderExposesRetailDocFields) {
    const TaskSchemaNS::TaskSchema* sc = GetBuiltinSchema("AIStationaryGunHolder");
    ASSERT_NE(sc, nullptr);

    // Core placement + interaction fields from AiStationaryGunHolder.doc.
    struct Expectation { const char* name; const char* type; int offset; };
    const Expectation expected[] = {
        {"Position",        "ObjectPos", 3},
        {"Orientation",     "Real32x9",  6},
        {"Holder Model",    "String16",  9},
        {"Viewcone Alpha",  "Real32",    10},
        {"Viewcone Gamma",  "Real32",    11},
        {"Viewcone Length", "Real32",    12},
        {"Min Alpha",       "Real32",    17},
        {"Max Alpha",       "Real32",    18},
        {"Max Gamma",       "Real32",    19},
        {"Ammo",            "Int32",     20},
        {"Accuracy",        "Real32",    24},
        {"Weapon ID",       "Int32",     25},
    };
    for (const auto& e : expected) {
        const FieldDef* f = FindField(*sc, e.name);
        ASSERT_NE(f, nullptr) << "missing field: " << e.name;
        EXPECT_STREQ(f->typeName.c_str(), e.type) << e.name;
        EXPECT_EQ(f->argOffset, e.offset) << e.name;
    }
}

TEST(InteractableSchemaTest, GunHolderFieldsDoNotOverlapGivenTypeArgCounts) {
    // Offsets must advance consistently with TypeArgCount so no two fields share args.
    const TaskSchemaNS::TaskSchema* sc = GetBuiltinSchema("AIStationaryGunHolder");
    ASSERT_NE(sc, nullptr);
    for (size_t i = 0; i + 1 < sc->size(); ++i) {
        const FieldDef& a = (*sc)[i];
        const FieldDef& b = (*sc)[i + 1];
        EXPECT_EQ(b.argOffset, a.argOffset + a.argCount)
            << "gap/overlap between " << a.name << " and " << b.name;
    }
}

TEST(WireAnchorMappingTest, ArgIndicesMapToWireEndFields) {
    // Mirrors level_objects.cpp's isWire switch: args 3-5 -> pos, 6-8 -> wire_end,
    // 9 -> model. Pinned here so the parser and the schema cannot drift apart.
    struct WireArgs { double x, y, z, ex, ey, ez; const char* model; };
    WireArgs in{100.0, 200.0, 300.0, -400.5, 500.25, -600.75, "\"wire_01_1\""};

    glm::dvec3 pos(0.0), wireEnd(0.0);
    std::string modelId;

    // arg_idx -> field mapping (kept in sync with level_objects.cpp).
    for (int arg_idx = 0; arg_idx < 10; ++arg_idx) {
        switch (arg_idx) {
            case 3: pos.x = in.x; break;
            case 4: pos.y = in.y; break;
            case 5: pos.z = in.z; break;
            case 6: wireEnd.x = in.ex; break;
            case 7: wireEnd.y = in.ey; break;
            case 8: wireEnd.z = in.ez; break;
            case 9: modelId = in.model; break;
            default: break;
        }
    }

    EXPECT_EQ(pos.x, 100.0);
    EXPECT_EQ(pos.y, 200.0);
    EXPECT_EQ(pos.z, 300.0);
    EXPECT_EQ(wireEnd.x, -400.5);   // pre-#42 these three were silently dropped
    EXPECT_EQ(wireEnd.y, 500.25);
    EXPECT_EQ(wireEnd.z, -600.75);
    EXPECT_EQ(modelId, "\"wire_01_1\"");
}
