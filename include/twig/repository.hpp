#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace twig {

struct CommitInfo {
    std::string tree_hash;
    std::string parent_hash;
    std::string author;
    std::string timestamp;
    std::string message;
};

class Repository {
public:
    explicit Repository(std::filesystem::path root = std::filesystem::current_path());

    bool init();
    bool is_initialized() const;
    std::filesystem::path root() const;
    std::string current_branch() const;

    std::vector<std::string> status() const;
    bool add_file(const std::filesystem::path& relative_path, bool quiet = false);
    bool add_files(const std::vector<std::filesystem::path>& relative_paths, bool quiet = false);
    std::string blob_id_for_file(const std::filesystem::path& relative_path) const;
    bool is_ignored(const std::filesystem::path& relative_path) const;
    std::vector<std::string> diff_file(const std::filesystem::path& relative_path) const;

    bool commit(const std::string& message, const std::string& author = "Twig User <twig@local>");
    bool branch(const std::string& name) const;
    bool checkout(const std::string& target);
    bool merge(const std::string& branch_name);
    std::vector<std::string> log() const;
    std::string read_head_commit() const;
    std::string write_tree() const;
    std::vector<std::string> list_branches() const;

    static std::string hash_bytes(std::string_view content);
    static std::string benchmark_hash_pipeline(size_t file_count, size_t file_size_bytes);

private:
    std::filesystem::path root_;
    std::filesystem::path dot_twig_;

    std::filesystem::path object_dir() const;
    std::filesystem::path refs_dir() const;
    std::filesystem::path branch_ref_path(const std::string& branch_name) const;
    std::filesystem::path head_path() const;
    std::filesystem::path index_path() const;
    std::filesystem::path normalize_rel_path(std::filesystem::path path) const;

    std::string branch_ref() const;
    std::string read_object(const std::string& object_hash) const;
    std::string read_blob(const std::string& object_hash) const;
    std::filesystem::path object_path_for_hash(const std::string& object_hash) const;
    bool write_object(const std::string& type, const std::string& payload, std::string& object_hash) const;
    CommitInfo parse_commit(const std::string& object_payload) const;
    std::string read_ref_value(const std::string& ref_path) const;
    std::string find_merge_base(const std::string& first_commit, const std::string& second_commit) const;
    std::map<std::string, std::string> read_commit_entries(const std::string& commit_hash) const;
    bool write_index_from_commit(const std::string& commit_hash);
    std::vector<std::string> read_ignore_patterns() const;
    bool matches_ignore_pattern(const std::string& path, const std::string& pattern) const;
    std::map<std::string, std::string> read_index_entries() const;
    bool write_index_entries(const std::map<std::string, std::string>& entries) const;
    std::string read_file_contents(const std::filesystem::path& path) const;
    std::vector<std::string> compute_line_diff(const std::string& old_text, const std::string& new_text) const;
    std::string read_blob_from_path(const std::filesystem::path& path) const;
    std::string merge_text(const std::string& base_text, const std::string& ours_text, const std::string& theirs_text) const;
};

}  // namespace twig
