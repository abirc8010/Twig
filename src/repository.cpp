#include "twig/repository.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <utility>

namespace twig {

namespace {

std::string shell_quote(const std::filesystem::path& value) {
    const auto raw = value.string();
    std::string quoted = "'";
    for (char ch : raw) {
        if (ch == '\'') {
            quoted += "'\\''";
        } else {
            quoted.push_back(ch);
        }
    }
    quoted.push_back('\'');
    return quoted;
}

bool git_available() {
    return std::system("git --version >/dev/null 2>&1") == 0;
}

}  // namespace

Repository::Repository(std::filesystem::path root)
    : root_(std::move(root)), dot_twig_(root_ / ".twig") {}

bool Repository::init() {
    std::filesystem::create_directories(root_ / ".twig" / "objects");
    std::filesystem::create_directories(root_ / ".twig" / "refs" / "heads");

    const auto head_path = this->head_path();
    std::filesystem::create_directories(head_path.parent_path());

    std::ofstream head_file(head_path, std::ios::trunc);
    if (!head_file) {
        std::cerr << "Failed to create HEAD file." << std::endl;
        return false;
    }
    head_file << "refs/heads/main\n";
    head_file.close();

    const auto branch_path = branch_ref_path("main");
    std::filesystem::create_directories(branch_path.parent_path());

    std::ofstream branch_file(branch_path, std::ios::trunc);
    if (!branch_file) {
        std::cerr << "Failed to create branch reference file." << std::endl;
        return false;
    }
    branch_file << "0000000000000000000000000000000000000000\n";
    branch_file.close();

    std::ofstream index_file(index_path(), std::ios::trunc);
    if (!index_file) {
        std::cerr << "Failed to create index file." << std::endl;
        return false;
    }
    index_file.close();

    return true;
}

bool Repository::is_initialized() const {
    return std::filesystem::exists(dot_twig_) && std::filesystem::exists(head_path()) &&
           std::filesystem::exists(object_dir());
}

std::filesystem::path Repository::root() const {
    return root_;
}

std::string Repository::current_branch() const {
    std::ifstream head_file(head_path());
    if (!head_file) {
        return "main";
    }

    std::string branch_ref;
    std::getline(head_file, branch_ref);
    if (branch_ref.empty()) {
        return "main";
    }

    const auto pos = branch_ref.rfind('/');
    if (pos == std::string::npos) {
        return branch_ref;
    }
    return branch_ref.substr(pos + 1);
}

bool Repository::add_file(const std::filesystem::path& relative_path, bool quiet) {
    return add_files({relative_path}, quiet);
}

bool Repository::add_files(const std::vector<std::filesystem::path>& relative_paths, bool quiet) {
    if (!is_initialized()) {
        std::cerr << "Repository is not initialized" << std::endl;
        return false;
    }

    (void)quiet;
    auto entries = read_index_entries();
    const auto ignore_patterns = read_ignore_patterns();
    bool all_added = true;
    std::vector<std::filesystem::path> files_to_stage;

    const auto ignored_by_patterns = [&](const std::filesystem::path& path) {
        const auto path_string = path.generic_string();
        for (const auto& pattern : ignore_patterns) {
            if (pattern.empty() || pattern[0] == '#') {
                continue;
            }
            if ((pattern.back() == '/' && path_string.rfind(pattern.substr(0, pattern.size() - 1), 0) == 0) ||
                matches_ignore_pattern(path_string, pattern)) {
                return true;
            }
        }
        return false;
    };

    for (const auto& relative_path : relative_paths) {
        const auto normalized = normalize_rel_path(relative_path);
        const auto abs_path = root_ / normalized;
        std::error_code error;
        if (std::filesystem::is_directory(abs_path, error)) {
            if (normalized == ".twig" || ignored_by_patterns(normalized)) {
                continue;
            }
            std::filesystem::recursive_directory_iterator it(
                abs_path, std::filesystem::directory_options::skip_permission_denied, error);
            const std::filesystem::recursive_directory_iterator end;
            if (error) {
                std::cerr << "Unable to traverse directory: " << normalized << std::endl;
                all_added = false;
                continue;
            }
            for (; it != end; it.increment(error)) {
                if (error) {
                    all_added = false;
                    error.clear();
                    continue;
                }
                const auto entry_path = it->path();
                const auto entry_relative = normalize_rel_path(entry_path);
                std::error_code entry_error;
                if (it->is_directory(entry_error)) {
                    if (entry_relative.filename() == ".twig" || ignored_by_patterns(entry_relative)) {
                        it.disable_recursion_pending();
                    }
                    continue;
                }
                if (!entry_error && it->is_regular_file(entry_error) && !ignored_by_patterns(entry_relative)) {
                    files_to_stage.push_back(entry_relative);
                }
            }
            continue;
        }

        if (!std::filesystem::is_regular_file(abs_path, error)) {
            std::cerr << "File not found: " << normalized << std::endl;
            all_added = false;
            continue;
        }
        files_to_stage.push_back(normalized);
    }

    for (const auto& normalized : files_to_stage) {
        const auto abs_path = root_ / normalized;
        const auto normalized_string = normalized.generic_string();

        std::error_code error;
        const auto file_size = std::filesystem::file_size(abs_path, error);
        if (error || file_size > static_cast<uintmax_t>(std::numeric_limits<std::streamsize>::max()) ||
            file_size > static_cast<uintmax_t>(std::numeric_limits<size_t>::max())) {
            std::cerr << "Unable to determine file size: " << normalized << std::endl;
            all_added = false;
            continue;
        }

        std::string content(static_cast<size_t>(file_size), '\0');
        std::ifstream input(abs_path, std::ios::binary);
        if (!input || (!content.empty() && !input.read(content.data(), static_cast<std::streamsize>(content.size())))) {
            std::cerr << "Unable to read file: " << normalized << std::endl;
            all_added = false;
            continue;
        }

        std::string blob_id;
        if (!write_object("blob", content, blob_id)) {
            all_added = false;
            continue;
        }
        entries[normalized_string] = blob_id;
    }

    return write_index_entries(entries) && all_added;
}

std::string Repository::blob_id_for_file(const std::filesystem::path& relative_path) const {
    const auto abs_path = root_ / normalize_rel_path(relative_path);
    std::error_code error;
    const auto file_size = std::filesystem::file_size(abs_path, error);
    if (error || file_size > static_cast<uintmax_t>(std::numeric_limits<std::streamsize>::max()) ||
        file_size > static_cast<uintmax_t>(std::numeric_limits<size_t>::max())) {
        return "missing";
    }

    std::string content(static_cast<size_t>(file_size), '\0');
    std::ifstream file(abs_path, std::ios::binary);
    if (!file || (!content.empty() && !file.read(content.data(), static_cast<std::streamsize>(content.size())))) {
        return "missing";
    }
    const auto object_data = "blob " + std::to_string(content.size()) + "\n" + content;
    return hash_bytes(object_data);
}

bool Repository::commit(const std::string& message, const std::string& author) {
    if (!is_initialized()) {
        return false;
    }

    const auto entries = read_index_entries();
    std::string tree_payload;
    for (const auto& [path, blob_hash] : entries) {
        tree_payload += path + " " + blob_hash + "\n";
    }
    std::string tree_hash;
    if (!write_object("tree", tree_payload, tree_hash)) {
        return false;
    }
    const auto parent_hash = read_head_commit();
    const auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                               std::chrono::system_clock::now().time_since_epoch())
                               .count();

    std::string payload = "tree=" + tree_hash + "\n";
    if (!parent_hash.empty() && parent_hash != "0000000000000000000000000000000000000000") {
        payload += "parent=" + parent_hash + "\n";
    }
    payload += "author=" + author + "\n";
    payload += "timestamp=" + std::to_string(timestamp) + "\n";
    payload += "message=" + message + "\n";

    for (const auto& [path, blob_hash] : entries) {
        payload += "entry=" + path + ":" + blob_hash + "\n";
    }

    std::string commit_hash;
    if (!write_object("commit", payload, commit_hash)) {
        return false;
    }

    const auto branch_path = branch_ref_path(current_branch());
    std::ofstream ref_file(branch_path, std::ios::trunc);
    if (!ref_file) {
        return false;
    }
    ref_file << commit_hash << '\n';
    ref_file.close();

    return true;
}

bool Repository::branch(const std::string& name) const {
    if (!is_initialized() || name.empty()) {
        return false;
    }

    const auto branch_path = branch_ref_path(name);
    if (std::filesystem::exists(branch_path)) {
        return false;
    }

    const auto current_hash = read_head_commit();
    std::ofstream ref_file(branch_path, std::ios::trunc);
    if (!ref_file) {
        return false;
    }
    ref_file << (current_hash.empty() ? "0000000000000000000000000000000000000000" : current_hash) << '\n';
    return true;
}

bool Repository::checkout(const std::string& target) {
    if (!is_initialized() || target.empty()) {
        return false;
    }

    std::string commit_hash = target;
    std::string branch_name = target;

    const auto target_branch_path = branch_ref_path(target);
    if (std::filesystem::exists(target_branch_path)) {
        commit_hash = read_ref_value(target_branch_path.string());
        branch_name = target;
    } else {
        const auto branch_ref = read_ref_value((refs_dir() / "heads" / target).string());
        if (!branch_ref.empty()) {
            commit_hash = branch_ref;
            branch_name = target;
        }
    }

    if (commit_hash.empty() || commit_hash == "0000000000000000000000000000000000000000") {
        auto head_file = std::ofstream(head_path(), std::ios::trunc);
        if (branch_name != current_branch()) {
            head_file << "refs/heads/" + branch_name + "\n";
        }
        return true;
    }

    const auto payload = read_object(commit_hash);
    if (payload.empty()) {
        return false;
    }

    std::map<std::string, std::string> entries;
    std::istringstream stream(payload);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.rfind("entry=", 0) == 0) {
            const auto value = line.substr(6);
            const auto colon = value.find(':');
            if (colon != std::string::npos) {
                const auto path = value.substr(0, colon);
                const auto blob_hash = value.substr(colon + 1);
                entries[path] = blob_hash;
            }
        }
    }

    std::ofstream index_file(index_path(), std::ios::trunc);
    for (const auto& [path, blob_hash] : entries) {
        const auto normalized = normalize_rel_path(path);
        const auto file_path = root_ / normalized;
        const auto parent = file_path.parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent);
        }

        const auto blob_data = read_blob(blob_hash);
        std::ofstream out(file_path, std::ios::binary | std::ios::trunc);
        if (out) {
            out.write(blob_data.data(), static_cast<std::streamsize>(blob_data.size()));
        }

        index_file << path << ' ' << blob_hash << '\n';
    }
    index_file.close();

    const auto head_target = branch_ref_path(branch_name);
    if (std::filesystem::exists(head_target)) {
        std::ofstream head_out(head_path(), std::ios::trunc);
        head_out << "refs/heads/" + branch_name + "\n";
    }

    return true;
}

MergeResult Repository::merge(const std::string& branch_name) {
    MergeResult result;
    result.status = MergeStatus::Error;

    if (!is_initialized() || branch_name.empty()) {
        return result;
    }

    const auto target_ref = branch_ref_path(branch_name);
    if (!std::filesystem::exists(target_ref)) {
        return result;
    }

    const auto current_head = read_head_commit();
    const auto target_head = read_ref_value(target_ref.string());
    if (current_head.empty() || target_head.empty() ||
        current_head == "0000000000000000000000000000000000000000" ||
        target_head == "0000000000000000000000000000000000000000") {
        return result;
    }

    const auto merge_base = find_merge_base(current_head, target_head);
    if (merge_base.empty()) {
        return result;
    }

    const auto base_entries = read_commit_entries(merge_base);
    const auto current_entries = read_commit_entries(current_head);
    const auto target_entries = read_commit_entries(target_head);
    std::set<std::string> all_paths;
    for (const auto& [path, _] : base_entries) {
        all_paths.insert(path);
    }
    for (const auto& [path, _] : current_entries) {
        all_paths.insert(path);
    }
    for (const auto& [path, _] : target_entries) {
        all_paths.insert(path);
    }

    std::map<std::string, std::string> merged_index;
    std::map<std::string, std::string> merged_contents;
    bool had_conflict = false;

    for (const auto& path : all_paths) {
        const auto base_it = base_entries.find(path);
        const auto ours_it = current_entries.find(path);
        const auto theirs_it = target_entries.find(path);

        const bool has_base = base_it != base_entries.end();
        const bool has_ours = ours_it != current_entries.end();
        const bool has_theirs = theirs_it != target_entries.end();
        const std::string base_hash = has_base ? base_it->second : std::string{};
        const std::string ours_hash = has_ours ? ours_it->second : std::string{};
        const std::string theirs_hash = has_theirs ? theirs_it->second : std::string{};

        std::string selected_hash;
        bool path_conflict = false;

        if (has_ours == has_theirs && (!has_ours || ours_hash == theirs_hash)) {
            if (has_ours) {
                selected_hash = ours_hash;
            }
        } else if (has_ours == has_base && (!has_ours || ours_hash == base_hash)) {
            if (has_theirs) {
                selected_hash = theirs_hash;
            }
        } else if (has_theirs == has_base && (!has_theirs || theirs_hash == base_hash)) {
            if (has_ours) {
                selected_hash = ours_hash;
            }
        } else {
            path_conflict = true;
            const auto base_data = has_base ? read_blob(base_hash) : std::string{};
            const auto ours_data = has_ours ? read_blob(ours_hash) : std::string{};
            const auto theirs_data = has_theirs ? read_blob(theirs_hash) : std::string{};

            std::string merged_data;
            if (has_base && ((has_ours && !has_theirs) || (!has_ours && has_theirs))) {
                const std::filesystem::path file_path = root_ / normalize_rel_path(path);
                const auto modified_data = has_ours ? ours_data : theirs_data;
                if (has_ours && !has_theirs) {
                    result.messages.push_back("CONFLICT (modify/delete): " + path + " deleted in " + branch_name +
                                              " and modified in HEAD.  Version HEAD of " + path + " left in tree.");
                } else {
                    result.messages.push_back("CONFLICT (modify/delete): " + path + " deleted in HEAD and modified in " +
                                              branch_name + ".  Version " + branch_name + " of " + path +
                                              " left in tree.");
                }
                had_conflict = true;
                std::ofstream out(file_path, std::ios::binary | std::ios::trunc);
                out.write(modified_data.data(), static_cast<std::streamsize>(modified_data.size()));
                selected_hash = has_ours ? ours_hash : theirs_hash;
                merged_index[path] = selected_hash;
                merged_contents[path] = modified_data;
                continue;
            }

            if (!has_base && has_ours && has_theirs && ours_data != theirs_data) {
                result.messages.push_back("Auto-merging " + path);
                merged_data = merge_text(base_data, ours_data, theirs_data, branch_name);
                if (merged_data.find("<<<<<<< HEAD\n") != std::string::npos) {
                    had_conflict = true;
                    result.messages.push_back("CONFLICT (add/add): Merge conflict in " + path);
                }
                if (!write_object("blob", merged_data, selected_hash)) {
                    return result;
                }
                merged_contents[path] = merged_data;
                merged_index[path] = selected_hash;
                continue;
            }

            if (has_base || has_ours || has_theirs) {
                result.messages.push_back("Auto-merging " + path);
                merged_data = merge_text(base_data, ours_data, theirs_data, branch_name);
                if (merged_data.find("<<<<<<< HEAD\n") != std::string::npos) {
                    had_conflict = true;
                    result.messages.push_back("CONFLICT (content): Merge conflict in " + path);
                }
                if (!write_object("blob", merged_data, selected_hash)) {
                    return result;
                }
                merged_contents[path] = merged_data;
            }
            if (!selected_hash.empty()) {
                merged_index[path] = selected_hash;
            }
        }

        if (!path_conflict && !selected_hash.empty()) {
            merged_index[path] = selected_hash;
            merged_contents[path] = read_blob(selected_hash);
        }
    }

    if (had_conflict) {
        result.status = MergeStatus::Conflict;
        result.messages.push_back("Automatic merge failed; fix conflicts and then commit the result.");
        std::set<std::string> written_paths;
        for (const auto& [path, content] : merged_contents) {
            if (written_paths.find(path) != written_paths.end()) {
                continue;
            }
            const auto file_path = root_ / normalize_rel_path(path);
            const auto parent = file_path.parent_path();
            if (!parent.empty()) {
                std::filesystem::create_directories(parent);
            }
            std::ofstream out(file_path, std::ios::binary | std::ios::trunc);
            if (out) {
                out.write(content.data(), static_cast<std::streamsize>(content.size()));
            }
            written_paths.insert(path);
        }
        if (!write_index_entries(merged_index)) {
            result.status = MergeStatus::Error;
        }
        return result;
    }

    for (const auto& path : all_paths) {
        const auto file_path = root_ / normalize_rel_path(path);
        const auto content_it = merged_contents.find(path);
        if (content_it == merged_contents.end()) {
            std::error_code error;
            std::filesystem::remove(file_path, error);
            continue;
        }
        const auto parent = file_path.parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent);
        }
        std::ofstream out(file_path, std::ios::binary | std::ios::trunc);
        if (!out) {
            result.status = MergeStatus::Error;
            return result;
        }
        out.write(content_it->second.data(), static_cast<std::streamsize>(content_it->second.size()));
    }

    if (!write_index_entries(merged_index)) {
        result.status = MergeStatus::Error;
        return result;
    }

    result.status = MergeStatus::Clean;
    result.messages.clear();
    return result;
}

std::vector<std::string> Repository::log() const {
    std::vector<std::string> history;
    const auto entries = log_entries();
    for (const auto& entry : entries) {
        history.push_back(entry.hash + " " + entry.message);
    }
    return history;
}

std::vector<LogEntry> Repository::log_entries() const {
    std::vector<LogEntry> history;
    std::string current_hash = read_head_commit();
    while (!current_hash.empty() && current_hash != "0000000000000000000000000000000000000000") {
        const auto payload = read_object(current_hash);
        if (payload.empty()) {
            break;
        }
        const auto commit = parse_commit(payload);
        LogEntry entry;
        entry.hash = current_hash;
        entry.author = commit.author;
        entry.message = commit.message;
        if (!commit.timestamp.empty()) {
            const auto seconds = std::stoll(commit.timestamp);
            const auto time = std::chrono::system_clock::from_time_t(static_cast<time_t>(seconds));
            std::time_t now = std::chrono::system_clock::to_time_t(time);
            std::tm tm_value = *std::localtime(&now);
            std::ostringstream out;
            out << std::put_time(&tm_value, "%a %b %-d %H:%M:%S %Y %z");
            entry.date = out.str();
        }
        history.push_back(entry);
        if (commit.parent_hash.empty() || commit.parent_hash == "0000000000000000000000000000000000000000") {
            break;
        }
        current_hash = commit.parent_hash;
    }
    return history;
}

std::vector<BranchEntry> Repository::branch_entries() const {
    std::vector<BranchEntry> branches;
    const auto heads_dir = refs_dir() / "heads";
    if (!std::filesystem::exists(heads_dir)) {
        return branches;
    }
    std::vector<std::string> names;
    for (const auto& entry : std::filesystem::directory_iterator(heads_dir)) {
        if (entry.is_regular_file()) {
            names.push_back(entry.path().filename().string());
        }
    }
    std::sort(names.begin(), names.end());
    for (const auto& name : names) {
        BranchEntry branch;
        branch.name = name;
        branch.current = (current_branch() == name);
        branches.push_back(branch);
    }
    return branches;
}

std::string Repository::read_head_commit() const {
    const auto branch_path = branch_ref_path(current_branch());
    std::ifstream ref(branch_path);
    if (!ref) {
        return "";
    }

    std::string value;
    std::getline(ref, value);
    return value;
}

std::vector<std::string> Repository::list_branches() const {
    std::vector<std::string> branches;
    const auto heads_dir = refs_dir() / "heads";
    if (!std::filesystem::exists(heads_dir)) {
        return branches;
    }

    for (const auto& entry : std::filesystem::directory_iterator(heads_dir)) {
        if (entry.is_regular_file()) {
            branches.push_back(entry.path().filename().string());
        }
    }
    return branches;
}

std::string Repository::write_tree() const {
    std::string entry_payload;
    for (const auto& [path, blob_hash] : read_index_entries()) {
        entry_payload += path + " " + blob_hash + "\n";
    }

    std::string tree_hash;
    write_object("tree", entry_payload, tree_hash);
    return tree_hash;
}

std::string Repository::hash_bytes(std::string_view content) {
    const uint64_t fnv_offset = 14695981039346656037ull;
    uint64_t hash = fnv_offset;

    for (unsigned char ch : content) {
        hash ^= static_cast<uint64_t>(ch);
        hash *= 1099511628211ull;
    }

    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(16) << hash;
    return out.str();
}

std::string Repository::benchmark_hash_pipeline(size_t file_count, size_t file_size_bytes) {
    const auto bench_root = std::filesystem::temp_directory_path() / "twig-benchmark";
    const auto twig_root = bench_root / "twig-bench";
    const auto git_root = bench_root / "git-bench";
    std::filesystem::remove_all(bench_root);
    std::filesystem::create_directories(twig_root);
    std::filesystem::create_directories(git_root);

    Repository twig_repo(twig_root);
    if (!twig_repo.init()) {
        std::filesystem::remove_all(bench_root);
        return "benchmark: files=" + std::to_string(file_count) + ", bytes_per_file=" +
               std::to_string(file_size_bytes) + ", twig_elapsed_ms=unavailable, git_elapsed_ms=unavailable, "
               "throughput_mb_s=unavailable, git_status=twig-init-failed";
    }

    const bool has_git = git_available();
    if (has_git) {
        const auto git_init = "git -C " + shell_quote(git_root) + " init --quiet >/dev/null 2>&1";
        if (std::system(git_init.c_str()) != 0) {
            std::filesystem::remove_all(bench_root);
            return "benchmark: files=" + std::to_string(file_count) + ", bytes_per_file=" +
                   std::to_string(file_size_bytes) + ", twig_elapsed_ms=unavailable, git_elapsed_ms=unavailable, "
                   "throughput_mb_s=unavailable, git_status=git-init-failed";
        }
    }

    for (size_t i = 0; i < file_count; ++i) {
        std::string data(file_size_bytes, 'x');
        if (!data.empty()) {
            auto value = i;
            for (size_t j = 0; j < sizeof(value) && j < data.size(); ++j) {
                data[j] = static_cast<char>(value & 0xff);
                value >>= 8;
            }
        }

        const auto filename = "file-" + std::to_string(i) + ".dat";
        for (const auto& repo_root : {twig_root, git_root}) {
            std::ofstream file(repo_root / filename, std::ios::binary | std::ios::trunc);
            if (!file) {
                std::filesystem::remove_all(bench_root);
                return "benchmark: files=" + std::to_string(file_count) + ", bytes_per_file=" +
                       std::to_string(file_size_bytes) + ", twig_elapsed_ms=unavailable, git_elapsed_ms=unavailable, "
                       "throughput_mb_s=unavailable, git_status=file-setup-failed";
            }
            file.write(data.data(), static_cast<std::streamsize>(data.size()));
            if (!file) {
                std::filesystem::remove_all(bench_root);
                return "benchmark: files=" + std::to_string(file_count) + ", bytes_per_file=" +
                       std::to_string(file_size_bytes) + ", twig_elapsed_ms=unavailable, git_elapsed_ms=unavailable, "
                       "throughput_mb_s=unavailable, git_status=file-setup-failed";
            }
        }
    }

    std::vector<std::filesystem::path> twig_paths;
    twig_paths.reserve(file_count);
    for (size_t i = 0; i < file_count; ++i) {
        twig_paths.emplace_back("file-" + std::to_string(i) + ".dat");
    }
    const auto twig_start = std::chrono::steady_clock::now();
    bool twig_ok = twig_repo.add_files(twig_paths, true);
    if (twig_ok) {
        twig_ok = twig_repo.commit("benchmark snapshot", "Twig Benchmark <benchmark@local>");
    }
    const auto twig_duration = std::chrono::steady_clock::now() - twig_start;
    const auto twig_elapsed_ms = std::chrono::duration<double, std::milli>(twig_duration).count();

    bool git_ok = false;
    std::chrono::duration<double, std::milli> git_duration{0};
    if (has_git) {
        const auto git_command = "git -C " + shell_quote(git_root) + " add . && git -C " + shell_quote(git_root) +
                                 " -c user.name=TwigBenchmark -c user.email=benchmark@local commit -m bench --quiet "
                                 ">/dev/null 2>&1";
        const auto git_start = std::chrono::steady_clock::now();
        git_ok = std::system(git_command.c_str()) == 0;
        git_duration = std::chrono::steady_clock::now() - git_start;
    }

    const auto git_elapsed_ms = git_duration.count();
    const auto total_mb = static_cast<long double>(file_count) * static_cast<long double>(file_size_bytes) /
                          1000000.0L;
    const auto twig_throughput = twig_elapsed_ms > 0.0 ? total_mb / (twig_elapsed_ms / 1000.0) : 0.0L;
    const auto git_throughput = git_ok && git_elapsed_ms > 0.0 ? total_mb / (git_elapsed_ms / 1000.0) : 0.0L;

    std::ostringstream out;
    out << std::fixed << std::setprecision(3) << "benchmark: files=" << file_count
        << ", bytes_per_file=" << file_size_bytes << ", twig_elapsed_ms=";
    if (twig_ok) {
        out << twig_elapsed_ms;
    } else {
        out << "unavailable";
    }
    out << ", git_elapsed_ms=";
    if (git_ok) {
        out << git_elapsed_ms;
    } else {
        out << "unavailable";
    }
    out << ", throughput_mb_s=twig:" << twig_throughput << ",git:";
    if (git_ok) {
        out << git_throughput;
    } else {
        out << "unavailable";
    }
    out << ", git_status=" << (!has_git ? "unavailable" : (git_ok ? "available" : "git-benchmark-failed"));

    std::filesystem::remove_all(bench_root);
    return out.str();
}

bool Repository::is_ignored(const std::filesystem::path& relative_path) const {
    const auto normalized = normalize_rel_path(relative_path);
    const auto path_string = normalized.string();
    if (path_string.empty()) {
        return false;
    }

    for (const auto& pattern : read_ignore_patterns()) {
        if (pattern.empty() || pattern[0] == '#') {
            continue;
        }

        if (pattern.back() == '/' && path_string.rfind(pattern.substr(0, pattern.size() - 1), 0) == 0) {
            return true;
        }

        if (matches_ignore_pattern(path_string, pattern)) {
            return true;
        }
    }

    return false;
}

std::vector<std::string> Repository::read_ignore_patterns() const {
    std::vector<std::string> patterns;
    const auto ignore_path = root_ / ".twigignore";
    if (!std::filesystem::exists(ignore_path)) {
        return patterns;
    }

    std::ifstream input(ignore_path);
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty()) {
            patterns.push_back(line);
        }
    }
    return patterns;
}

bool Repository::matches_ignore_pattern(const std::string& path, const std::string& pattern) const {
    if (pattern.empty()) {
        return false;
    }

    if (pattern.find('*') == std::string::npos) {
        return path == pattern || path.find(pattern) != std::string::npos;
    }

    std::function<bool(size_t, size_t)> match = [&](size_t i, size_t j) -> bool {
        if (j == pattern.size()) {
            return i == path.size();
        }
        if (pattern[j] == '*') {
            return match(i, j + 1) || (i < path.size() && match(i + 1, j));
        }
        return i < path.size() && pattern[j] == path[i] && match(i + 1, j + 1);
    };

    return match(0, 0);
}

std::map<std::string, std::string> Repository::read_index_entries() const {
    std::map<std::string, std::string> entries;
    if (!std::filesystem::exists(index_path())) {
        return entries;
    }

    std::ifstream index_file(index_path());
    std::string line;
    while (std::getline(index_file, line)) {
        if (line.empty()) {
            continue;
        }

        const auto separator = line.find(' ');
        if (separator == std::string::npos) {
            continue;
        }

        const auto path = line.substr(0, separator);
        const auto hash = line.substr(separator + 1);
        entries[path] = hash;
    }
    return entries;
}

bool Repository::write_index_entries(const std::map<std::string, std::string>& entries) const {
    std::ofstream index_file(index_path(), std::ios::trunc);
    if (!index_file) {
        std::cerr << "Unable to open index." << std::endl;
        return false;
    }
    for (const auto& [path, hash] : entries) {
        index_file << path << ' ' << hash << '\n';
    }
    return static_cast<bool>(index_file);
}

std::string Repository::read_blob_from_path(const std::filesystem::path& path) const {
    const auto normalized = normalize_rel_path(path);
    const auto entries = read_index_entries();
    const auto it = entries.find(normalized.string());
    if (it == entries.end()) {
        return "";
    }
    return read_blob(it->second);
}

std::string Repository::branch_ref() const {
    std::ifstream head_file(head_path());
    if (!head_file) {
        return "refs/heads/main";
    }

    std::string value;
    std::getline(head_file, value);
    if (!value.empty()) {
        return value;
    }
    return "refs/heads/main";
}

std::string Repository::read_object(const std::string& object_hash) const {
    const auto path = object_path_for_hash(object_hash);
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return "";
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::string Repository::read_blob(const std::string& object_hash) const {
    const auto raw = read_object(object_hash);
    if (raw.empty()) {
        return "";
    }

    const auto newline_pos = raw.find('\n');
    if (newline_pos == std::string::npos) {
        return raw;
    }
    return raw.substr(newline_pos + 1);
}

std::filesystem::path Repository::object_path_for_hash(const std::string& object_hash) const {
    if (object_hash.size() < 2) {
        return object_dir() / object_hash;
    }
    return object_dir() / object_hash.substr(0, 2) / object_hash.substr(2);
}

bool Repository::write_object(const std::string& type, const std::string& payload, std::string& object_hash) const {
    const std::string object_data = type + " " + std::to_string(payload.size()) + "\n" + payload;
    object_hash = hash_bytes(object_data);

    const auto path = object_path_for_hash(object_hash);
    std::filesystem::create_directories(path.parent_path());

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return false;
    }

    output.write(object_data.data(), static_cast<std::streamsize>(object_data.size()));
    return true;
}

CommitInfo Repository::parse_commit(const std::string& object_payload) const {
    CommitInfo info;
    std::istringstream stream(object_payload);
    std::string line;

    while (std::getline(stream, line)) {
        if (line.empty()) {
            continue;
        }

        const auto equals_pos = line.find('=');
        if (equals_pos == std::string::npos) {
            continue;
        }

        const std::string key = line.substr(0, equals_pos);
        const std::string value = line.substr(equals_pos + 1);

        if (key == "tree") {
            info.tree_hash = value;
        } else if (key == "parent") {
            info.parent_hash = value;
        } else if (key == "author") {
            info.author = value;
        } else if (key == "timestamp") {
            info.timestamp = value;
        } else if (key == "message") {
            info.message = value;
        }
    }

    return info;
}

std::string Repository::read_ref_value(const std::string& ref_path) const {
    std::ifstream ref_file(ref_path);
    if (!ref_file) {
        return "";
    }

    std::string value;
    std::getline(ref_file, value);
    return value;
}

std::string Repository::find_merge_base(const std::string& first_commit, const std::string& second_commit) const {
    constexpr const char* zero_hash = "0000000000000000000000000000000000000000";
    std::map<std::string, size_t> first_ancestors;
    std::string commit_hash = first_commit;
    size_t distance = 0;
    while (!commit_hash.empty() && commit_hash != zero_hash && first_ancestors.emplace(commit_hash, distance).second) {
        const auto commit = parse_commit(read_object(commit_hash));
        commit_hash = commit.parent_hash;
        ++distance;
    }

    std::string best_common;
    size_t best_distance = std::numeric_limits<size_t>::max();
    commit_hash = second_commit;
    distance = 0;
    std::set<std::string> visited;
    while (!commit_hash.empty() && commit_hash != zero_hash && visited.insert(commit_hash).second) {
        const auto common = first_ancestors.find(commit_hash);
        if (common != first_ancestors.end() && common->second + distance < best_distance) {
            best_common = commit_hash;
            best_distance = common->second + distance;
        }
        const auto commit = parse_commit(read_object(commit_hash));
        commit_hash = commit.parent_hash;
        ++distance;
    }
    return best_common;
}

std::map<std::string, std::string> Repository::read_commit_entries(const std::string& commit_hash) const {
    std::map<std::string, std::string> entries;
    const auto payload = read_object(commit_hash);
    std::istringstream stream(payload);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.rfind("entry=", 0) != 0) {
            continue;
        }
        const auto value = line.substr(6);
        const auto colon = value.find(':');
        if (colon != std::string::npos) {
            entries[value.substr(0, colon)] = value.substr(colon + 1);
        }
    }
    return entries;
}

bool Repository::write_index_from_commit(const std::string& commit_hash) {
    const auto payload = read_object(commit_hash);
    if (payload.empty()) {
        return false;
    }

    std::ofstream index_file(index_path(), std::ios::trunc);
    std::istringstream stream(payload);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.rfind("entry=", 0) == 0) {
            const auto value = line.substr(6);
            const auto colon = value.find(':');
            if (colon != std::string::npos) {
                const auto path = value.substr(0, colon);
                const auto blob_hash = value.substr(colon + 1);
                index_file << path << ' ' << blob_hash << '\n';
            }
        }
    }
    return true;
}

std::filesystem::path Repository::object_dir() const {
    return dot_twig_ / "objects";
}

std::filesystem::path Repository::refs_dir() const {
    return dot_twig_ / "refs";
}

std::filesystem::path Repository::branch_ref_path(const std::string& branch_name) const {
    return refs_dir() / "heads" / branch_name;
}

std::filesystem::path Repository::head_path() const {
    return dot_twig_ / "HEAD";
}

std::filesystem::path Repository::index_path() const {
    return dot_twig_ / "index";
}

std::filesystem::path Repository::normalize_rel_path(std::filesystem::path path) const {
    if (!path.is_absolute()) {
        return path.lexically_normal();
    }
    const auto relative = path.lexically_normal().lexically_relative(root_.lexically_normal());
    return relative.lexically_normal();
}

}  // namespace twig