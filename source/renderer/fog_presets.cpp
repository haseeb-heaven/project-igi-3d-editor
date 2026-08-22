#include "fog_presets.h"

#include "../logger.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>

// C++ port of open-igi src/OpenIGI.Rendering/FogPresets.cs + FogPresetEntry.cs +
// LocalFogVolume.cs (issue #66). Field names in the preset file match open-igi's
// Assets/fog-presets.json schema exactly ("density", "heightFalloff", "albedo",
// "anisotropy", "distance", "multipleScattering", "uniformFraction",
// "sunIntensity", "ambientIntensity"; volumes: "centre", "extent",
// "densityScale", "shape"). The reader tolerates comments and trailing commas
// like open-igi's JsonDocumentOptions(CommentHandling.Skip, AllowTrailingCommas).

namespace igi {

namespace {

// ---------------------------------------------------------------------------
// Minimal tolerant JSON reader — just enough for the fog-presets schema.
// ---------------------------------------------------------------------------
struct JsonValue;
using JsonObject = std::vector<std::pair<std::string, JsonValue>>;
using JsonArray = std::vector<JsonValue>;

struct JsonValue {
    enum class Kind { Null, Bool, Number, String, Array, Object } kind = Kind::Null;
    bool boolean = false;
    double number = 0.0;
    std::string text;
    JsonArray array;
    JsonObject object;

    const JsonValue* Find(const std::string& key) const {
        if (kind != Kind::Object) return nullptr;
        for (const auto& kv : object) {
            if (kv.first == key) return &kv.second;
        }
        return nullptr;
    }
};

class JsonParser {
public:
    explicit JsonParser(std::string text) : text_(std::move(text)) {
        StripComments();
    }

    bool Parse(JsonValue& out) {
        pos_ = 0;
        SkipWs();
        if (!ParseValue(out)) return false;
        SkipWs();
        return pos_ >= text_.size();
    }

private:
    void StripComments() {
        // Replace // and /* */ comments with spaces so offsets stay stable.
        std::string out;
        out.reserve(text_.size());
        for (size_t i = 0; i < text_.size(); ++i) {
            char c = text_[i];
            if (!in_string_ && c == '/' && i + 1 < text_.size()) {
                if (text_[i + 1] == '/') {
                    while (i < text_.size() && text_[i] != '\n') { out.push_back(' '); ++i; }
                    out.push_back(' ');
                    continue;
                }
                if (text_[i + 1] == '*') {
                    i += 2;
                    while (i + 1 < text_.size() && !(text_[i] == '*' && text_[i + 1] == '/')) {
                        out.push_back(text_[i] == '\n' ? '\n' : ' ');
                        ++i;
                    }
                    i++; // skip '*'
                    out.push_back(' ');
                    continue;
                }
            }
            if (c == '"') in_string_ = !in_string_;
            if (in_string_ && c == '\\' && i + 1 < text_.size()) {
                out.push_back(c);
                out.push_back(text_[i + 1]);
                ++i;
                continue;
            }
            out.push_back(c);
        }
        text_ = std::move(out);
    }

    static void SkipWsOf(const std::string& s, size_t& p) {
        while (p < s.size() && std::isspace(static_cast<unsigned char>(s[p]))) ++p;
    }
    void SkipWs() { SkipWsOf(text_, pos_); }

    bool ParseValue(JsonValue& v) {
        SkipWs();
        if (pos_ >= text_.size()) return false;
        char c = text_[pos_];
        if (c == '{') return ParseObject(v);
        if (c == '[') return ParseArray(v);
        if (c == '"') return ParseString(v);
        if (strncmp(text_.c_str() + pos_, "true", 4) == 0) {
            v = {}; v.kind = JsonValue::Kind::Bool; v.boolean = true; pos_ += 4; return true;
        }
        if (strncmp(text_.c_str() + pos_, "false", 5) == 0) {
            v = {}; v.kind = JsonValue::Kind::Bool; v.boolean = false; pos_ += 5; return true;
        }
        if (strncmp(text_.c_str() + pos_, "null", 4) == 0) {
            v = {}; pos_ += 4; return true;
        }
        return ParseNumber(v);
    }

    bool ParseNumber(JsonValue& v) {
        char* end = nullptr;
        double d = strtod(text_.c_str() + pos_, &end);
        if (end == text_.c_str() + pos_) return false;
        v = {}; v.kind = JsonValue::Kind::Number; v.number = d;
        pos_ = static_cast<size_t>(end - text_.c_str());
        return true;
    }

    bool ParseString(JsonValue& v) {
        if (text_[pos_] != '"') return false;
        ++pos_;
        std::string out;
        while (pos_ < text_.size() && text_[pos_] != '"') {
            if (text_[pos_] == '\\' && pos_ + 1 < text_.size()) {
                ++pos_;
                char e = text_[pos_];
                switch (e) {
                    case 'n': out.push_back('\n'); break;
                    case 't': out.push_back('\t'); break;
                    case 'r': out.push_back('\r'); break;
                    default: out.push_back(e); break;
                }
            } else {
                out.push_back(text_[pos_]);
            }
            ++pos_;
        }
        if (pos_ >= text_.size()) return false;
        ++pos_; // closing quote
        v = {}; v.kind = JsonValue::Kind::String; v.text = std::move(out);
        return true;
    }

    bool ParseArray(JsonValue& v) {
        ++pos_; // '['
        v = {}; v.kind = JsonValue::Kind::Array;
        SkipWs();
        if (pos_ < text_.size() && text_[pos_] == ']') { ++pos_; return true; }
        while (true) {
            SkipWs();
            JsonValue item;
            if (!ParseValue(item)) return false;
            v.array.push_back(std::move(item));
            SkipWs();
            if (pos_ < text_.size() && text_[pos_] == ',') { ++pos_; continue; } // trailing comma tolerated
            if (pos_ < text_.size() && text_[pos_] == ']') { ++pos_; return true; }
            return false;
        }
    }

    bool ParseObject(JsonValue& v) {
        ++pos_; // '{'
        v = {}; v.kind = JsonValue::Kind::Object;
        SkipWs();
        if (pos_ < text_.size() && text_[pos_] == '}') { ++pos_; return true; }
        while (true) {
            SkipWs();
            JsonValue key;
            if (!ParseString(key)) return false;
            SkipWs();
            if (pos_ >= text_.size() || text_[pos_] != ':') return false;
            ++pos_;
            JsonValue value;
            if (!ParseValue(value)) return false;
            v.object.emplace_back(std::move(key.text), std::move(value));
            SkipWs();
            if (pos_ < text_.size() && text_[pos_] == ',') { ++pos_; continue; } // trailing comma tolerated
            if (pos_ < text_.size() && text_[pos_] == '}') { ++pos_; return true; }
            return false;
        }
    }

    std::string text_;
    size_t pos_ = 0;
    bool in_string_ = false;
};

std::optional<float> NumberField(const JsonValue& obj, const char* name) {
    const JsonValue* f = obj.Find(name);
    if (!f || f->kind != JsonValue::Kind::Number) return std::nullopt;
    return static_cast<float>(f->number);
}

std::optional<glm::vec3> Vec3Field(const JsonValue& obj, const char* name) {
    const JsonValue* f = obj.Find(name);
    if (!f || f->kind != JsonValue::Kind::Array || f->array.size() < 3) return std::nullopt;
    glm::vec3 v;
    for (int i = 0; i < 3; ++i) {
        if (f->array[i].kind != JsonValue::Kind::Number) return std::nullopt;
        v[i] = static_cast<float>(f->array[i].number);
    }
    return v;
}

} // namespace

// ---------------------------------------------------------------------------
// FogPresetEntry
// ---------------------------------------------------------------------------

bool FogPresetEntry::IsEmpty() const {
    return !density && !height_falloff && !albedo && !anisotropy && !distance &&
           !multiple_scattering && !uniform_fraction && !sun_intensity && !ambient_intensity;
}

void FogPresetEntry::LayerOver(const FogPresetEntry& fallback) {
    density = density ? density : fallback.density;
    height_falloff = height_falloff ? height_falloff : fallback.height_falloff;
    albedo = albedo ? albedo : fallback.albedo;
    anisotropy = anisotropy ? anisotropy : fallback.anisotropy;
    distance = distance ? distance : fallback.distance;
    multiple_scattering = multiple_scattering ? multiple_scattering : fallback.multiple_scattering;
    uniform_fraction = uniform_fraction ? uniform_fraction : fallback.uniform_fraction;
    sun_intensity = sun_intensity ? sun_intensity : fallback.sun_intensity;
    ambient_intensity = ambient_intensity ? ambient_intensity : fallback.ambient_intensity;
}

FogPresetEntry FogPresetEntry::Read(const std::string& json_object_body) {
    FogPresetEntry entry;
    JsonParser parser(json_object_body);
    JsonValue root;
    if (!parser.Parse(root) || root.kind != JsonValue::Kind::Object) {
        return entry; // non-object input reads as Empty, like open-igi's kind check
    }
    entry.density = NumberField(root, "density");
    entry.height_falloff = NumberField(root, "heightFalloff");
    entry.albedo = Vec3Field(root, "albedo");
    entry.anisotropy = NumberField(root, "anisotropy");
    entry.distance = NumberField(root, "distance");
    entry.multiple_scattering = NumberField(root, "multipleScattering");
    entry.uniform_fraction = NumberField(root, "uniformFraction");
    entry.sun_intensity = NumberField(root, "sunIntensity");
    entry.ambient_intensity = NumberField(root, "ambientIntensity");
    return entry;
}

// ---------------------------------------------------------------------------
// FogPresets
// ---------------------------------------------------------------------------

FogPresets FogPresets::Empty() {
    return FogPresets{};
}

FogPresets FogPresets::Load(const std::string& path, bool* ok) {
    if (ok) *ok = false;
    std::ifstream in(path, std::ios::binary);
    if (!in.good()) {
        // "No fog presets at <path>; levels derive their fog from their own sky."
        Logger::Get().Log(LogLevel::INFO,
            "[Fog] No fog presets at " + path + "; levels derive their fog from their own sky.");
        return Empty();
    }
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    FogPresets presets;
    JsonParser parser(std::move(text));
    JsonValue root;
    if (!parser.Parse(root) || root.kind != JsonValue::Kind::Object) {
        Logger::Get().Log(LogLevel::WARNING,
            "[Fog] Could not read " + path + ". Levels derive their fog from their own sky.");
        return Empty();
    }

    if (const JsonValue* defaults = root.Find("default")) {
        if (defaults->kind == JsonValue::Kind::Object) {
            // Same field set as FogPresetEntry::Read, but from an already-parsed tree.
            presets.default_.density = NumberField(*defaults, "density");
            presets.default_.height_falloff = NumberField(*defaults, "heightFalloff");
            presets.default_.albedo = Vec3Field(*defaults, "albedo");
            presets.default_.anisotropy = NumberField(*defaults, "anisotropy");
            presets.default_.distance = NumberField(*defaults, "distance");
            presets.default_.multiple_scattering = NumberField(*defaults, "multipleScattering");
            presets.default_.uniform_fraction = NumberField(*defaults, "uniformFraction");
            presets.default_.sun_intensity = NumberField(*defaults, "sunIntensity");
            presets.default_.ambient_intensity = NumberField(*defaults, "ambientIntensity");
        }
    }

    if (const JsonValue* missions = root.Find("missions")) {
        if (missions->kind == JsonValue::Kind::Object) {
            for (const auto& kv : missions->object) {
                int id = atoi(kv.first.c_str());
                char check[16];
                snprintf(check, sizeof(check), "%d", id);
                if (kv.first != check) continue; // mission keys must be integers
                if (kv.second.kind != JsonValue::Kind::Object) continue;
                FogPresetEntry entry;
                entry.density = NumberField(kv.second, "density");
                entry.height_falloff = NumberField(kv.second, "heightFalloff");
                entry.albedo = Vec3Field(kv.second, "albedo");
                entry.anisotropy = NumberField(kv.second, "anisotropy");
                entry.distance = NumberField(kv.second, "distance");
                entry.multiple_scattering = NumberField(kv.second, "multipleScattering");
                entry.uniform_fraction = NumberField(kv.second, "uniformFraction");
                entry.sun_intensity = NumberField(kv.second, "sunIntensity");
                entry.ambient_intensity = NumberField(kv.second, "ambientIntensity");
                presets.missions_.emplace_back(id, entry);
            }
        }
    }

    if (const JsonValue* volumes = root.Find("volumes")) {
        if (volumes->kind == JsonValue::Kind::Object) {
            for (const auto& kv : volumes->object) {
                int id = atoi(kv.first.c_str());
                char check[16];
                snprintf(check, sizeof(check), "%d", id);
                if (kv.first != check) continue;
                if (kv.second.kind != JsonValue::Kind::Array) continue;
                std::vector<LocalFogVolumeDef> defs;
                for (const JsonValue& element : kv.second.array) {
                    if (element.kind != JsonValue::Kind::Object) continue;
                    const JsonValue* centre = element.Find("centre");
                    const JsonValue* extent = element.Find("extent");
                    if (!centre || !extent) continue; // both required, like open-igi
                    auto centre_v = Vec3Field(element, "centre");
                    auto extent_v = Vec3Field(element, "extent");
                    if (!centre_v || !extent_v) continue;
                    LocalFogVolumeDef def;
                    def.centre = *centre_v;
                    def.extent = *extent_v;
                    auto scale = NumberField(element, "densityScale");
                    def.density_scale = scale.value_or(1.0f);
                    const JsonValue* shape = element.Find("shape");
                    def.sphere = shape && shape->kind == JsonValue::Kind::String &&
                                 shape->text == "sphere";
                    defs.push_back(def);
                    if (static_cast<int>(defs.size()) >= LocalFogVolumeDef::kMaxPerLevel) break;
                }
                presets.volumes_.emplace_back(id, std::move(defs));
            }
        }
    }

    int volume_count = 0;
    for (const auto& kv : presets.volumes_) volume_count += static_cast<int>(kv.second.size());
    Logger::Get().Log(LogLevel::INFO,
        "[Fog] Fog presets: " + std::to_string(presets.missions_.size()) + " mission(s), " +
        std::to_string(volume_count) + " volume(s).");
    if (ok) *ok = true;
    return presets;
}

ResolvedFog FogPresets::Resolve(int mission, float derived_density, float base_height, int steps) const {
    // Mission entry layered over default (open-igi: authored.Over(_default)).
    FogPresetEntry entry = default_;
    if (const FogPresetEntry* authored = EntryFor(mission)) {
        entry.LayerOver(*authored);
    }

    ResolvedFog out;
    out.steps = steps;
    out.base_height = base_height;
    // Density: an explicitly authored positive density wins; otherwise the value
    // derived from the level's own FlatSky task.
    if (entry.density && *entry.density > 0.0f) {
        out.density = *entry.density;
        out.density_authored = true;
    } else {
        out.density = derived_density;
    }
    out.height_falloff = entry.height_falloff.value_or(24.0f);
    out.albedo = entry.albedo.value_or(glm::vec3(0.0f));
    out.anisotropy = entry.anisotropy.value_or(0.7f);
    out.distance = entry.distance.value_or(500.0f);
    out.multiple_scattering = entry.multiple_scattering.value_or(0.35f);
    out.uniform_fraction = entry.uniform_fraction.value_or(0.12f);
    out.sun_intensity = entry.sun_intensity.value_or(1.0f);
    out.ambient_intensity = entry.ambient_intensity.value_or(1.0f);
    return out;
}

const FogPresetEntry* FogPresets::EntryFor(int mission) const {
    for (const auto& kv : missions_) {
        if (kv.first == mission) return &kv.second;
    }
    return nullptr;
}

const std::vector<LocalFogVolumeDef>& FogPresets::VolumesFor(int mission) const {
    static const std::vector<LocalFogVolumeDef> kNone;
    for (const auto& kv : volumes_) {
        if (kv.first == mission) return kv.second;
    }
    return kNone;
}

float FogPresets::DeriveFlatSkyDensity(float flat_sky_fog_amount, bool has_flat_sky) {
    if (!has_flat_sky) return 0.014f;
    float clamped = std::min(std::max(flat_sky_fog_amount, 0.0f), 1.0f);
    return 0.004f + clamped * 0.03f;
}

} // namespace igi
