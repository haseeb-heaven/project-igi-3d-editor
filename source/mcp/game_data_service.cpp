#include "game_data_service.h"

#include "mcp_transaction.h"

#include "../level/qsc_lexer.h"
#include "../level/qsc_parser.h"
#include "../level/qvm_decompiler.h"
#include "../level/qvm_parser.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace mcp {
namespace {

std::vector<std::filesystem::path> MissionDirectoryCandidatesFor(int mission_id) {
    std::vector<std::filesystem::path> candidates;
    // IGI1 retail data uses missions/location0/level1 through level14. The
    // numbered locations are retained as a compatibility layout used by the
    // editor's documented campaign fixtures.
    if (mission_id >= 1 && mission_id <= 14) {
        candidates.emplace_back(std::filesystem::path("missions") / "location0" /
                                ("level" + std::to_string(mission_id)));
    }
    if (mission_id >= 11 && mission_id <= 17)
        candidates.emplace_back(std::filesystem::path("missions") / "location1" /
                                ("level" + std::to_string(mission_id - 10)));
    if (mission_id >= 21 && mission_id <= 26)
        candidates.emplace_back(std::filesystem::path("missions") / "location2" /
                                ("level" + std::to_string(mission_id - 20)));
    if (mission_id >= 31 && mission_id <= 36)
        candidates.emplace_back(std::filesystem::path("missions") / "location3" /
                                ("level" + std::to_string(mission_id - 30)));
    switch (mission_id) {
    case 1: candidates.emplace_back("missions/multiplayer/redstone"); break;
    case 2: candidates.emplace_back("missions/multiplayer/forestraid"); break;
    case 3: candidates.emplace_back("missions/multiplayer/sandstorm"); break;
    case 4: candidates.emplace_back("missions/multiplayer/timberland"); break;
    case 5: candidates.emplace_back("missions/multiplayer/chinesetemple"); break;
    case 8: candidates.emplace_back("missions/multiplayer/jungle"); break;
    default: break;
    }
    return candidates;
}

std::string FoldPath(const std::filesystem::path& path) {
    std::string result = path.generic_string();
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return result;
}

bool IsWithinRoot(const std::filesystem::path& root, const std::filesystem::path& candidate) {
    const std::string root_text = FoldPath(root.lexically_normal());
    const std::string candidate_text = FoldPath(candidate.lexically_normal());
    if (candidate_text == root_text) return true;
    return candidate_text.size() > root_text.size() &&
           candidate_text.starts_with(root_text) &&
           candidate_text[root_text.size()] == '/';
}

bool HasTraversal(const std::filesystem::path& path) {
    for (const auto& component : path) {
        if (component == "..") return true;
    }
    return false;
}

bool ContainsReparsePoint(const std::filesystem::path& root,
                          const std::filesystem::path& relative_path) {
    std::filesystem::path current = root;
    for (const auto& component : relative_path) {
        current /= component;
#ifdef _WIN32
        const DWORD attributes = GetFileAttributesW(current.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            const DWORD last_error = GetLastError();
            if (last_error == ERROR_FILE_NOT_FOUND || last_error == ERROR_PATH_NOT_FOUND) break;
            return true;
        }
        if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) return true;
#else
        std::error_code link_error;
        if (std::filesystem::is_symlink(current, link_error) && !link_error) return true;
#endif
    }
    return false;
}

bool IsMcpBackupPath(const std::filesystem::path& path) {
    for (const auto& component : path) {
        if (FoldPath(component) == ".mcp-backups") return true;
    }
    return false;
}

void Mix(std::uint64_t& hash, std::string_view bytes) {
    constexpr std::uint64_t kPrime = 1099511628211ull;
    for (const unsigned char byte : bytes) {
        hash ^= byte;
        hash *= kPrime;
    }
}

bool HashFile(const std::filesystem::path& file, std::uint64_t& hash) {
    std::ifstream input(file, std::ios::binary);
    if (!input) return false;
    char buffer[4096];
    while (input.read(buffer, sizeof(buffer)) || input.gcount() > 0) {
        Mix(hash, std::string_view(buffer, static_cast<std::size_t>(input.gcount())));
    }
    return input.eof();
}

bool ReadBoundedText(const std::filesystem::path& file, std::string& text, std::string& error) {
    std::error_code size_error;
    const auto size = std::filesystem::file_size(file, size_error);
    if (size_error || size > kMaxJsonMessageBytes) {
        error = "source_too_large";
        return false;
    }
    std::ifstream input(file, std::ios::binary);
    if (!input) {
        error = "read_failed";
        return false;
    }
    text.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    if (!input.eof() && input.fail()) {
        error = "read_failed";
        return false;
    }
    error.clear();
    return true;
}

bool LoadObjectQsc(const ProjectScope& scope, const std::filesystem::path& level_directory,
                  std::string& source, std::string& error) {
    std::filesystem::path level_relative;
    if (!scope.RelativeToRoot(level_directory, level_relative, error)) return false;

    std::filesystem::path qsc_path;
    if (!scope.ResolveRelative(level_relative / "objects.qsc", qsc_path, error)) return false;
    std::error_code status_error;
    if (std::filesystem::is_regular_file(qsc_path, status_error) && !status_error) {
        return ReadBoundedText(qsc_path, source, error);
    }

    std::filesystem::path qvm_path;
    if (!scope.ResolveRelative(level_relative / "objects.qvm", qvm_path, error)) return false;
    const QVMFile qvm = QVM_Parse(qvm_path.string());
    if (!qvm.valid) {
        error = "qvm_invalid";
        return false;
    }
    source = QVM_DecompileToString(qvm);
    if (source.empty() && !qvm.instructions.empty()) {
        error = "qvm_decompile_failed";
        return false;
    }
    error.clear();
    return true;
}

std::string ScalarStableText(const qsc::Node& node) {
    switch (node.kind) {
    case qsc::NodeKind::IntLit: return std::to_string(node.i_val);
    case qsc::NodeKind::FloatLit: return std::to_string(node.f_val);
    case qsc::NodeKind::BoolLit: return node.b_val ? "true" : "false";
    case qsc::NodeKind::StringLit:
    case qsc::NodeKind::IdentLit: return node.s_val;
    case qsc::NodeKind::Unary:
        if (!node.children.empty()) return node.s_val + ScalarStableText(*node.children.front());
        break;
    default: break;
    }
    return {};
}

JsonValue ScalarJsonValue(const qsc::Node& node) {
    switch (node.kind) {
    case qsc::NodeKind::IntLit: return JsonValue(node.i_val);
    case qsc::NodeKind::FloatLit: return JsonValue(static_cast<double>(node.f_val));
    case qsc::NodeKind::BoolLit: return JsonValue(node.b_val);
    case qsc::NodeKind::StringLit:
    case qsc::NodeKind::IdentLit: return JsonValue(node.s_val);
    case qsc::NodeKind::Unary:
        if (node.children.size() == 1 && node.s_val == "-" &&
            (node.children.front()->kind == qsc::NodeKind::IntLit ||
             node.children.front()->kind == qsc::NodeKind::FloatLit)) {
            const JsonValue value = ScalarJsonValue(*node.children.front());
            return JsonValue(-value.as_number());
        }
        return JsonValue(node.s_val +
                         (node.children.empty() ? std::string{} : ScalarStableText(*node.children.front())));
    default: return JsonValue(nullptr);
    }
}

bool ScalarNumber(const qsc::Node& node, double& value) {
    if (node.kind == qsc::NodeKind::IntLit) {
        value = static_cast<double>(node.i_val);
        return true;
    }
    if (node.kind == qsc::NodeKind::FloatLit) {
        value = static_cast<double>(node.f_val);
        return std::isfinite(value);
    }
    if (node.kind == qsc::NodeKind::Unary && node.s_val == "-" && node.children.size() == 1) {
        if (!ScalarNumber(*node.children.front(), value)) return false;
        value = -value;
        return true;
    }
    return false;
}

const qsc::Node* ScalarChild(const qsc::Node& call, std::size_t index) {
    if (index >= call.children.size() || call.children[index]->kind == qsc::NodeKind::Call) return nullptr;
    return call.children[index].get();
}

std::string ChildString(const qsc::Node& call, std::size_t index) {
    const qsc::Node* child = ScalarChild(call, index);
    if (!child) return {};
    if (child->kind == qsc::NodeKind::StringLit || child->kind == qsc::NodeKind::IdentLit) {
        return child->s_val;
    }
    return ScalarStableText(*child);
}

struct SnapshotRecord {
    const qsc::Node* call = nullptr;
    std::string id;
    std::string parent_id;
    std::vector<std::string> children;
};

void CollectSnapshotRecords(const qsc::Node& node, const std::string& parent_id,
                            std::vector<SnapshotRecord>& records,
                            std::unordered_map<std::string, int>& id_counts) {
    if (node.kind == qsc::NodeKind::Call && node.s_val == "Task_New") {
        std::string id = ChildString(node, 0);
        if (id.empty()) id = "anonymous";
        if (id == "-1" || id == "anonymous") {
            std::string key = parent_id + "|" + ChildString(node, 1) + "|" +
                              ChildString(node, 2) + "|" + ScalarStableText(node);
            std::uint64_t hash = 14695981039346656037ull;
            Mix(hash, key);
            std::ostringstream hashed;
            hashed << "anon-" << std::hex << hash;
            id = hashed.str();
        }
        const int occurrence = id_counts[id]++;
        if (occurrence > 0) id += "#" + std::to_string(occurrence);
        records.push_back({&node, id, parent_id, {}});
        const std::size_t record_index = records.size() - 1;
        for (const auto& child : node.children) {
            CollectSnapshotRecords(*child, id, records, id_counts);
        }
        for (std::size_t index = record_index + 1; index < records.size(); ++index) {
            if (records[index].parent_id == id) records[record_index].children.push_back(records[index].id);
        }
        return;
    }
    for (const auto& child : node.children) {
        CollectSnapshotRecords(*child, parent_id, records, id_counts);
    }
}

JsonValue SnapshotRecordJson(const SnapshotRecord& record) {
    const qsc::Node& call = *record.call;
    JsonValue::Object object;
    object["id"] = record.id;
    object["parent_id"] = record.parent_id.empty() ? JsonValue(nullptr) : JsonValue(record.parent_id);
    object["function"] = call.s_val;
    object["type"] = ChildString(call, 1);
    object["name"] = ChildString(call, 2);
    object["source_line"] = static_cast<int>(call.line);

    JsonValue::Array args;
    for (const auto& child : call.children) {
        args.push_back(child->kind == qsc::NodeKind::Call ? JsonValue(nullptr) : ScalarJsonValue(*child));
    }
    object["args"] = std::move(args);

    auto numericVector = [&](std::size_t first) {
        JsonValue::Array values;
        for (std::size_t index = first; index < first + 3; ++index) {
            double value = 0.0;
            if (!ScalarChild(call, index) || !ScalarNumber(*ScalarChild(call, index), value)) return JsonValue::Array{};
            values.emplace_back(value);
        }
        return values;
    };
    object["position"] = numericVector(3);
    object["rotation_radians"] = numericVector(6);
    const std::string model_id = ChildString(call, 9);
    if (!model_id.empty()) object["model_id"] = model_id;

    JsonValue::Array children;
    for (const auto& child : record.children) children.emplace_back(child);
    object["children"] = std::move(children);
    return object;
}

bool ParseSnapshot(const std::string& source, JsonValue::Array& objects, std::string& error) {
    const qsc::LexResult lexed = qsc::Lex(source);
    if (!lexed.ok) {
        error = "qsc_lex_failed";
        return false;
    }
    const qsc::ParseResult parsed = qsc::Parse(lexed.tokens);
    if (!parsed.ok || !parsed.program) {
        error = "qsc_parse_failed";
        return false;
    }
    std::unordered_map<std::string, int> id_counts;
    std::vector<SnapshotRecord> records;
    CollectSnapshotRecords(*parsed.program, {}, records, id_counts);
    for (const auto& record : records) objects.emplace_back(SnapshotRecordJson(record));
    error.clear();
    return true;
}

}  // namespace

std::optional<ProjectScope> ProjectScope::Open(const std::filesystem::path& project_root,
                                                std::string& error) {
    std::error_code status_error;
    if (project_root.empty() || !std::filesystem::is_directory(project_root, status_error) ||
        status_error) {
        error = "invalid_project_root";
        return std::nullopt;
    }

    std::error_code canonical_error;
    const std::filesystem::path canonical_root = std::filesystem::canonical(project_root, canonical_error);
    if (canonical_error || !std::filesystem::is_directory(canonical_root, status_error) || status_error) {
        error = "invalid_project_root";
        return std::nullopt;
    }

    error.clear();
    return ProjectScope(canonical_root);
}

bool ProjectScope::ResolveRelative(const std::filesystem::path& relative_path,
                                   std::filesystem::path& resolved_path,
                                   std::string& error) const {
    if (relative_path.empty() || relative_path.is_absolute() || relative_path.has_root_name() ||
        HasTraversal(relative_path)) {
        error = "path_forbidden";
        return false;
    }
    if (ContainsReparsePoint(root_, relative_path)) {
        error = "path_forbidden";
        return false;
    }

    std::error_code canonical_error;
    const std::filesystem::path candidate =
        std::filesystem::weakly_canonical(root_ / relative_path, canonical_error);
    if (canonical_error || !IsWithinRoot(root_, candidate)) {
        error = "path_forbidden";
        return false;
    }

    resolved_path = candidate;
    error.clear();
    return true;
}

bool ProjectScope::RelativeToRoot(const std::filesystem::path& path,
                                  std::filesystem::path& relative_path,
                                  std::string& error) const {
    if (path.empty()) {
        error = "path_forbidden";
        return false;
    }

    std::error_code canonical_error;
    const std::filesystem::path candidate = std::filesystem::weakly_canonical(path, canonical_error);
    if (canonical_error || !IsWithinRoot(root_, candidate)) {
        error = "path_forbidden";
        return false;
    }

    relative_path = candidate.lexically_relative(root_);
    if (relative_path.empty() || HasTraversal(relative_path)) {
        error = "path_forbidden";
        return false;
    }
    error.clear();
    return true;
}

bool ProjectScope::LevelDirectory(int level, std::filesystem::path& level_directory,
                                  std::string& error) const {
    const std::vector<std::filesystem::path> candidates = MissionDirectoryCandidatesFor(level);
    if (candidates.empty()) {
        error = "invalid_level";
        return false;
    }

    std::vector<std::filesystem::path> ordered_candidates = candidates;
    std::filesystem::path igi2_manifest;
    std::string manifest_error;
    const bool is_igi2 = ResolveRelative("missions/igi2.qvm", igi2_manifest, manifest_error) &&
                         std::filesystem::is_regular_file(igi2_manifest);
    if (is_igi2) {
        std::stable_partition(ordered_candidates.begin(), ordered_candidates.end(),
                              [](const auto& candidate) {
                                  return candidate.generic_string().starts_with("missions/multiplayer/");
                              });
    }

    for (const auto& mission_path : ordered_candidates) {
        std::filesystem::path candidate_directory;
        if (!ResolveRelative(mission_path, candidate_directory, error)) return false;

        std::filesystem::path objects_qvm;
        if (!ResolveRelative(mission_path / "objects.qvm", objects_qvm, error)) return false;

        std::error_code status_error;
        if (std::filesystem::is_directory(candidate_directory, status_error) && !status_error &&
            std::filesystem::is_regular_file(objects_qvm, status_error) && !status_error) {
            level_directory = std::move(candidate_directory);
            error.clear();
            return true;
        }
    }
    error = "invalid_level";
    return false;
}

bool GameDataService::CalculateRevision(int level, const std::filesystem::path& level_directory,
                                        LevelRevision& revision, std::string& error) const {
    std::vector<std::filesystem::path> files;
    std::error_code iterator_error;
    for (std::filesystem::recursive_directory_iterator iterator(
             level_directory, std::filesystem::directory_options::skip_permission_denied, iterator_error),
         end;
         iterator != end;
         iterator.increment(iterator_error)) {
        if (iterator_error) {
            error = "revision_failed";
            return false;
        }
        std::error_code type_error;
        if (iterator->is_regular_file(type_error) && !type_error) files.push_back(iterator->path());
    }
    if (iterator_error) {
        error = "revision_failed";
        return false;
    }

    std::sort(files.begin(), files.end(), [](const auto& left, const auto& right) {
        return FoldPath(left) < FoldPath(right);
    });

    std::uint64_t hash = 14695981039346656037ull;
    for (const auto& file : files) {
        std::filesystem::path relative;
        if (!scope_.RelativeToRoot(file, relative, error)) return false;
        if (IsMcpBackupPath(relative)) continue;
        Mix(hash, relative.generic_string());
        Mix(hash, std::string_view("\0", 1));
        if (!HashFile(file, hash)) {
            error = "revision_failed";
            return false;
        }
        Mix(hash, std::string_view("\0", 1));
    }

    std::ostringstream fingerprint;
    fingerprint << std::hex << std::setw(16) << std::setfill('0') << hash;
    revision = LevelRevision{level, fingerprint.str()};
    error.clear();
    return true;
}

bool GameDataService::OpenLevel(int level, std::string& error) {
    std::lock_guard<std::mutex> lock(*mutation_mutex_);
    std::filesystem::path level_directory;
    if (!scope_.LevelDirectory(level, level_directory, error)) return false;

    LevelRevision revision;
    if (!CalculateRevision(level, level_directory, revision, error)) return false;
    current_revision_ = std::move(revision);
    error.clear();
    return true;
}

bool GameDataService::RefreshRevisionUnlocked(std::string& error) {
    if (!current_revision_) {
        error = "level_not_open";
        return false;
    }

    std::filesystem::path level_directory;
    if (!scope_.LevelDirectory(current_revision_->level, level_directory, error)) return false;
    LevelRevision revision;
    if (!CalculateRevision(current_revision_->level, level_directory, revision, error)) return false;
    current_revision_ = std::move(revision);
    error.clear();
    return true;
}

bool GameDataService::RefreshRevision(std::string& error) {
    std::lock_guard<std::mutex> lock(*mutation_mutex_);
    return RefreshRevisionUnlocked(error);
}

LevelRevision GameDataService::CurrentRevision() const {
    std::lock_guard<std::mutex> lock(*mutation_mutex_);
    if (!current_revision_) throw std::logic_error("level is not open");
    return *current_revision_;
}

JsonValue GameDataService::ProjectInfo() const {
    std::lock_guard<std::mutex> lock(*mutation_mutex_);
    JsonValue::Object info;
    info["schema_version"] = "1";
    info["project_type"] = "igi1";
    std::filesystem::path igi2_manifest;
    std::string ignored;
    if (scope_.ResolveRelative("missions/igi2.qvm", igi2_manifest, ignored) &&
        std::filesystem::is_regular_file(igi2_manifest)) {
        info["project_type"] = "igi2";
    }
    info["root_kind"] = "configured_game_root";
    info["protocol_profile"] = "2026-07-28";
    info["game_data_only"] = true;
    info["capabilities"] = JsonValue::Object{
        {"levels", true}, {"reload", true}, {"validate", true}, {"snapshots", true}};
    if (current_revision_) {
        info["current_level"] = current_revision_->level;
        info["current_revision"] = current_revision_->fingerprint;
    } else {
        info["current_level"] = JsonValue(nullptr);
        info["current_revision"] = JsonValue(nullptr);
    }
    return info;
}

bool GameDataService::HasOpenLevel() const {
    std::lock_guard<std::mutex> lock(*mutation_mutex_);
    return current_revision_.has_value();
}

JsonValue GameDataService::ListLevels(std::string& error) const {
    static constexpr int kMissionIds[] = {
        1, 2, 3, 4, 5, 8, 11, 12, 13, 14, 15, 16, 17,
        21, 22, 23, 24, 25, 26, 31, 32, 33, 34, 35, 36};
    JsonValue::Array levels;
    for (const int level : kMissionIds) {
        std::filesystem::path directory;
        std::string level_error;
        if (!scope_.LevelDirectory(level, directory, level_error)) continue;
        std::filesystem::path relative;
        if (!scope_.RelativeToRoot(directory, relative, error)) return JsonValue(nullptr);
        const std::string relative_text = relative.generic_string();
        std::string layout = "unknown";
        if (relative_text.find("/multiplayer/") != std::string::npos) layout = "multiplayer";
        else if (relative_text.find("/location0/") != std::string::npos) layout = "location0";
        else if (relative_text.find("/location1/") != std::string::npos) layout = "location1";
        else if (relative_text.find("/location2/") != std::string::npos) layout = "location2";
        else if (relative_text.find("/location3/") != std::string::npos) layout = "location3";

        JsonValue::Object entry;
        entry["level"] = level;
        entry["layout"] = layout;
        entry["relative_path"] = relative_text;
        entry["available"] = true;
        JsonValue::Object files;
        std::error_code status_error;
        files["objects_qvm"] = std::filesystem::is_regular_file(directory / "objects.qvm", status_error) &&
                                 !status_error;
        status_error.clear();
        files["mission_qvm"] = std::filesystem::is_regular_file(directory / "mission.qvm", status_error) &&
                                 !status_error;
        status_error.clear();
        files["terrain"] = std::filesystem::is_directory(directory / "terrain", status_error) &&
                             !status_error;
        status_error.clear();
        files["graphs"] = std::filesystem::is_directory(directory / "graphs", status_error) &&
                           !status_error;
        entry["files"] = std::move(files);
        levels.emplace_back(std::move(entry));
    }
    error.clear();
    return JsonValue(std::move(levels));
}

JsonValue GameDataService::LevelManifest(int level, std::string& error) const {
    std::filesystem::path level_directory;
    if (!scope_.LevelDirectory(level, level_directory, error)) return JsonValue(nullptr);
    LevelRevision revision;
    if (!CalculateRevision(level, level_directory, revision, error)) return JsonValue(nullptr);

    JsonValue::Array files;
    std::vector<std::filesystem::path> paths;
    std::error_code iterator_error;
    for (std::filesystem::recursive_directory_iterator iterator(
             level_directory, std::filesystem::directory_options::skip_permission_denied, iterator_error),
         end;
         iterator != end; iterator.increment(iterator_error)) {
        if (iterator_error) {
            error = "manifest_failed";
            return JsonValue(nullptr);
        }
        std::filesystem::path relative;
        if (!scope_.RelativeToRoot(iterator->path(), relative, error)) return JsonValue(nullptr);
        if (IsMcpBackupPath(relative)) continue;
        std::error_code type_error;
        if (iterator->is_regular_file(type_error) && !type_error) paths.push_back(iterator->path());
    }
    if (iterator_error) {
        error = "manifest_failed";
        return JsonValue(nullptr);
    }
    std::sort(paths.begin(), paths.end(), [this](const auto& left, const auto& right) {
        return FoldPath(left) < FoldPath(right);
    });
    for (const auto& path : paths) {
        std::filesystem::path relative;
        if (!scope_.RelativeToRoot(path, relative, error)) return JsonValue(nullptr);
        std::error_code size_error;
        const auto size = std::filesystem::file_size(path, size_error);
        if (size_error) {
            error = "manifest_failed";
            return JsonValue(nullptr);
        }
        files.emplace_back(JsonValue::Object{
            {"path", relative.generic_string()}, {"bytes", static_cast<double>(size)}});
    }

    JsonValue::Object manifest;
    manifest["level"] = level;
    std::filesystem::path level_relative;
    if (!scope_.RelativeToRoot(level_directory, level_relative, error)) return JsonValue(nullptr);
    manifest["relative_path"] = level_relative.generic_string();
    manifest["revision"] = revision.fingerprint;
    manifest["files"] = std::move(files);
    error.clear();
    return manifest;
}

JsonValue GameDataService::ListObjects(int level, std::string& error) const {
    std::filesystem::path level_directory;
    if (!scope_.LevelDirectory(level, level_directory, error)) return JsonValue(nullptr);
    LevelRevision revision;
    if (!CalculateRevision(level, level_directory, revision, error)) return JsonValue(nullptr);
    std::string source;
    if (!LoadObjectQsc(scope_, level_directory, source, error)) return JsonValue(nullptr);
    JsonValue::Array objects;
    if (!ParseSnapshot(source, objects, error)) return JsonValue(nullptr);
    JsonValue::Object result;
    result["level"] = level;
    result["revision"] = revision.fingerprint;
    result["objects"] = std::move(objects);
    error.clear();
    return result;
}

JsonValue GameDataService::GetObject(int level, std::string_view task_id, std::string& error) const {
    if (task_id.empty() || task_id.size() > 128) {
        error = "invalid_task_id";
        return JsonValue(nullptr);
    }
    const JsonValue snapshot = ListObjects(level, error);
    if (!error.empty()) return JsonValue(nullptr);
    for (const auto& object : snapshot.at("objects").as_array()) {
        if (object.at("id").as_string() == task_id) {
            error.clear();
            return object;
        }
    }
    error = "unknown_task_id";
    return JsonValue(nullptr);
}

JsonValue GameDataService::ValidateLevel(int level, std::string& error) const {
    std::filesystem::path level_directory;
    if (!scope_.LevelDirectory(level, level_directory, error)) return JsonValue(nullptr);
    LevelRevision revision;
    if (!CalculateRevision(level, level_directory, revision, error)) return JsonValue(nullptr);

    JsonValue::Array checks;
    JsonValue::Array failures;
    bool valid = true;
    const auto addCheck = [&](std::string name, bool passed, std::string code,
                              std::string summary) {
        checks.emplace_back(JsonValue::Object{
            {"name", std::move(name)},
            {"status", passed ? JsonValue("passed") : JsonValue("failed")},
            {"code", code.empty() ? JsonValue(nullptr) : JsonValue(std::move(code))},
            {"summary", std::move(summary)},
        });
        if (!passed) {
            valid = false;
            failures.emplace_back(JsonValue::Object{
                {"code", checks.back().at("code")},
                {"field", JsonValue(nullptr)},
                {"summary", checks.back().at("summary")},
            });
        }
    };

    std::filesystem::path level_relative;
    if (!scope_.RelativeToRoot(level_directory, level_relative, error)) return JsonValue(nullptr);
    std::filesystem::path qvm_path;
    if (!scope_.ResolveRelative(level_relative / "objects.qvm", qvm_path, error)) return JsonValue(nullptr);
    const QVMFile qvm = QVM_Parse(qvm_path.string());
    addCheck("objects_qvm", qvm.valid, qvm.valid ? "" : "qvm_invalid",
             qvm.valid ? "objects.qvm parsed" : "objects.qvm failed validation");

    std::string source;
    std::string source_error;
    const bool source_loaded = LoadObjectQsc(scope_, level_directory, source, source_error);
    if (!source_loaded) {
        addCheck("objects_qsc", false, source_error, "objects source could not be loaded");
    } else {
        JsonValue::Array ignored_objects;
        const bool parsed = ParseSnapshot(source, ignored_objects, source_error);
        addCheck("objects_qsc", parsed, parsed ? "" : source_error,
                 parsed ? "objects source lexed and parsed" : "objects source failed validation");
    }

    JsonValue::Object result;
    result["level"] = level;
    result["revision"] = revision.fingerprint;
    result["valid"] = valid;
    result["checks"] = std::move(checks);
    result["errors"] = std::move(failures);
    error.clear();
    return result;
}

std::unique_ptr<Transaction> GameDataService::BeginMutation(const MutationOptions& options,
                                                             std::string& error) {
    std::unique_lock<std::mutex> mutation_lock(*mutation_mutex_);
    if (!current_revision_) {
        error = "level_not_open";
        return nullptr;
    }

    const std::string cached_revision = current_revision_->fingerprint;
    if (!RefreshRevisionUnlocked(error)) return nullptr;
    if (current_revision_->fingerprint != cached_revision) {
        error = "stale_revision";
        return nullptr;
    }
    if (options.expected_revision && *options.expected_revision != current_revision_->fingerprint) {
        error = "stale_revision";
        return nullptr;
    }

    std::filesystem::path level_directory;
    if (!scope_.LevelDirectory(current_revision_->level, level_directory, error)) return nullptr;
    std::filesystem::path allowed_prefix;
    if (!scope_.RelativeToRoot(level_directory, allowed_prefix, error)) return nullptr;
    error.clear();
    const std::string baseline_revision = current_revision_->fingerprint;
    auto transaction = std::make_unique<Transaction>(scope_, options, std::move(allowed_prefix),
                                                      mutation_mutex_, std::move(mutation_lock));
    transaction->SetCommitGuard([this, baseline_revision](std::string& guard_error) {
        if (!RefreshRevisionUnlocked(guard_error)) return false;
        if (!current_revision_ || current_revision_->fingerprint != baseline_revision) {
            guard_error = "stale_revision";
            return false;
        }
        return true;
    });
    transaction->SetCommitObserver([this]() {
        std::string ignored;
        RefreshRevisionUnlocked(ignored);
    });
    return transaction;
}

}  // namespace mcp
