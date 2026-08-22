#include "pch.h"
#include "renderer_rain.h"
#include "weather_math.h"

#include <string>
#include "../logger.h"
#include <freeglut.h>
#include <vector>
#include <random>

// Rain streaks: retuned to open-igi RainRenderer.cs (retail-verified, itself
// ported from this file's original constants and then corrected against igi.exe):
// 1200 drops, 0.08 m streaks, fall speed 0.08..0.18 of the authored band per
// second, alpha = clamp(authored * 1.25, 0, 0.28). See weather_math.h.
static const char* RAIN_VERT_SRC_FMT = R"(
#version 330 core
layout(location = 0) in vec3 a_seed;   // per-drop random seed, x/y/z in [0,1)
layout(location = 1) in float a_isTop; // 0 = bottom vertex of the streak, 1 = top

layout(std140) uniform Matrices {
    mat4 u_unused1;
    mat4 u_unused2;
    mat4 u_mvp;
};

uniform vec3  u_cameraPos;
uniform float u_time;
uniform float u_boxSize;     // footprint around the camera that drops are scattered in
uniform float u_heightStart; // world units, where drops spawn
uniform float u_heightEnd;   // world units, where drops disappear
uniform float u_streakLen;   // world units
uniform float u_speedMul;    // user speed multiplier (r_weather_speed)

void main() {
    float fallRange = max(u_heightStart - u_heightEnd, 1.0);
    // open-igi: (min + seed.z * spread) of the band per second — constants
    // injected from igi::weather (single source of truth with weather_math.h).
    float speed = (%.9g + a_seed.z * %.9g) * fallRange * u_speedMul;
    float z = u_heightStart - mod(u_time * speed + a_seed.y * fallRange + u_cameraPos.z, fallRange);

    vec2 cell = mod(a_seed.xy * u_boxSize - u_cameraPos.xy, u_boxSize) - u_boxSize * 0.5;
    vec3 worldPos = vec3(u_cameraPos.x + cell.x, u_cameraPos.y + cell.y, z + a_isTop * u_streakLen);

    gl_Position = u_mvp * vec4(worldPos, 1.0);
}
)";

static const char* RAIN_FRAG_SRC_FMT = R"(
#version 330 core
uniform float u_alpha;
out vec4 fragColor;
void main() {
    // open-igi: clamp(authored alpha * 1.25, 0, 0.28) — a restrained scale so a
    // thin streak never turns into a solid sheet of white.
    fragColor = vec4(0.8, 0.85, 0.9, clamp(u_alpha * %.9g, 0.0, %.9g));
}
)";

// Snow flakes: port of open-igi SnowRenderer.cs — slow drifting flakes in a
// camera-relative box. Fall speed is 0.025..0.065 of the band per second with
// sinusoidal X/Y drift; flakes are round point sprites tinted pale cool white.
static const char* SNOW_VERT_SRC_FMT = R"(
#version 330 core
layout(location = 0) in vec3 a_seed;   // per-flake random seed, x/y/z in [0,1)

layout(std140) uniform Matrices {
    mat4 u_unused1;
    mat4 u_unused2;
    mat4 u_mvp;
};

uniform vec3  u_cameraPos;
uniform float u_time;
uniform float u_boxSize;
uniform float u_heightStart;
uniform float u_heightEnd;
uniform float u_speedMul;
uniform float u_flakeSize;   // world units
uniform float u_viewportH;   // pixels, for perspective point sizing
uniform float DRIFT_UNITS;   // drift amplitude in world units

void main() {
    float fallRange = max(u_heightStart - u_heightEnd, 1.0);
    // open-igi: (0.025 + seed.z * 0.04) of the band per second
    float speed = (%.9g + a_seed.z * %.9g) * fallRange * u_speedMul;
    float z = u_heightStart - mod(u_time * speed + a_seed.z * fallRange + u_cameraPos.z, fallRange);

    // open-igi sway: sin(t*0.45 + sx*17)*drift on X, cos(t*0.35 + sy*19)*drift on Y
    float driftX = sin(u_time * %.9g + a_seed.x * %.9g);
    float driftY = cos(u_time * %.9g + a_seed.y * %.9g);
    float halfBox = u_boxSize * 0.5;
    float cellX = mod(a_seed.x * u_boxSize - u_cameraPos.x, u_boxSize) - halfBox + driftX * DRIFT_UNITS;
    float cellY = mod(a_seed.y * u_boxSize - u_cameraPos.y, u_boxSize) - halfBox + driftY * DRIFT_UNITS;

    vec3 worldPos = vec3(u_cameraPos.x + cellX, u_cameraPos.y + cellY, z);

    gl_Position = u_mvp * vec4(worldPos, 1.0);
    // Perspective-correct point size: projScaleY = viewportH/2 * |proj[1][1]|.
    gl_PointSize = clamp(u_viewportH * 0.5 * abs(u_mvp[1][1]) * u_flakeSize / max(gl_Position.w, 0.0001), 1.0, 64.0);
}
)";

static const char* SNOW_FRAG_SRC_FMT = R"(
#version 330 core
uniform float u_alpha;
out vec4 fragColor;
void main() {
    // Round flake mask (open-igi draws square billboards ~0.045 m; a circular
    // mask at the same footprint reads identically at retail flake sizes).
    vec2 d = gl_PointCoord - vec2(0.5);
    if (dot(d, d) > 0.25) discard;
    // open-igi SnowRenderer colour Bgra32(0.92, 0.97, 255): pale cool white,
    // alpha clamped into [0.10, 0.42] so flakes stay visible on snow terrain.
    fragColor = vec4(0.92, 0.97, 1.0, clamp(u_alpha * %.9g, %.9g, %.9g));
}
)";

static GLuint CompileRainShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, 512, nullptr, log);
        Logger::Get().Log(LogLevel::ERR, std::string("[Renderer_Rain] Shader compile error: ") + log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static GLuint LinkWeatherProgram(GLenum vertType, const char* vertSrc, GLenum fragType, const char* fragSrc, GLuint uboBinding) {
    GLuint vert = CompileRainShader(vertType, vertSrc);
    GLuint frag = CompileRainShader(fragType, fragSrc);
    if (!vert || !frag) {
        if (vert) glDeleteShader(vert);
        if (frag) glDeleteShader(frag);
        return 0;
    }

    GLuint program = glCreateProgram();
    glAttachShader(program, vert);
    glAttachShader(program, frag);
    glLinkProgram(program);
    GLint linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    glDeleteShader(vert);
    glDeleteShader(frag);
    if (!linked) {
        char log[512];
        glGetProgramInfoLog(program, 512, nullptr, log);
        Logger::Get().Log(LogLevel::ERR, std::string("[Renderer_Rain] Link error: ") + log);
        glDeleteProgram(program);
        return 0;
    }

    GLuint blockIdx = glGetUniformBlockIndex(program, "Matrices");
    if (blockIdx != GL_INVALID_INDEX) {
        glUniformBlockBinding(program, blockIdx, uboBinding);
    }
    return program;
}

// Shader sources are FORMATTED from the tested igi::weather constants so
// weather_math.h is the single source of truth shared by renderer and tests
// (PR #64 review finding: hardcoded literals could silently drift).
namespace {
std::string FmtRainVert() {
    char buf[4096];
    snprintf(buf, sizeof(buf), RAIN_VERT_SRC_FMT,
             (double)igi::weather::kRainMinSpeedMul,
             (double)igi::weather::kRainMaxSpeedMul - (double)igi::weather::kRainMinSpeedMul);
    return buf;
}
std::string FmtRainFrag() {
    char buf[1024];
    snprintf(buf, sizeof(buf), RAIN_FRAG_SRC_FMT,
             (double)igi::weather::kRainAlphaBoost, (double)igi::weather::kRainMaxAlpha);
    return buf;
}
std::string FmtSnowVert() {
    char buf[4096];
    snprintf(buf, sizeof(buf), SNOW_VERT_SRC_FMT,
             (double)igi::weather::kSnowMinSpeedMul,
             (double)igi::weather::kSnowMaxSpeedMul - (double)igi::weather::kSnowMinSpeedMul);
    return buf;
}
std::string FmtSnowFrag() {
    char buf[2048];
    snprintf(buf, sizeof(buf), SNOW_FRAG_SRC_FMT,
             (double)igi::weather::kSnowAlphaBoost, (double)igi::weather::kSnowMinAlpha,
             (double)igi::weather::kSnowMaxAlpha);
    return buf;
}
} // namespace

bool Renderer_Rain::Init() {
    static const std::string rain_vert = FmtRainVert();
    static const std::string rain_frag = FmtRainFrag();
    shader_program_ = LinkWeatherProgram(GL_VERTEX_SHADER, rain_vert.c_str(),
                                         GL_FRAGMENT_SHADER, rain_frag.c_str(), ubo_binding_point_);
    if (!shader_program_) return false;

    // Rain seeds (open-igi GenerateSeeds: mt19937, seed 12345)
    num_drops_ = igi::weather::kRainDrops;
    std::vector<float> verts;
    verts.reserve(num_drops_ * 2 * 4);
    std::mt19937 rng(12345);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    for (int i = 0; i < num_drops_; ++i) {
        float sx = dist(rng), sy = dist(rng), sz = dist(rng);
        verts.insert(verts.end(), { sx, sy, sz, 0.0f }); // bottom
        verts.insert(verts.end(), { sx, sy, sz, 1.0f }); // top
    }

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    Logger::Get().Log(LogLevel::INFO, "[Renderer_Rain] Init OK.");
    return InitSnow();
}

bool Renderer_Rain::InitSnow() {
    static const std::string snow_vert = FmtSnowVert();
    static const std::string snow_frag = FmtSnowFrag();
    snow_program_ = LinkWeatherProgram(GL_VERTEX_SHADER, snow_vert.c_str(),
                                       GL_FRAGMENT_SHADER, snow_frag.c_str(), ubo_binding_point_);
    if (!snow_program_) return false;

    // Snow seeds (open-igi SnowRenderer ctor: Random(271828))
    num_flakes_ = igi::weather::kSnowFlakes;
    std::vector<float> seeds;
    seeds.reserve(num_flakes_ * 3);
    std::mt19937 rng(271828);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    for (int i = 0; i < num_flakes_; ++i) {
        seeds.insert(seeds.end(), { dist(rng), dist(rng), dist(rng) });
    }

    glGenVertexArrays(1, &snow_vao_);
    glGenBuffers(1, &snow_vbo_);
    glBindVertexArray(snow_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, snow_vbo_);
    glBufferData(GL_ARRAY_BUFFER, seeds.size() * sizeof(float), seeds.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    Logger::Get().Log(LogLevel::INFO, "[Renderer_Rain] Snow renderer init OK.");
    return true;
}

void Renderer_Rain::Shutdown() {
    if (vbo_) { glDeleteBuffers(1, &vbo_); vbo_ = 0; }
    if (vao_) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
    if (snow_vbo_) { glDeleteBuffers(1, &snow_vbo_); snow_vbo_ = 0; }
    if (snow_vao_) { glDeleteVertexArrays(1, &snow_vao_); snow_vao_ = 0; }
    if (shader_program_) { glDeleteProgram(shader_program_); shader_program_ = 0; }
    if (snow_program_) { glDeleteProgram(snow_program_); snow_program_ = 0; }
}

void Renderer_Rain::SetParams(bool active, bool isRain, float startMeters, float endMeters, float alpha) {
    // open-igi ReadParams semantics: an active task makes the weather AVAILABLE
    // even when it is authored as snow ("Is Rain"=FALSE); which style renders is
    // resolved against the user's style setting in Draw().
    authored_available_ = active;
    authored_is_rain_ = isRain;
    start_meters_ = startMeters;
    end_meters_ = endMeters;
    alpha_ = alpha;
}

void Renderer_Rain::ApplySettings(bool enabled, int style, int speedPercent) {
    user_enabled_ = enabled;
    style_override_ = style;
    speed_multiplier_ = std::max(0.0f, std::min(2.0f, speedPercent / 100.0f));
}

// Shared per-frame band setup: returns false when nothing should render.
static bool ComputeBand(const glm::vec3& cameraPos, float startMeters, float endMeters,
                        float& heightStart, float& heightEnd) {
    // RainEffect's Traceline start/end are raycast-occlusion heights (sky-to-ground
    // probe), not absolute world Y — re-anchor them to the camera each frame so the
    // particle band always surrounds wherever the player actually is in the level.
    heightStart = cameraPos.z + startMeters * WORLD_UNITS_PER_METER;
    heightEnd = cameraPos.z - endMeters * WORLD_UNITS_PER_METER;
    return heightStart > heightEnd;
}

void Renderer_Rain::Draw(GLuint ubo_mats, const glm::vec3& cameraPos) {
    if (!authored_available_ || !user_enabled_ || indoors_) return;

    bool wantRain = (style_override_ == kStyleRain) ||
                    (style_override_ == kStyleAuto && authored_is_rain_);
    bool wantSnow = (style_override_ == kStyleSnow) ||
                    (style_override_ == kStyleAuto && !authored_is_rain_);

    float timeSec = glutGet(GLUT_ELAPSED_TIME) / 1000.0f;

    if (wantRain && shader_program_) {
        float heightStart = 0.0f, heightEnd = 0.0f;
        if (!ComputeBand(cameraPos, start_meters_, end_meters_, heightStart, heightEnd)) return;

        glUseProgram(shader_program_);
        glBindBufferBase(GL_UNIFORM_BUFFER, ubo_binding_point_, ubo_mats);

        glUniform3f(glGetUniformLocation(shader_program_, "u_cameraPos"), cameraPos.x, cameraPos.y, cameraPos.z);
        glUniform1f(glGetUniformLocation(shader_program_, "u_time"), timeSec);
        glUniform1f(glGetUniformLocation(shader_program_, "u_boxSize"), igi::weather::kBoxMeters * WORLD_UNITS_PER_METER);
        glUniform1f(glGetUniformLocation(shader_program_, "u_heightStart"), heightStart);
        glUniform1f(glGetUniformLocation(shader_program_, "u_heightEnd"), heightEnd);
        glUniform1f(glGetUniformLocation(shader_program_, "u_streakLen"), igi::weather::kRainStreakMeters * WORLD_UNITS_PER_METER);
        glUniform1f(glGetUniformLocation(shader_program_, "u_speedMul"), speed_multiplier_);
        glUniform1f(glGetUniformLocation(shader_program_, "u_alpha"), alpha_);

        GLboolean depthMaskWas;
        glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMaskWas);
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_DEPTH_TEST);

        glLineWidth(1.5f);
        glBindVertexArray(vao_);
        glDrawArrays(GL_LINES, 0, num_drops_ * 2);
        glBindVertexArray(0);
        glLineWidth(1.0f);

        glDepthMask(depthMaskWas);
        glUseProgram(0);
    }

    if (wantSnow && snow_program_) {
        DrawSnow(ubo_mats, cameraPos, timeSec);
    }
}

void Renderer_Rain::DrawSnow(GLuint ubo_mats, const glm::vec3& cameraPos, float timeSec) {
    float heightStart = 0.0f, heightEnd = 0.0f;
    if (!ComputeBand(cameraPos, start_meters_, end_meters_, heightStart, heightEnd)) return;

    glUseProgram(snow_program_);
    glBindBufferBase(GL_UNIFORM_BUFFER, ubo_binding_point_, ubo_mats);

    glUniform3f(glGetUniformLocation(snow_program_, "u_cameraPos"), cameraPos.x, cameraPos.y, cameraPos.z);
    glUniform1f(glGetUniformLocation(snow_program_, "u_time"), timeSec);
    glUniform1f(glGetUniformLocation(snow_program_, "u_boxSize"), igi::weather::kBoxMeters * WORLD_UNITS_PER_METER);
    glUniform1f(glGetUniformLocation(snow_program_, "u_heightStart"), heightStart);
    glUniform1f(glGetUniformLocation(snow_program_, "u_heightEnd"), heightEnd);
    glUniform1f(glGetUniformLocation(snow_program_, "u_speedMul"), speed_multiplier_);
    glUniform1f(glGetUniformLocation(snow_program_, "u_flakeSize"), igi::weather::kSnowFlakeMeters * WORLD_UNITS_PER_METER);
    glUniform1f(glGetUniformLocation(snow_program_, "u_viewportH"), (float)glutGet(GLUT_WINDOW_HEIGHT));
    glUniform1f(glGetUniformLocation(snow_program_, "u_alpha"), alpha_);
    glUniform1f(glGetUniformLocation(snow_program_, "DRIFT_UNITS"), igi::weather::kSnowDriftMeters * WORLD_UNITS_PER_METER);

    GLboolean depthMaskWas;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMaskWas);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);

    glEnable(GL_PROGRAM_POINT_SIZE);
    glBindVertexArray(snow_vao_);
    glDrawArrays(GL_POINTS, 0, num_flakes_);
    glBindVertexArray(0);
    glDisable(GL_PROGRAM_POINT_SIZE);

    glDepthMask(depthMaskWas);
    glUseProgram(0);
}
