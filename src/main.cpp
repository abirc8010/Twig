#include "twig/repository.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

void print_usage() {
    std::cout << "Twig - a lightweight custom VCS\n\n";
    std::cout << "Usage:\n";
    std::cout << "  twig init\n";
    std::cout << "  twig status\n";
    std::cout << "  twig add <file>\n";
    std::cout << "  twig commit -m \"message\"\n";
    std::cout << "  twig log\n";
    std::cout << "  twig help\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage();
        return 0;
    }

    const std::string command = argv[1];

    twig::Repository repo(std::filesystem::current_path());

    if (command == "help" || command == "-h" || command == "--help") {
        print_usage();
        return 0;
    }

    if (command == "init") {
        if (repo.init()) {
            std::cout << "Initialized empty Twig repository in " << repo.root() << std::endl;
            return 0;
        }

        std::cerr << "Failed to initialize repository" << std::endl;
        return 1;
    }

    if (command == "status") {
        const auto lines = repo.status();
        for (const auto& line : lines) {
            std::cout << line << std::endl;
        }
        return 0;
    }

    if (command == "add") {
        if (argc < 3) {
            std::cerr << "Usage: twig add <file>" << std::endl;
            return 1;
        }

        for (int i = 2; i < argc; ++i) {
            repo.add_file(argv[i]);
        }
        return 0;
    }

    if (command == "commit") {
        if (!repo.is_initialized()) {
            std::cerr << "Repository is not initialized." << std::endl;
            return 1;
        }

        std::string message = "snapshot";
        std::string author = "Twig User <twig@local>";

        for (int i = 2; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "-m" || arg == "--message") {
                if (i + 1 >= argc) {
                    std::cerr << "Missing commit message." << std::endl;
                    return 1;
                }
                message = argv[++i];
            } else if (arg == "--author") {
                if (i + 1 >= argc) {
                    std::cerr << "Missing author value." << std::endl;
                    return 1;
                }
                author = argv[++i];
            } else {
                message = arg;
            }
        }

        if (!repo.commit(message, author)) {
            std::cerr << "Failed to create commit." << std::endl;
            return 1;
        }

        std::cout << "Created commit " << repo.read_head_commit() << " on branch " << repo.current_branch()
                  << std::endl;
        return 0;
    }

    if (command == "log") {
        if (!repo.is_initialized()) {
            std::cerr << "Repository is not initialized." << std::endl;
            return 1;
        }

        const auto entries = repo.log();
        if (entries.empty()) {
            std::cout << "No commits yet." << std::endl;
            return 0;
        }

        for (const auto& entry : entries) {
            std::cout << entry << std::endl;
        }
        return 0;
    }

    std::cerr << "Unknown command: " << command << std::endl;
    print_usage();
    return 1;
}
