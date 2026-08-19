#include "twig/repository.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

int main() {
    const auto root = std::filesystem::temp_directory_path() / "twig-test-commit";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    twig::Repository repo(root);
    if (!repo.init()) {
        std::cerr << "init failed" << std::endl;
        return 1;
    }

    std::ofstream staged_file(root / "foo.txt");
    staged_file << "foo contents\n";
    staged_file.close();
    if (!repo.add_file("foo.txt")) {
        std::cerr << "initial foo staging failed" << std::endl;
        return 2;
    }

    bool staged_foo = false;
    bool modified_foo = false;
    for (const auto& line : repo.status()) {
        staged_foo = staged_foo || line.find("staged: foo.txt") != std::string::npos;
        modified_foo = modified_foo || line.find("modified: foo.txt") != std::string::npos;
    }
    if (!staged_foo || modified_foo) {
        std::cerr << "status did not report freshly staged foo.txt correctly" << std::endl;
        return 3;
    }

    if (!repo.add_file("foo.txt")) {
        std::cerr << "restaging foo.txt failed" << std::endl;
        return 4;
    }
    std::ifstream staged_index(root / ".twig" / "index");
    std::string index_line;
    size_t foo_index_entries = 0;
    while (std::getline(staged_index, index_line)) {
        if (index_line.rfind("foo.txt ", 0) == 0) {
            ++foo_index_entries;
        }
    }
    if (foo_index_entries != 1) {
        std::cerr << "restaging created duplicate foo.txt index entries" << std::endl;
        return 5;
    }

    std::ofstream file(root / "hello.txt");
    file << "hello twig\n";
    file.close();

    repo.add_file("hello.txt");

    if (!repo.commit("initial commit", "Test User <test@example.com>")) {
        std::cerr << "commit failed" << std::endl;
        return 6;
    }

    std::ofstream file2(root / "hello.txt", std::ios::trunc);
    file2 << "hello twig changed\n";
    file2.close();

    const auto status_lines = repo.status();
    bool modified_found = false;
    for (const auto& line : status_lines) {
        if (line.find("modified: hello.txt") != std::string::npos) {
            modified_found = true;
        }
    }

    if (!modified_found) {
        std::cerr << "status did not report modified file" << std::endl;
        return 7;
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
        return 8;
    }

    const auto log = repo.log();
    if (log.empty()) {
        std::cerr << "log empty" << std::endl;
        return 9;
    }

    if (!repo.branch("feature")) {
        std::cerr << "branch creation failed" << std::endl;
        return 10;
    }

    if (!repo.checkout("feature")) {
        std::cerr << "checkout failed" << std::endl;
        return 11;
    }

    if (repo.current_branch() != "feature") {
        std::cerr << "branch checkout did not switch branch" << std::endl;
        return 12;
    }

    std::ifstream restored(root / "hello.txt");
    std::string restored_contents((std::istreambuf_iterator<char>(restored)), {});
    if (restored_contents != "hello twig\n") {
        std::cerr << "checkout did not restore file contents" << std::endl;
        return 13;
    }

    std::ofstream ignore_file(root / ".twigignore");
    ignore_file << "build/\n*.o\n";
    ignore_file.close();

    if (!repo.is_ignored("build/output.o")) {
        std::cerr << "ignore matcher failed for build artifact" << std::endl;
        return 14;
    }

    const auto diff = repo.diff_file("hello.txt");
    if (diff.empty()) {
        std::cerr << "diff output empty" << std::endl;
        return 15;
    }

    if (!repo.merge("main")) {
        std::cerr << "merge operation failed" << std::endl;
        return 16;
    }

    std::ostringstream benchmark_console;
    auto* const original_cout = std::cout.rdbuf(benchmark_console.rdbuf());
    const auto benchmark_output = twig::Repository::benchmark_hash_pipeline(8, 32);
    std::cout.rdbuf(original_cout);

    const bool benchmark_fields_present =
        benchmark_output.find("files=8") != std::string::npos &&
        benchmark_output.find("bytes_per_file=32") != std::string::npos &&
        benchmark_output.find("twig_elapsed_ms=") != std::string::npos &&
        benchmark_output.find("git_elapsed_ms=") != std::string::npos &&
        benchmark_output.find("throughput_mb_s=") != std::string::npos &&
        benchmark_output.find("git_status=") != std::string::npos;
    if (!benchmark_fields_present) {
        std::cerr << "benchmark output missing required comparison fields: " << benchmark_output << std::endl;
        return 17;
    }
    if (!benchmark_console.str().empty()) {
        std::cerr << "benchmark emitted noisy staging output" << std::endl;
        return 18;
    }

    std::cout << "commit ok" << std::endl;
    std::filesystem::remove_all(root);
    return 0;
}
