#include "twig/repository.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>

int main() {
    const auto root = std::filesystem::temp_directory_path() / "twig-test-commit";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    twig::Repository repo(root);
    if (!repo.init()) {
        std::cerr << "init failed" << std::endl;
        return 1;
    }

    std::ofstream file(root / "hello.txt");
    file << "hello twig\n";
    file.close();

    repo.add_file("hello.txt");

    if (!repo.commit("initial commit", "Test User <test@example.com>")) {
        std::cerr << "commit failed" << std::endl;
        return 2;
    }

    std::ofstream file2(root / "hello.txt", std::ios::trunc);
    file2 << "hello twig changed\n";
    file2.close();

    const auto status_lines = repo.status();
    bool modified_found = false;
    bool staged_found = false;
    bool untracked_found = false;
    for (const auto& line : status_lines) {
        if (line.find("modified: hello.txt") != std::string::npos) {
            modified_found = true;
        }
        if (line.find("staged: hello.txt") != std::string::npos) {
            staged_found = true;
        }
        if (line.find("untracked: new.txt") != std::string::npos) {
            untracked_found = true;
        }
    }

    if (!modified_found) {
        std::cerr << "status did not report modified file" << std::endl;
        return 3;
    }

    std::ofstream new_file(root / "new.txt");
    new_file << "new file\n";
    new_file.close();

    const auto status_after_untracked = repo.status();
    bool untracked_after = false;
    for (const auto& line : status_after_untracked) {
        if (line.find("untracked: new.txt") != std::string::npos) {
            untracked_after = true;
            break;
        }
    }

    if (!untracked_after) {
        std::cerr << "status did not report untracked file" << std::endl;
        return 4;
    }

    const auto log = repo.log();
    if (log.empty()) {
        std::cerr << "log empty" << std::endl;
        return 5;
    }

    std::cout << "commit ok" << std::endl;
    std::filesystem::remove_all(root);
    return 0;
}
