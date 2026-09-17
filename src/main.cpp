#include "twig/repository.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

bool is_tty_stdout() {
    static const bool tty = []() {
        const auto fd = 1;
#ifdef _WIN32
        return false;
#else
        return ::isatty(fd) != 0;
#endif
    }();
    return tty && std::getenv("NO_COLOR") == nullptr;
}

void print_usage() {
    std::cout << "Twig - a lightweight custom VCS\n\n";
    std::cout << "Usage:\n";
    std::cout << "  twig init\n";
    std::cout << "  twig status\n";
    std::cout << "  twig add <file>\n";
    std::cout << "  twig commit -m \"message\"\n";
    std::cout << "  twig branch [name]\n";
    std::cout << "  twig checkout <branch-or-commit>\n";
    std::cout << "  twig diff <file>\n";
    std::cout << "  twig merge <branch>\n";
    std::cout << "  twig benchmark [files=1000] [size=1024]\n";
    std::cout << "  twig log\n";
    std::cout << "  twig help\n";
}

std::string green(const std::string& text) {
    return is_tty_stdout() ? "\033[32m" + text + "\033[0m" : text;
}

std::string red(const std::string& text) {
    return is_tty_stdout() ? "\033[31m" + text + "\033[0m" : text;
}

std::string yellow(const std::string& text) {
    return is_tty_stdout() ? "\033[33m" + text + "\033[0m" : text;
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
        const auto entries = repo.status_entries();
        std::vector<twig::StatusEntry> staged;
        std::vector<twig::StatusEntry> unstaged;
        std::vector<twig::StatusEntry> untracked;
        for (const auto& entry : entries) {
            if (entry.category == "staged" || entry.category == "new file") {
                staged.push_back(entry);
            } else if (entry.category == "modified" || entry.category == "deleted") {
                unstaged.push_back(entry);
            } else if (entry.category == "untracked") {
                untracked.push_back(entry);
            }
        }

        std::cout << "On branch " << repo.current_branch() << std::endl;
        if (entries.empty()) {
            std::cout << "nothing to commit, working tree clean" << std::endl;
            return 0;
        }
        if (!staged.empty()) {
            std::cout << "Changes to be committed:" << std::endl;
            std::cout << "  (use \"twig restore --staged <file>...\" to unstage)" << std::endl;
            for (const auto& entry : staged) {
                const auto label = entry.category == "new file" ? "new file:   " : "modified:   ";
                std::cout << "\t" << green(label + entry.path) << std::endl;
            }
            std::cout << std::endl;
        }
        if (!unstaged.empty()) {
            std::cout << "Changes not staged for commit:" << std::endl;
            std::cout << "  (use \"twig add <file>...\" to update what will be committed)" << std::endl;
            for (const auto& entry : unstaged) {
                const auto label = entry.category == "deleted" ? "deleted:    " : "modified:   ";
                std::cout << "\t" << red(label + entry.path) << std::endl;
            }
            std::cout << std::endl;
        }
        if (!untracked.empty()) {
            std::cout << "Untracked files:" << std::endl;
            std::cout << "  (use \"twig add <file>...\" to include in what will be committed)" << std::endl;
            for (const auto& entry : untracked) {
                std::cout << "\t" << red(entry.path) << std::endl;
            }
            std::cout << std::endl;
        }
        return 0;
    }

    if (command == "add") {
        if (argc < 3) {
            std::cerr << "Usage: twig add <file>" << std::endl;
            return 1;
        }

        std::vector<std::filesystem::path> paths;
        for (int i = 2; i < argc; ++i) {
            paths.emplace_back(argv[i]);
        }
        return repo.add_files(paths) ? 0 : 1;
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

    if (command == "branch") {
        if (!repo.is_initialized()) {
            std::cerr << "Repository is not initialized." << std::endl;
            return 1;
        }

        if (argc == 2) {
            const auto branches = repo.branch_entries();
            for (const auto& branch : branches) {
                if (branch.current) {
                    std::cout << green("* " + branch.name) << std::endl;
                } else {
                    std::cout << "  " << branch.name << std::endl;
                }
            }
            return 0;
        }

        const std::string branch_name = argv[2];
        if (!repo.branch(branch_name)) {
            std::cerr << "Failed to create branch: " << branch_name << std::endl;
            return 1;
        }

        std::cout << "Created branch: " << branch_name << std::endl;
        return 0;
    }

    if (command == "checkout") {
        if (!repo.is_initialized()) {
            std::cerr << "Repository is not initialized." << std::endl;
            return 1;
        }

        if (argc < 3) {
            std::cerr << "Usage: twig checkout <branch-or-commit>" << std::endl;
            return 1;
        }

        if (!repo.checkout(argv[2])) {
            std::cerr << "Failed to checkout: " << argv[2] << std::endl;
            return 1;
        }

        std::cout << "Checked out: " << argv[2] << std::endl;
        return 0;
    }

    if (command == "diff") {
        if (!repo.is_initialized()) {
            std::cerr << "Repository is not initialized." << std::endl;
            return 1;
        }
        if (argc < 3) {
            std::cerr << "Usage: twig diff <file>" << std::endl;
            return 1;
        }

        const auto diff_lines = repo.diff_file(argv[2]);
        if (diff_lines.empty()) {
            return 0;
        }

        for (const auto& line : diff_lines) {
            std::cout << line << std::endl;
        }
        return 0;
    }

    if (command == "merge") {
        if (!repo.is_initialized()) {
            std::cerr << "Repository is not initialized." << std::endl;
            return 1;
        }
        if (argc < 3) {
            std::cerr << "Usage: twig merge <branch>" << std::endl;
            return 1;
        }

        const auto result = repo.merge(argv[2]);
        if (!result.messages.empty()) {
            for (const auto& line : result.messages) {
                std::cout << line << std::endl;
            }
        }
        if (result.status == twig::MergeStatus::Conflict) {
            return 1;
        }
        if (result.status == twig::MergeStatus::Error) {
            std::cerr << "Merge failed." << std::endl;
            return 1;
        }
        std::cout << "Merged branch: " << argv[2] << std::endl;
        return 0;
    }

    if (command == "benchmark") {
        size_t file_count = 1000;
        size_t file_size = 1024;
        if (argc >= 3) {
            file_count = static_cast<size_t>(std::stoul(argv[2]));
        }
        if (argc >= 4) {
            file_size = static_cast<size_t>(std::stoul(argv[3]));
        }
        std::cout << twig::Repository::benchmark_hash_pipeline(file_count, file_size) << std::endl;
        return 0;
    }

    if (command == "log") {
        if (!repo.is_initialized()) {
            std::cerr << "Repository is not initialized." << std::endl;
            return 1;
        }

        const auto entries = repo.log_entries();
        if (entries.empty()) {
            std::cout << "No commits yet." << std::endl;
            return 0;
        }

        for (size_t i = 0; i < entries.size(); ++i) {
            const auto& entry = entries[i];
            std::cout << yellow("commit " + entry.hash);
            if (i == 0) {
                std::cout << " (HEAD -> " << repo.current_branch() << ")";
            }
            std::cout << std::endl;
            std::cout << "Author: " << entry.author << std::endl;
            std::cout << "Date:   " << entry.date << std::endl;
            std::cout << std::endl;
            std::cout << "    " << entry.message << std::endl;
            if (i + 1 < entries.size()) {
                std::cout << std::endl;
            }
        }
        return 0;
    }

    std::cerr << "Unknown command: " << command << std::endl;
    print_usage();
    return 1;
}
