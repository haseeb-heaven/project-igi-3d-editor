#include <gtest/gtest.h>
#include "../source/renderer/tex_writer.h"
#include "../source/renderer/res_writer.h"
#include "utils.h"
#include <string>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cctype>

// ============================================================
//  TEX Parser — first .tex extracted from level1.res
//
//  Extracts the first .tex entry from level1.res into a temp
//  file then parses it with TEX_Parse.
// ============================================================

static std::string ResPath() {
    return Utils::GetIGIRootPath() +
           "\\missions\\location0\\level1\\textures\\level1.res";
}
static std::string TempTexPath() {
    return Utils::GetExeDirectory() + "\\fixtures\\_tmp_test.tex";
}

static bool ExtractFirstTex(const std::string& resPath, const std::string& outPath) {
    RESFile res = RES_Parse(resPath);
    if (!res.valid) return false;
    for (const auto& e : res.entries) {
        std::string name = e.name;
        std::string ext = name.size() >= 4 ? name.substr(name.size() - 4) : "";
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c){ return (char)std::tolower(c); });
        if (ext == ".tex") {
            std::ofstream f(outPath, std::ios::binary);
            if (!f) return false;
            f.write(reinterpret_cast<const char*>(e.data.data()), (std::streamsize)e.data.size());
            return true;
        }
    }
    return false;
}

class TexParserTest : public ::testing::Test {
protected:
    TEXFile tex;
    void SetUp() override {
        bool ok = ExtractFirstTex(ResPath(), TempTexPath());
        ASSERT_TRUE(ok) << "Could not extract a .tex entry from " << ResPath();
        tex = TEX_Parse(TempTexPath());
    }
    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove(TempTexPath(), ec);
    }
};

TEST_F(TexParserTest, ParsesValid) {
    ASSERT_TRUE(tex.valid) << "TEX parse failed: " << tex.error;
}

TEST_F(TexParserTest, VersionIsKnown) {
    EXPECT_TRUE(tex.version == 2 || tex.version == 7 ||
                tex.version == 9 || tex.version == 11)
        << "Unexpected TEX version: " << tex.version;
}

TEST_F(TexParserTest, HasImages) {
    EXPECT_GT(tex.images.size(), 0u);
}

TEST_F(TexParserTest, FirstImageHasPositiveDimensions) {
    ASSERT_FALSE(tex.images.empty());
    EXPECT_GT(tex.images[0].width,  0u);
    EXPECT_GT(tex.images[0].height, 0u);
}

TEST_F(TexParserTest, PixelDataSizeMatchesDimensions) {
    ASSERT_FALSE(tex.images.empty());
    const auto& img = tex.images[0];
    size_t bytesPerPixel = (img.mode == 2) ? 2u : 4u;
    size_t expected = (size_t)img.width * img.height * bytesPerPixel;
    EXPECT_EQ(img.pixels.size(), expected)
        << "mode=" << img.mode << " w=" << img.width << " h=" << img.height;
}

// ============================================================
//  SPR Cursor Sprite Loading — Regression test for terrain
//  cursor icon bug. All 12 cursor SPR files must parse with
//  TEX_Parse and return valid images.
// ============================================================

static std::string QedPath() {
    return Utils::GetIGIRootPath() + "\\editor\\qed\\";
}

struct CursorSprCase {
    const char* filename;
    uint32_t expectedMode;   // 3 = ARGB8888 (4bpp), 2 = RGB565 (2bpp)
    uint32_t expectedWidth;
    uint32_t expectedHeight;
};

class CursorSprTest : public ::testing::TestWithParam<CursorSprCase> {};

TEST_P(CursorSprTest, ParsesValidSpr) {
    const auto& tc = GetParam();
    std::string path = QedPath() + tc.filename;

    // Skip if game files not present (CI without game install)
    if (!std::filesystem::exists(path)) {
        GTEST_SKIP() << "Skipped: game file not present: " << path;
    }

    TEXFile tex = TEX_Parse(path);
    ASSERT_TRUE(tex.valid) << "[" << tc.filename << "] parse failed: " << tex.error;
    ASSERT_FALSE(tex.images.empty()) << "[" << tc.filename << "] no images parsed";

    const auto& img = tex.images[0];
    EXPECT_EQ(img.mode,   tc.expectedMode)   << "[" << tc.filename << "] wrong mode";
    EXPECT_EQ(img.width,  tc.expectedWidth)  << "[" << tc.filename << "] wrong width";
    EXPECT_EQ(img.height, tc.expectedHeight) << "[" << tc.filename << "] wrong height";

    // Pixel data must exactly match w * h * bpp
    size_t bpp = (img.mode == 2) ? 2u : 4u;
    size_t expected = (size_t)img.width * img.height * bpp;
    EXPECT_EQ(img.pixels.size(), expected)
        << "[" << tc.filename << "] pixel buffer size mismatch";

    // At least some pixels must be non-zero (icon has actual content)
    bool hasNonZero = std::any_of(img.pixels.begin(), img.pixels.end(),
                                  [](uint8_t b) { return b != 0; });
    EXPECT_TRUE(hasNonZero) << "[" << tc.filename << "] all pixels are zero";
}

INSTANTIATE_TEST_SUITE_P(
    TerrainCursorSprites,
    CursorSprTest,
    ::testing::Values(
        CursorSprCase{"TerrainEditIcon_Pointer.spr",    3, 32, 32},
        CursorSprCase{"TerrainEditIcon_Lift.spr",       3, 32, 32},
        CursorSprCase{"TerrainEditIcon_Lower.spr",      3, 32, 32},
        CursorSprCase{"TerrainEditIcon_Flatten.spr",    3, 32, 32},
        CursorSprCase{"TerrainEditIcon_FlattenLine.spr",3, 32, 32},
        CursorSprCase{"TerrainEditIcon_Drop.spr",       3, 32, 32},
        CursorSprCase{"TerrainEditIcon_Soften.spr",     3, 32, 32},
        CursorSprCase{"highlighttool.spr",              3, 33, 33},
        CursorSprCase{"activetool.spr",                 3, 33, 33},
        CursorSprCase{"inactivetool.spr",               3, 33, 33},
        CursorSprCase{"editor_camera.spr",              2, 40, 28},
        CursorSprCase{"editor_move.spr",                2, 40, 28}
    ),
    [](const ::testing::TestParamInfo<CursorSprCase>& info) {
        std::string name(info.param.filename);
        // strip .spr
        if (name.size() > 4) name.resize(name.size() - 4);
        // replace non-alphanum with _
        for (auto& c : name) if (!std::isalnum((unsigned char)c)) c = '_';
        return name;
    }
);
