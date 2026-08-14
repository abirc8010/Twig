#pragma once

#include <filesystem>
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
    void add_file(const std::filesystem::path& relative_path);
    std::string blob_id_for_file(const std::filesystem::path& relative_path) const;

    bool commit(const std::string& message, const std::string& author = "Twig User <twig@local>");
    std::vector<std::string> log() const;
    std::string read_head_commit() const;
    std::string write_tree() const;

    static std::string hash_bytes(std::string_view content);

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
    std::filesystem::path object_path_for_hash(const std::string& object_hash) const;
    bool write_object(const std::string& type, const std::string& payload, std::string& object_hash) const;
    CommitInfo parse_commit(const std::string& object_payload) const;
};

}  // namespace twig
