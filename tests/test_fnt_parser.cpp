#include <gtest/gtest.h>
#include "../source/renderer/fnt_parser.h"
#include "utils.h"
#include <string>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <cstdint>

// ============================================================
//  FNT Parser — first .fnt found under game root
// ============================================================

static std::string FindFirstFile(const std::string& root, const std::string& ext) {
    namespace fs = std::filesystem;
    std::error_code ec;
    for (auto& e : fs::recursive_directory_iterator(root, ec)) {
        if (ec) { ec.clear(); continue; }
        if (!e.is_regular_file(ec)) { ec.clear(); continue; }
        std::string fext = e.path().extension().string();
        std::transform(fext.begin(), fext.end(), fext.begin(),
                       [](unsigned char c){ return (char)std::tolower(c); });
        if (fext == ext) return e.path().string();
    }
    return "";
}

class FntParserTest : public ::testing::Test {
protected:
    FntFont font;
    std::string fntPath;
    void SetUp() override {
        fntPath = FindFirstFile(Utils::GetIGIRootPath(), ".fnt");
        ASSERT_FALSE(fntPath.empty())
            << "No .fnt file found under: " << Utils::GetIGIRootPath();
        font = FNT_Parse(fntPath);
    }
};

TEST_F(FntParserTest, ParsesValid) {
    ASSERT_TRUE(font.valid) << "FNT parse failed\nPath: " << fntPath;
}

TEST_F(FntParserTest, LineHeightIsPositive) {
    EXPECT_GT(font.lineHeight, 0);
}

TEST_F(FntParserTest, TextureDimensionsArePositive) {
    EXPECT_GT(font.texWidth,  0);
    EXPECT_GT(font.texHeight, 0);
}

TEST_F(FntParserTest, HasGlyphs) {
    EXPECT_GT(font.glyphs.size(), 0u);
}

TEST_F(FntParserTest, AtlasPixelDataSizeMatchesDimensions) {
    size_t expected = (size_t)font.texWidth * font.texHeight * 4;
    EXPECT_EQ(font.rgba.size(), expected)
        << "texWidth=" << font.texWidth << " texHeight=" << font.texHeight;
}

TEST(FntParserMalformedTest, RejectsChunkThatCannotAdvance) {
    namespace fs = std::filesystem;
    const fs::path path = fs::temp_directory_path() / "igi-editor-invalid-skip.fnt";
    std::vector<uint8_t> bytes(36, 0);
    auto put32 = [&](size_t offset, uint32_t value) {
        bytes[offset + 0] = static_cast<uint8_t>(value);
        bytes[offset + 1] = static_cast<uint8_t>(value >> 8);
        bytes[offset + 2] = static_cast<uint8_t>(value >> 16);
        bytes[offset + 3] = static_cast<uint8_t>(value >> 24);
    };
    put32(0, 0x46464C49);   // ILFF
    put32(16, 0x544E4F46);  // FONT
    put32(20, 0x48544E46);  // FNTH
    put32(32, 8);           // invalid skip: smaller than the chunk header
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(output.is_open());
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
    }

    const FntFont parsed = FNT_Parse(path.string());
    EXPECT_FALSE(parsed.valid);
    std::error_code ec;
    fs::remove(path, ec);
}
