#include "magic_object_registry.h"
#include "../logger.h"
#include "../utils.h"
#include "../level/qvm_parser.h"
#include "../level/qvm_decompiler.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <sstream>

namespace igi {

namespace {

std::string ToLowerKey(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

// Extracts the Nth double-quoted field of a line; returns "" when absent.
std::string QuotedField(const std::string& line, int index) {
    size_t pos = 0;
    for (int i = 0; i <= index; ++i) {
        const size_t q1 = line.find('"', pos);
        if (q1 == std::string::npos) return "";
        const size_t q2 = line.find('"', q1 + 1);
        if (q2 == std::string::npos) return "";
        if (i == index) return line.substr(q1 + 1, q2 - q1 - 1);
        pos = q2 + 1;
    }
    return "";
}

} // namespace

MagicObjectRegistry& MagicObjectRegistry::Get() {
    static MagicObjectRegistry s_instance;
    return s_instance;
}

void MagicObjectRegistry::Clear() {
    by_name_.clear();
    task_type_names_.clear();
    loaded_ = false;
}

int MagicObjectRegistry::LoadFromDecompiledText(const std::string& decompiled_qsc) {
    int registered = 0;
    std::istringstream ss(decompiled_qsc);
    std::string line;
    while (std::getline(ss, line)) {
        if (line.find("DefineMagicObj") == std::string::npos) continue;

        MagicObjectDefinition def;
        def.name = QuotedField(line, 0);
        def.model = QuotedField(line, 1);
        if (def.name.empty() || def.model.empty()) continue;

        // Third argument: TASKTYPE_* constant, when present in the decompiled text.
        const size_t tt = line.find("TASKTYPE_");
        if (tt != std::string::npos) {
            const size_t start = tt;
            size_t end = start;
            while (end < line.size() &&
                   (std::isalnum(static_cast<unsigned char>(line[end])) || line[end] == '_')) {
                ++end;
            }
            def.task_type_name = line.substr(start, end - start);

            // First-seen allocation order for the numeric id (see header note).
            auto it = std::find_if(task_type_names_.begin(), task_type_names_.end(),
                                   [&](const auto& kv) { return kv.second == def.task_type_name; });
            if (it != task_type_names_.end()) {
                def.task_type_id = it->first;
            } else {
                def.task_type_id = static_cast<int>(task_type_names_.size());
                task_type_names_[def.task_type_id] = def.task_type_name;
            }
        }

        // The original appends unconditionally and its lookup takes the first match,
        // so a repeated name keeps the EARLIER definition.
        const std::string key = ToLowerKey(def.name);
        if (by_name_.find(key) == by_name_.end()) {
            by_name_[key] = def;
        }
        ++registered;
    }
    return registered;
}

void MagicObjectRegistry::EnsureLoaded() {
    if (loaded_) return;
    loaded_ = true;

    const std::string qvm_path =
        Utils::GetIGIRootPath() + "\\magicobj\\magicobj.qvm";
    if (!std::filesystem::exists(qvm_path)) {
        Logger::Get().Log(LogLevel::WARNING,
            "[MagicObjects] No magic-object script at '" + qvm_path +
            "'; models attach nothing (retail Load-missing semantics).");
        return;
    }

    QVMFile qvm = QVM_Parse(qvm_path);
    if (!qvm.valid) {
        Logger::Get().Log(LogLevel::WARNING,
            "[MagicObjects] Failed to parse magicobj.qvm: " + qvm.error);
        return;
    }

    const std::string src = QVM_DecompileToString(qvm);
    const int registered = LoadFromDecompiledText(src);
    Logger::Get().Log(LogLevel::INFO,
        "[MagicObjects] Registered " + std::to_string(by_name_.size()) +
        " magic object(s) from " + std::to_string(registered) + " DefineMagicObj call(s).");
}

bool MagicObjectRegistry::TryGet(const std::string& attachment_name,
                                 MagicObjectDefinition& out) const {
    const auto it = by_name_.find(ToLowerKey(attachment_name));
    if (it == by_name_.end()) return false;
    out = it->second;
    return true;
}

int MagicObjectRegistry::Count() const {
    return static_cast<int>(by_name_.size());
}

std::string MagicObjectRegistry::TaskTypeName(int task_type_id) const {
    const auto it = task_type_names_.find(task_type_id);
    return it != task_type_names_.end() ? it->second : std::string();
}

} // namespace igi
