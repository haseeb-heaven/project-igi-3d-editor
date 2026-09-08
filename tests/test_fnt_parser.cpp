#include <gtest/gtest.h>
#include "../source/renderer/fnt_parser.h"
#include "support/temp_directory.h"
#include "utils.h"
#include <string>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <cstdint>
#include <vector>
#include <random>

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
    test_support::TempDirectory temporary_directory;
    const fs::path path = temporary_directory.path() / "invalid-skip.fnt";
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
}

TEST(FntParserMalformedTest, RejectsEveryShortHeaderWithoutReadingPastEnd) {
    for (size_t length = 0; length < 20; ++length) {
        const std::vector<uint8_t> bytes(length, 0);
        EXPECT_FALSE(FNT_ParseBytes(bytes).valid) << "length=" << length;
    }
}

namespace {
void AppendU16(std::vector<uint8_t>& bytes, uint16_t value) {
    bytes.push_back(static_cast<uint8_t>(value));
    bytes.push_back(static_cast<uint8_t>(value >> 8));
}

void AppendU32(std::vector<uint8_t>& bytes, uint32_t value) {
    bytes.push_back(static_cast<uint8_t>(value));
    bytes.push_back(static_cast<uint8_t>(value >> 8));
    bytes.push_back(static_cast<uint8_t>(value >> 16));
    bytes.push_back(static_cast<uint8_t>(value >> 24));
}

void AppendChunk(std::vector<uint8_t>& bytes, uint32_t fourcc, const std::vector<uint8_t>& data) {
    const uint32_t padded_length = static_cast<uint32_t>((data.size() + 3u) & ~3u);
    AppendU32(bytes, fourcc);
    AppendU32(bytes, static_cast<uint32_t>(data.size()));
    AppendU32(bytes, 0);
    AppendU32(bytes, 16u + padded_length);
    bytes.insert(bytes.end(), data.begin(), data.end());
    bytes.resize(bytes.size() + padded_length - data.size(), 0);
}

std::vector<uint8_t> MinimalRgb565Font() {
    std::vector<uint8_t> bytes;
    AppendU32(bytes, 0x46464C49); // ILFF
    AppendU32(bytes, 0); AppendU32(bytes, 0); AppendU32(bytes, 0);
    AppendU32(bytes, 0x544E4F46); // FONT

    std::vector<uint8_t> fnth(24, 0);
    fnth[4] = 1; fnth[12] = 7; fnth[16] = 5; fnth[20] = 9;
    AppendChunk(bytes, 0x48544E46, fnth); // FNTH
    std::vector<uint8_t> anmf(40, 0);
    anmf[22] = 2; anmf[24] = 1; anmf[26] = 3;
    AppendChunk(bytes, 0x464D4E41, anmf); // ANMF
    std::vector<uint8_t> tran(2, 0); tran[0] = 1;
    AppendChunk(bytes, 0x4E415254, tran); // TRAN
    std::vector<uint8_t> texh(24, 0);
    texh[0] = 2; texh[14] = 2; texh[16] = 1;
    AppendChunk(bytes, 0x48584554, texh); // TEXH
    std::vector<uint8_t> body;
    AppendU16(body, 0xffff); AppendU16(body, 0);
    AppendChunk(bytes, 0x59444F42, body); // BODY
    return bytes;
}
} // namespace

TEST(FntParserSyntheticTest, ParsesMinimalRgb565FontAndGlyphMap) {
    const FntFont font = FNT_ParseBytes(MinimalRgb565Font());
    ASSERT_TRUE(font.valid);
    EXPECT_EQ(font.texWidth, 2);
    EXPECT_EQ(font.texHeight, 1);
    EXPECT_EQ(font.lineHeight, 9);
    ASSERT_EQ(font.glyphs.size(), 1u);
    const FntGlyph& glyph = font.glyphs.at(0);
    EXPECT_EQ(glyph.width, 2);
    EXPECT_EQ(glyph.height, 1);
    EXPECT_EQ(glyph.advance, 3);
    ASSERT_EQ(font.rgba.size(), 8u);
    EXPECT_EQ(font.rgba[3], 255);
    EXPECT_EQ(font.rgba[7], 0);
}

TEST(FntParserMalformedTest, RejectsMinimalFontWithoutBody) {
    std::vector<uint8_t> bytes = MinimalRgb565Font();
    bytes.resize(bytes.size() - 20); // Remove BODY header and payload.
    EXPECT_FALSE(FNT_ParseBytes(bytes).valid);
}

TEST(FntParserFuzzSmokeTest, DeterministicMalformedBuffersNeverProduceValidFont) {
    // Fixed seed makes a failing input reproducible while exercising parser
    // bounds checks without creating one file per generated sample.
    std::mt19937 generator(0xF17F00Du);
    std::uniform_int_distribution<int> length(0, 512);
    std::uniform_int_distribution<int> byte(0, 255);
    for (int sample = 0; sample < 512; ++sample) {
        std::vector<uint8_t> input(static_cast<size_t>(length(generator)));
        for (uint8_t& value : input) value = static_cast<uint8_t>(byte(generator));
        EXPECT_FALSE(FNT_ParseBytes(input).valid) << "seed=0xF17F00D sample=" << sample;
    }
}
