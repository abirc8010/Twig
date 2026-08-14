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

    const auto log = repo.log();
    if (log.empty()) {
        std::cerr << "log empty" << std::endl;
        return 3;
    }

    std::cout << "commit ok" << std::endl;
    std::filesystem::remove_all(root);
    return 0;
}
