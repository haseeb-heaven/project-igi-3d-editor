#pragma once
#include "../pch.h"
#include <glm/glm.hpp>

// Renders falling weather particles for levels whose objects.qsc has an active
// RainEffect task (e.g. level3 rain). Levels without that task never call
// SetParams(true, ...) and this stays a no-op. Procedural GPU-only particles:
// rain draws 2-vertex line streaks and snow draws point flakes, each derived in
// the vertex shader from a per-particle random seed + elapsed time, wrapped in
// a box around the camera — no per-frame CPU update needed.
//
// Vanilla parity (issues #57/#58): constants, speeds, alpha response and the
// snow style come from open-igi's RainRenderer.cs / SnowRenderer.cs — see
// weather_math.h and docs/WEATHER_PARITY.md. User settings mirror open-igi's
// r_weather_enabled / r_weather_kind / r_weather_speed cvars:
//   - SetParams() feeds the AUTHORED descriptor (availability + band + alpha).
//   - ApplySettings() feeds the USER overrides (enabled / Auto-Rain-Snow /
//     speed multiplier); the effective mode is resolved per frame in Draw().
class Renderer_Rain {
public:
    // Weather style selection mirroring open-igi WeatherKind (+ Auto default).
    static constexpr int kStyleAuto = 0; // follow the authored "Is Rain" flag
    static constexpr int kStyleRain = 1;
    static constexpr int kStyleSnow = 2;

    bool Init();
    void Shutdown();

    // Authored RainEffect descriptor: isRain=false with active=true is SNOWFALL
    // (open-igi SnowRenderer.ReadParams semantics). startMeters/endMeters come
    // from the task's "Traceline start"/"Traceline end" fields (meters above /
    // below the camera where particles fall).
    void SetParams(bool active, bool isRain, float startMeters, float endMeters, float alpha);

    // User weather settings (r_weather_* semantics): enabled toggle, style
    // selector (kStyleAuto/kStyleRain/kStyleSnow) and speed percent 0..200.
    void ApplySettings(bool enabled, int style, int speedPercent);

    void Draw(GLuint ubo_mats, const glm::vec3& cameraPos);
    void SetIndoors(bool indoors) { indoors_ = indoors; }

private:
    bool InitSnow();
    void DrawSnow(GLuint ubo_mats, const glm::vec3& cameraPos, float timeSec);

    GLuint shader_program_ = 0;   // rain streaks
    GLuint snow_program_ = 0;     // snow flakes
    GLuint ubo_binding_point_ = 0;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint snow_vao_ = 0;
    GLuint snow_vbo_ = 0;
    int num_drops_ = 0;
    int num_flakes_ = 0;

    // Authored descriptor (level data)
    bool authored_available_ = false;
    bool authored_is_rain_ = true;
    float start_meters_ = 10.0f;
    float end_meters_ = 2.0f;
    float alpha_ = 0.5f;

    // User settings (weather menu)
    bool user_enabled_ = true;
    int style_override_ = kStyleAuto;
    float speed_multiplier_ = 1.0f;

    bool indoors_ = false;
};
