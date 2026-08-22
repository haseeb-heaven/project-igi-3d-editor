#include "sound_bank.h"
#include "../logger.h"
#include "../renderer/res_writer.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace igi {

std::string SoundNameToKey(const std::string& path) {
    // SoundName.cs ToKey: key is the bare stem before the FIRST dot, after the
    // last path separator, upper-cased.
    const size_t dot = path.find('.');
    if (dot == std::string::npos || dot == 0) {
        return {};
    }
    size_t start = 0;
    for (size_t i = dot; i-- > 0;) {
        const char c = path[i];
        if (c == '/' || c == '\\' || c == ':') {
            start = i + 1;
            break;
        }
    }
    if (start >= dot) {
        return {};
    }
    std::string key = path.substr(start, dot - start);
    std::transform(key.begin(), key.end(), key.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return key;
}

bool SoundBank::TryGet(const std::string& name, SoundBankEntry& out) const {
    if (name.empty()) return false;
    std::string key = name;
    std::transform(key.begin(), key.end(), key.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    const auto it = sounds_.find(key);
    if (it == sounds_.end()) return false;
    out = it->second;
    return true;
}

namespace {

std::vector<uint8_t> ReadWholeFileBytes(const std::string& filepath) {
    std::ifstream in(filepath, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + filepath);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)),
                                std::istreambuf_iterator<char>());
}

} // namespace

bool SoundBank::Add(const std::string& path_or_key_hint, const uint8_t* data, size_t size) {
    const std::string key = SoundNameToKey(path_or_key_hint);
    if (key.empty()) {
        Logger::Get().Log(LogLevel::WARNING,
                          "[Audio] Invalid sound filename '" + path_or_key_hint + "'.");
        return false;
    }
    if (sounds_.count(key) != 0) {
        // First mount wins — the original logs "SOUND - File %s already loaded!" and
        // keeps the first, so a mission's sounds cannot shadow a common one.
        Logger::Get().Log(LogLevel::DEBUG, "[Audio] Sound '" + key + "' already loaded; keeping the first.");
        return false;
    }

    IlsfSoundHeader header;
    if (!ParseIlsfHeader(data, size, header)) {
        Logger::Get().Log(LogLevel::WARNING,
                          "[Audio] Sound '" + path_or_key_hint + "' is not a valid ILSF format.");
        return false;
    }

    SoundBankEntry entry;
    entry.name = key;
    entry.origin = path_or_key_hint;
    entry.header = header;
    entry.resident = !header.IsStreamed();
    if (entry.resident) {
        if (!DecodeIlsfPcm(data, size, entry.pcm)) {
            Logger::Get().Log(LogLevel::WARNING,
                              "[Audio] Sound '" + path_or_key_hint + "' would not decode.");
            return false;
        }
    }
    sounds_[key] = std::move(entry);
    return true;
}

int SoundBank::MountDirectory(const std::string& directory) {
    namespace fs = std::filesystem;
    int added = 0;
    std::error_code ec;
    if (!fs::exists(directory, ec)) {
        // Not an error: the original's walker simply finds nothing, and most
        // missions ship no sounds of their own.
        return 0;
    }
    for (const auto& item : fs::directory_iterator(directory, ec)) {
        if (!item.is_regular_file()) continue;
        std::string ext = item.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        bool ok = false;
        try {
            const std::vector<uint8_t> bytes = ReadWholeFileBytes(item.path().string());
            if (ext == ".wav") {
                ok = IsIlsf(bytes.data(), bytes.size()) && Add(item.path().filename().string(),
                                                              bytes.data(), bytes.size());
            } else if (ext == ".res") {
                added += MountArchive(item.path().string());
                continue;
            }
            added += ok ? 1 : 0;
        } catch (const std::exception&) {
            continue; // unreadable file: skip, as the original's walker does
        }
    }
    mounted_.push_back(directory);
    Logger::Get().Log(LogLevel::DEBUG,
                      "[Audio] Mounted '" + directory + "': " + std::to_string(added) + " sound(s).");
    return added;
}

int SoundBank::MountArchive(const std::string& path) {
    if (!std::filesystem::exists(path)) {
        return 0;
    }
    int added = 0;
    std::string err;
    RES_ForEachEntry(path, [&](const std::string& name, const uint8_t* data, size_t size) {
        if (IsIlsf(data, size) && Add(name, data, size)) {
            ++added;
        }
    }, err);
    if (std::find(mounted_.begin(), mounted_.end(), path) == mounted_.end()) {
        mounted_.push_back(path);
    }
    return added;
}

void SoundBank::Clear() {
    sounds_.clear();
    mounted_.clear();
}

} // namespace igi
