#include "twig/repository.hpp"

#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>

namespace twig {

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

std::vector<std::string> Repository::status() const {
    std::vector<std::string> lines;
    lines.push_back("Repository root: " + root_.string());

    if (!is_initialized()) {
        lines.push_back("Repository is not initialized. Run: twig init");
        return lines;
    }

    lines.push_back("Current branch: " + current_branch());

    std::map<std::string, std::string> index_entries;
    if (std::filesystem::exists(index_path())) {
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
            index_entries[path] = hash;
        }
    }

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
        if (relative.empty() || relative.string() == ".twig") {
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

void Repository::add_file(const std::filesystem::path& relative_path) {
    if (!is_initialized()) {
        std::cerr << "Repository is not initialized" << std::endl;
        return;
    }

    const auto normalized = normalize_rel_path(relative_path);
    const auto abs_path = root_ / normalized;
    if (!std::filesystem::exists(abs_path) || std::filesystem::is_directory(abs_path)) {
        std::cerr << "File not found: " << normalized << std::endl;
        return;
    }

    const auto blob_id = blob_id_for_file(normalized);

    std::ofstream index_file(index_path(), std::ios::app);
    if (!index_file) {
        std::cerr << "Unable to open index." << std::endl;
        return;
    }

    index_file << normalized.string() << ' ' << blob_id << '\n';
    std::cout << "staged: " << normalized << " -> " << blob_id << std::endl;
}

std::string Repository::blob_id_for_file(const std::filesystem::path& relative_path) const {
    const auto abs_path = root_ / normalize_rel_path(relative_path);
    std::ifstream file(abs_path, std::ios::binary);
    if (!file) {
        return "missing";
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    return hash_bytes(buffer.str());
}

bool Repository::commit(const std::string& message, const std::string& author) {
    if (!is_initialized()) {
        return false;
    }

    const auto tree_hash = write_tree();
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

std::vector<std::string> Repository::log() const {
    std::vector<std::string> history;
    std::string current_hash = read_head_commit();

    while (!current_hash.empty() && current_hash != "0000000000000000000000000000000000000000") {
        const auto payload = read_object(current_hash);
        if (payload.empty()) {
            break;
        }

        const auto commit = parse_commit(payload);
        history.push_back(current_hash + " " + commit.message);
        if (commit.parent_hash.empty() || commit.parent_hash == "0000000000000000000000000000000000000000") {
            break;
        }
        current_hash = commit.parent_hash;
    }

    return history;
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

std::string Repository::write_tree() const {
    std::ifstream index_file(index_path());
    std::string entry_payload;
    std::string line;

    while (std::getline(index_file, line)) {
        if (!line.empty()) {
            entry_payload += line + "\n";
        }
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
        path = root_ / path;
    }

    const auto relative = std::filesystem::relative(path, root_);
    return relative.lexically_normal();
}

}  // namespace twig
