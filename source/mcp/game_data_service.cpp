#include "game_data_service.h"

#include "mcp_transaction.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>
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
