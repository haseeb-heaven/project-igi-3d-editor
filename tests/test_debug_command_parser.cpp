#include <gtest/gtest.h>
#include "debug_command_parser.h"

TEST(DebugCommandParserTest, ParseGotoCommand) {
    auto cmd = ParseDebugCommand("goto level=5 model=123_45_6");
    ASSERT_TRUE(cmd);
    EXPECT_EQ(cmd->type, "goto");
    EXPECT_EQ(cmd->level, 5);
    EXPECT_EQ(cmd->modelId, "123_45_6");
}

TEST(DebugCommandParserTest, ParseCaptureCommand) {
    auto cmd = ParseDebugCommand("capture-model level=10 model=999_12_1");
    ASSERT_TRUE(cmd);
    EXPECT_EQ(cmd->type, "capture-model");
    EXPECT_EQ(cmd->level, 10);
    EXPECT_EQ(cmd->modelId, "999_12_1");
}

TEST(DebugCommandParserTest, ParseInvalidCommand) {
    EXPECT_FALSE(ParseDebugCommand("invalid_cmd level=1 model=test"));
}
