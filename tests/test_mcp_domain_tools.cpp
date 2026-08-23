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

}  // namespace
