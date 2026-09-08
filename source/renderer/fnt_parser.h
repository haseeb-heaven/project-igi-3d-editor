/******************************************************************************
 * @file    fnt_parser.h
 * @brief   FNT (ILFF-based bitmap font) parser for IGI-1 .fnt files
 *
 * .fnt files are ILFF containers with content type "FONT".
 * Chunks: FNTH (header), ANMF (glyph metrics), TRAN (char-code map),
 *         TEXH (texture header), BODY (texture atlas).
 *****************************************************************************/

#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <span>
#include <unordered_map>

struct FntGlyph {
    float u0, v0, u1, v1;   // atlas UV rect (normalized)
    int   width, height;    // glyph pixel size
    int   offsetX, offsetY; // placement bearing relative to the line origin
    int   advance;          // x advance
};

struct FntFont {
    bool valid = false;
    int  lineHeight = 0;
    int  defaultAdvance = 0;
    int  defaultWidth = 0;
    int  texWidth = 0, texHeight = 0;
    std::vector<uint8_t> rgba;                       // atlas converted to RGBA, size texW*texH*4
    std::unordered_map<int, FntGlyph> glyphs;        // charcode -> glyph
};

FntFont FNT_Parse(const std::string& filepath);
FntFont FNT_ParseBytes(std::span<const uint8_t> bytes);
