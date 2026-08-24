#include <gtest/gtest.h>

#include "mcp/mcp_tools_ai.h"
#include "mcp/mcp_tools_assets.h"
#include "mcp/mcp_tools_graph.h"
#include "mcp/mcp_tools_mission.h"
#include "mcp/mcp_tools_objects.h"

#include <filesystem>
#include <fstream>

namespace {

namespace fs = std::filesystem;

class McpDomainToolsTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = fs::temp_directory_path() / "igi_mcp_domain_tools_test";
        std::error_code error;
        fs::remove_all(root_, error);
        const fs::path level = root_ / "missions/location0/level1";
        fs::create_directories(level, error);
        ASSERT_FALSE(error) << error.message();
        std::ofstream(level / "objects.qvm", std::ios::binary) << "fixture-qvm";
        std::ofstream(level / "objects.qsc", std::ios::binary)
            << "Task_New(100, \"Building\", \"Hangar\", 1, 2, 3, 0, 0, 0, \"300_01_1\");\n"
               "Task_New(101, \"HumanSoldier\", \"Guard\", 4, 5, 6, 0, \"200_01_1\", 0, -1, 0);\n"
               "Task_New(101, \"HumanSoldier\", \"Guard duplicate\", 14, 15, 16, 0, \"200_01_2\", 0, -1, 0);\n"
               "Task_New(-1, \"HumanSoldier\", \"Anonymous guard\", 17, 18, 19, 0, \"200_01_3\", 0, -1, 0);\n"
               "Task_New(-1, \"Building\", \"Anonymous\", 7, 8, 9, 0, 0, 0, \"302_01_1\");\n"
               "Task_New(102, \"UnknownTask\", \"Unknown\", 1, 2, 3, 0, 0, 0, \"303_01_1\");\n"
               "DefineComputerObjective(1, 0, 0, \"TRUE\", \"obj.text\", 100, \"TRUE\", \"FALSE\");\n";
        std::string open_error;
        scope_ = mcp::ProjectScope::Open(root_, open_error);
        ASSERT_TRUE(scope_.has_value()) << open_error;
        service_ = std::make_unique<mcp::GameDataService>(*scope_);
        ASSERT_TRUE(service_->OpenLevel(1, open_error)) << open_error;
    }

    void TearDown() override {
        service_.reset();
        std::error_code error;
        fs::remove_all(root_, error);
    }

    fs::path root_;
    std::optional<mcp::ProjectScope> scope_;
    std::unique_ptr<mcp::GameDataService> service_;
};

TEST_F(McpDomainToolsTest, PersistsTransformAndModelWithoutAcceptingScale) {
    std::string error;
    const auto transformed = mcp::CallObjectTool(
        *service_, "object_set_transform",
        mcp::JsonValue::Object{
            {"task_id", "100"},
            {"position", mcp::JsonValue::Array{10.0, 20.0, 30.0}},
            {"rotation_radians", mcp::JsonValue::Array{0.1, 0.2, 0.3}},
        }, error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_FALSE(transformed.is_null());

    const auto model = mcp::CallObjectTool(
        *service_, "object_set_model",
        mcp::JsonValue::Object{{"task_id", "100"}, {"model_id", "301_01_1"}}, error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_FALSE(model.is_null());

    const auto object = service_->GetObject(1, "100", error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_EQ(object.at("model_id").as_string(), "301_01_1");
    EXPECT_EQ(object.at("position").as_array()[0].as_number(), 10.0);

    const auto scale = mcp::CallObjectTool(
        *service_, "object_set_transform",
        mcp::JsonValue::Object{{"task_id", "100"}, {"scale", 2.0}}, error);
    EXPECT_TRUE(scale.is_null());
    EXPECT_EQ(error, "unsupported_operation");
}

TEST_F(McpDomainToolsTest, DryRunReturnsStagedObjectWithoutWriting) {
    std::string error;
    const auto before = service_->GetObject(1, "100", error);
    ASSERT_TRUE(error.empty()) << error;
    const auto result = mcp::CallObjectTool(
        *service_, "object_set_model",
        mcp::JsonValue::Object{{"task_id", "100"}, {"model_id", "301_01_1"}, {"dry_run", true}}, error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_TRUE(result.at("dry_run").as_bool());
    EXPECT_EQ(result.at("before").at("model_id").as_string(), before.at("model_id").as_string());
    EXPECT_EQ(result.at("after").at("model_id").as_string(), "301_01_1");
    const auto after = service_->GetObject(1, "100", error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_EQ(after.at("model_id").as_string(), before.at("model_id").as_string());
}

TEST_F(McpDomainToolsTest, EnforcesDeclaredStringLength) {
    std::string error;
    const auto result = mcp::CallObjectTool(
        *service_, "object_set_model",
        mcp::JsonValue::Object{{"task_id", "100"}, {"model_id", "12345678901234567"}}, error);
    EXPECT_TRUE(result.is_null());
    EXPECT_EQ(error, "unsupported_operation");
}

TEST_F(McpDomainToolsTest, ExposesAiMissionGraphAndAssetReadOnlyOperations) {
    std::string error;
    const auto ai = mcp::CallAiTool(
        *service_, "ai_validate_script", mcp::JsonValue::Object{{"source", "Foo(1);"}}, error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_TRUE(ai.at("valid").as_bool());

    const auto mission = mcp::CallMissionTool(
        *service_, "mission_objective_list", mcp::JsonValue::Object{}, error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_EQ(mission.at("level").as_number(), 1);

    const auto graphs = mcp::CallGraphTool(
        *service_, "graph_list", mcp::JsonValue::Object{}, error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_TRUE(graphs.at("graphs").is_array());

    const auto assets = mcp::CallAssetTool(
        *service_, "asset_list", mcp::JsonValue::Object{}, error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_TRUE(assets.at("assets").is_array());
}

TEST_F(McpDomainToolsTest, UsesListedAnonymousIdsAndRejectsUnknownMutationLayouts) {
    std::string error;
    const auto snapshot = service_->ListObjects(1, error);
    ASSERT_TRUE(error.empty()) << error;

    std::string anonymous_id;
    for (const auto& object : snapshot.at("objects").as_array()) {
        if (object.at("type").as_string() == "Building" &&
            object.at("id").as_string().starts_with("anon-")) {
            anonymous_id = object.at("id").as_string();
            break;
        }
    }
    ASSERT_FALSE(anonymous_id.empty());

    const auto anonymous_update = mcp::CallObjectTool(
        *service_, "object_set_model",
        mcp::JsonValue::Object{{"task_id", anonymous_id}, {"model_id", "304_01_1"}}, error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_FALSE(anonymous_update.is_null());

    const auto unknown_update = mcp::CallObjectTool(
        *service_, "object_set_transform",
        mcp::JsonValue::Object{{"task_id", "102"},
                               {"position", mcp::JsonValue::Array{11.0, 12.0, 13.0}}}, error);
    EXPECT_TRUE(unknown_update.is_null());
    EXPECT_EQ(error, "unsupported_operation");
}

TEST_F(McpDomainToolsTest, ReportsTypeSpecificObjectLayoutIndices) {
    std::string error;
    const auto schema = mcp::CallObjectTool(
        *service_, "object_get_schema", mcp::JsonValue::Object{{"type", "Door"}}, error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_EQ(schema.at("fields").at("position").at("parameter_indices").as_array()[0].as_number(), 3);
    EXPECT_EQ(schema.at("fields").at("rotation_radians").at("parameter_indices").as_array()[0].as_number(), 9);
    EXPECT_EQ(schema.at("fields").at("model_id").at("parameter_index").as_number(), 12);
}

TEST_F(McpDomainToolsTest, DerivesTransformAndModelLayoutsFromSchemas) {
    std::string error;
    const auto camera = mcp::CallObjectTool(
        *service_, "object_get_schema", mcp::JsonValue::Object{{"type", "SCamera"}}, error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_EQ(camera.at("fields").at("rotation_radians").at("parameter_indices").as_array()[0].as_number(), 6);
    EXPECT_EQ(camera.at("fields").at("model_id").at("parameter_index").as_number(), 10);

    for (const std::string type : {"Fence", "Car", "Heli"}) {
        const auto schema = mcp::CallObjectTool(
            *service_, "object_get_schema", mcp::JsonValue::Object{{"type", type}}, error);
        ASSERT_TRUE(error.empty()) << error;
        EXPECT_FALSE(schema.at("fields").contains("rotation_radians")) << type;
    }

    const auto train = mcp::CallObjectTool(
        *service_, "object_get_schema", mcp::JsonValue::Object{{"type", "Train"}}, error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_FALSE(train.at("fields").contains("position"));
    EXPECT_FALSE(train.at("fields").contains("rotation_radians"));
    EXPECT_EQ(train.at("fields").at("model_id").at("parameter_index").as_number(), 6);
}

TEST_F(McpDomainToolsTest, RejectsIncompatibleTypeMigration) {
    std::string error;
    const auto result = mcp::CallObjectTool(
        *service_, "object_set_type",
        mcp::JsonValue::Object{{"task_id", "100"}, {"type", "Door"}}, error);
    EXPECT_TRUE(result.is_null());
    EXPECT_EQ(error, "unsupported_operation");

    const auto object = service_->GetObject(1, "100", error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_EQ(object.at("type").as_string(), "Building");
}

TEST_F(McpDomainToolsTest, EnforcesSchemaTypesForParameterMutation) {
    std::string error;
    const auto fractional_team = mcp::CallObjectTool(
        *service_, "object_set_parameter",
        mcp::JsonValue::Object{{"task_id", "100"}, {"parameter_index", 3}, {"value", "not-a-number"}}, error);
    EXPECT_TRUE(fractional_team.is_null());
    EXPECT_EQ(error, "unsupported_operation");

    const auto unknown_field = mcp::CallObjectTool(
        *service_, "object_set_parameter",
        mcp::JsonValue::Object{{"task_id", "100"}, {"parameter_index", 99}, {"value", 1}}, error);
    EXPECT_TRUE(unknown_field.is_null());
    EXPECT_EQ(error, "unsupported_operation");
}

TEST_F(McpDomainToolsTest, ResolvesAnonymousAndDuplicateAiIds) {
    std::string error;
    const auto duplicate = mcp::CallAiTool(
        *service_, "ai_update",
        mcp::JsonValue::Object{{"task_id", "101#1"},
                               {"fields", mcp::JsonValue::Object{{"team", 2}}}}, error);
    EXPECT_TRUE(duplicate.is_null());
    EXPECT_EQ(error, "ambiguous_task_id");

    std::string anonymous_id;
    const auto snapshot = service_->ListObjects(1, error);
    ASSERT_TRUE(error.empty()) << error;
    for (const auto& object : snapshot.at("objects").as_array()) {
        if (object.at("type").as_string() == "HumanSoldier" &&
            object.at("id").as_string().starts_with("anon-")) {
            anonymous_id = object.at("id").as_string();
            break;
        }
    }
    ASSERT_FALSE(anonymous_id.empty());
    const auto anonymous = mcp::CallAiTool(
        *service_, "ai_update",
        mcp::JsonValue::Object{{"task_id", anonymous_id},
                               {"fields", mcp::JsonValue::Object{{"team", 3}}}}, error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_FALSE(anonymous.is_null());
}

}  // namespace
