#include <gtest/gtest.h>

#include "../source/runtime/level_weather.h"

namespace {

igi::WeatherEffectObject RainEffect(const char* is_rain, const char* is_active,
                                    const char* alpha = "0.06") {
    return {"RainEffect",
            {"-1", "RainEffect", "", is_rain, "50.0", "20.0", is_active, alpha}};
}

TEST(LevelWeatherTest, VanillaLevelsUseOnlyTheirAuthoredWeatherObject) {
    // Values are from the decompiled location0 level objects.qvm files.
    const std::vector<std::vector<igi::WeatherEffectObject>> levels = {
        {}, {}, {RainEffect("TRUE", "1", "0.13")},
        {RainEffect("TRUE", "FALSE\n")}, {}, {},
        {RainEffect("FALSE", "1")}, {RainEffect("TRUE", "FALSE\n")},
        {RainEffect("TRUE", "TRUE\n", "0.15")},
        {RainEffect("TRUE", "TRUE", "0.135")},
        {RainEffect("TRUE", "FALSE\n")},
        {RainEffect("FALSE", "1", "0.3125")}, {}, {},
    };
    const std::vector<bool> expected_active = {
        false, false, true, false, false, false, true,
        false, true, true, false, true, false, false,
    };
    const std::vector<bool> expected_snow = {
        false, false, false, false, false, false, true,
        false, false, false, false, true, false, false,
    };

    ASSERT_EQ(levels.size(), 14u);
    ASSERT_EQ(expected_active.size(), levels.size());
    ASSERT_EQ(expected_snow.size(), levels.size());
    for (size_t i = 0; i < levels.size(); ++i) {
        const igi::LevelWeatherSettings weather = igi::ResolveLevelWeather(levels[i]);
        EXPECT_EQ(weather.active, expected_active[i]) << "level " << (i + 1);
        EXPECT_EQ(weather.is_snow, expected_snow[i]) << "level " << (i + 1);
    }
}

TEST(LevelWeatherTest, FalseVarStringDoesNotEnableWeather) {
    const igi::LevelWeatherSettings weather =
        igi::ResolveLevelWeather({RainEffect("TRUE", "FALSE\n")});
    EXPECT_FALSE(weather.active);
    EXPECT_FALSE(weather.is_snow);
    EXPECT_FLOAT_EQ(weather.alpha, 0.0f);
}

TEST(LevelWeatherTest, InvalidRainEffectCannotEnableWeather) {
    const igi::WeatherEffectObject malformed = {
        "RainEffect",
        {"-1", "RainEffect", "", "MAYBE", "50.0", "20.0", "TRUE", "0.06"},
    };
    EXPECT_FALSE(igi::ResolveLevelWeather({malformed}).active);
}

TEST(LevelWeatherTest, MissingRainEffectResetsToDisabledDefaults) {
    const igi::LevelWeatherSettings weather = igi::ResolveLevelWeather({});
    EXPECT_FALSE(weather.active);
    EXPECT_FALSE(weather.is_snow);
    EXPECT_FLOAT_EQ(weather.start_meters, 0.0f);
    EXPECT_FLOAT_EQ(weather.end_meters, 0.0f);
    EXPECT_FLOAT_EQ(weather.alpha, 0.0f);
}

} // namespace
