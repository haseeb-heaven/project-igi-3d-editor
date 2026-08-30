#include <gtest/gtest.h>

#include "config.h"
#include "logger.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

TEST(LoggerTest, CreatesAndFlushesConfiguredLogFile) {
    namespace fs = std::filesystem;
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path path = fs::temp_directory_path() /
        ("igi1ed-logger-test-" + std::to_string(unique) + ".log");
    std::error_code ec;
    fs::remove(path, ec);

    Config::Get().enableLogging = true;
    Config::Get().debugLogging = true;
    Logger& logger = Logger::Get();
    logger.Init(path.string());
    ASSERT_TRUE(logger.IsOpen());
    ASSERT_EQ(fs::path(logger.GetLogPath()).lexically_normal(), path.lexically_normal());

    logger.Log(LogLevel::INFO, "logger regression test");
    logger.Flush();

    ASSERT_TRUE(fs::exists(path));
    std::ifstream in(path);
    const std::string contents((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
    EXPECT_NE(contents.find("logger regression test"), std::string::npos);
}

TEST(ConfigAssetTest, ShipsLoggingEnabledByDefault) {
    const std::filesystem::path config_path =
        std::filesystem::path("assets") / "editor" / "qed" / "qedconfig.qsc";
    std::ifstream in(config_path);
    ASSERT_TRUE(in.is_open()) << "missing " << config_path.string();
    const std::string source((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
    EXPECT_NE(source.find("QEDLogs(TRUE)"), std::string::npos);
    EXPECT_NE(source.find("QEDDebug(FALSE)"), std::string::npos);
    EXPECT_NE(source.find("missions/location0"), std::string::npos);
}
