#include "twig/repository.hpp"

#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace twig {

std::vector<std::string> Repository::status() const {
    std::vector<std::string> lines;
    lines.push_back("Repository root: " + root_.string());

    if (!is_initialized()) {
        lines.push_back("Repository is not initialized. Run: twig init");
        return lines;
    }

    lines.push_back("Current branch: " + current_branch());

    std::map<std::string, std::string> index_entries = read_index_entries();
    std::set<std::string> seen_files;
    std::filesystem::recursive_directory_iterator it(root_, std::filesystem::directory_options::skip_permission_denied);
    const std::filesystem::recursive_directory_iterator end;

    for (; it != end; ++it) {
        const auto current_path = it->path();
        if (std::filesystem::is_directory(current_path)) {
            if (current_path.filename() == ".twig") {
                it.disable_recursion_pending();
            }
            continue;
        }
        if (!std::filesystem::is_regular_file(current_path)) {
            continue;
        }
        const auto relative = normalize_rel_path(current_path);
        if (relative.empty() || relative.string() == ".twig" || is_ignored(relative)) {
            continue;
        }
        seen_files.insert(relative.string());
    }

    bool has_status = false;
    for (const auto& [path, hash] : index_entries) {
        has_status = true;
        const auto relative = normalize_rel_path(path);
        const auto abs_path = root_ / relative;
        if (!std::filesystem::exists(abs_path)) {
            lines.push_back("deleted: " + relative.string());
            continue;
        }
        const auto current_hash = blob_id_for_file(relative);
        if (current_hash == hash) {
            lines.push_back("staged: " + relative.string());
        } else {
            lines.push_back("modified: " + relative.string());
        }
        seen_files.erase(relative.string());
    }

    for (const auto& path : seen_files) {
        has_status = true;
        lines.push_back("untracked: " + path);
    }
    if (!has_status) {
        lines.push_back("No tracked files in the index.");
    }
    return lines;
}

std::vector<StatusEntry> Repository::status_entries() const {
    std::vector<StatusEntry> results;
    if (!is_initialized()) {
        return results;
    }

    const auto head_entries = read_commit_entries(read_head_commit());
    const auto index_entries = read_index_entries();
    std::set<std::string> seen_files;
    std::filesystem::recursive_directory_iterator it(root_, std::filesystem::directory_options::skip_permission_denied);
    const std::filesystem::recursive_directory_iterator end;
    for (; it != end; ++it) {
        const auto current_path = it->path();
        if (std::filesystem::is_directory(current_path)) {
            if (current_path.filename() == ".twig") {
                it.disable_recursion_pending();
            }
            continue;
        }
        if (!std::filesystem::is_regular_file(current_path)) {
            continue;
        }
        const auto relative = normalize_rel_path(current_path);
        if (relative.empty() || relative.string() == ".twig" || is_ignored(relative)) {
            continue;
        }
        seen_files.insert(relative.string());
    }

    for (const auto& [path, hash] : index_entries) {
        const auto relative = normalize_rel_path(path);
        const auto abs_path = root_ / relative;
        const auto head_it = head_entries.find(path);
        if (head_it == head_entries.end()) {
            results.push_back({"new file", relative.string()});
        } else if (head_it->second != hash) {
            results.push_back({"staged", relative.string()});
        }

        if (!std::filesystem::exists(abs_path)) {
            results.push_back({"deleted", relative.string()});
            continue;
        }
        if (blob_id_for_file(relative) != hash) {
            results.push_back({"modified", relative.string()});
        }
        seen_files.erase(relative.string());
    }

    for (const auto& path : seen_files) {
        results.push_back({"untracked", path});
    }
    return results;
}

}  // namespace twig
