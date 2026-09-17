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

    {
        std::ofstream changed_file(root / "hello.txt", std::ios::trunc);
        changed_file << "hello twig changed\n";
    }
    const auto diff = repo.diff_file("hello.txt");
    if (diff.empty()) {
        std::cerr << "diff output empty" << std::endl;
        return 15;
    }
    const auto diff_text = [&]() {
        std::ostringstream out;
        for (const auto& line : diff) {
            out << line << '\n';
        }
        return out.str();
    }();
    if (diff_text.find("diff --git a/hello.txt b/hello.txt") == std::string::npos ||
        diff_text.find("@@ -1 +1 @@") == std::string::npos ||
        diff_text.find("-hello twig") == std::string::npos ||
        diff_text.find("+hello twig changed") == std::string::npos) {
        std::cerr << "diff output did not resemble Git's hunk format" << std::endl;
        return 15;
    }
    {
        std::ofstream restored_file(root / "hello.txt", std::ios::trunc);
        restored_file << "hello twig\n";
    }

    {
        const auto conflict_root = std::filesystem::temp_directory_path() / "twig-test-merge-conflict";
        std::filesystem::remove_all(conflict_root);
        std::filesystem::create_directories(conflict_root);
        twig::Repository conflict_repo(conflict_root);
        if (!conflict_repo.init()) {
            std::cerr << "conflict repo init failed" << std::endl;
            return 29;
        }

        {
            std::ofstream file(conflict_root / "conflict.txt");
            file << "base\n";
        }
        if (!conflict_repo.add_file("conflict.txt") || !conflict_repo.commit("base conflict") ||
            !conflict_repo.branch("side")) {
            std::cerr << "conflict repo base setup failed" << std::endl;
            return 30;
        }

        {
            std::ofstream file(conflict_root / "conflict.txt", std::ios::trunc);
            file << "main\n";
        }
        if (!conflict_repo.add_file("conflict.txt") || !conflict_repo.commit("main change") ||
            !conflict_repo.checkout("side")) {
            std::cerr << "conflict repo main setup failed" << std::endl;
            return 31;
        }

        {
            std::ofstream file(conflict_root / "conflict.txt", std::ios::trunc);
            file << "side\n";
        }
        if (!conflict_repo.add_file("conflict.txt") || !conflict_repo.commit("side change") ||
            !conflict_repo.checkout("main")) {
            std::cerr << "conflict repo side setup failed" << std::endl;
            return 32;
        }

        const auto merge_result = conflict_repo.merge("side");
        if (merge_result.status != twig::MergeStatus::Conflict) {
            std::cerr << "merge conflict did not produce a conflicted state" << std::endl;
            return 33;
        }

        std::ifstream conflict_file(conflict_root / "conflict.txt");
        std::string merged_contents((std::istreambuf_iterator<char>(conflict_file)), {});
        if (merged_contents.find("<<<<<<< HEAD") == std::string::npos ||
            merged_contents.find("=======") == std::string::npos ||
            merged_contents.find(">>>>>>> side") == std::string::npos) {
            std::cerr << "merge conflict markers did not match the Git-style format" << std::endl;
            return 34;
        }
        std::filesystem::remove_all(conflict_root);
    }

    {
        const auto newline_root = std::filesystem::temp_directory_path() / "twig-test-no-newline-diff";
        std::filesystem::remove_all(newline_root);
        std::filesystem::create_directories(newline_root);
        twig::Repository newline_repo(newline_root);
        if (!newline_repo.init()) {
            std::cerr << "newline diff repo init failed" << std::endl;
            return 53;
        }
        {
            std::ofstream file(newline_root / "one.txt", std::ios::binary);
            file << "old";
        }
        if (!newline_repo.add_file("one.txt") || !newline_repo.commit("old")) {
            std::cerr << "newline diff base setup failed" << std::endl;
            return 54;
        }
        {
            std::ofstream file(newline_root / "one.txt", std::ios::binary | std::ios::trunc);
            file << "new";
        }
        const auto no_newline_diff = newline_repo.diff_file("one.txt");
        bool no_newline_marker = false;
        for (const auto& line : no_newline_diff) {
            no_newline_marker = no_newline_marker || line == "\\ No newline at end of file";
        }
        if (!no_newline_marker) {
            std::cerr << "diff omitted the missing-final-newline marker" << std::endl;
            return 55;
        }
        std::filesystem::remove(newline_root / "one.txt");
        const auto deleted_diff = newline_repo.diff_file("one.txt");
        bool deleted_hunk = false;
        for (const auto& line : deleted_diff) {
            deleted_hunk = deleted_hunk || line == "@@ -1 +0,0 @@";
        }
        if (!deleted_hunk) {
            std::cerr << "deleted-file diff did not use the Git range format" << std::endl;
            return 56;
        }
        std::filesystem::remove_all(newline_root);
    }

    {
        const auto clean_root = std::filesystem::temp_directory_path() / "twig-test-disjoint-merge";
        std::filesystem::remove_all(clean_root);
        std::filesystem::create_directories(clean_root);
        twig::Repository clean_repo(clean_root);
        if (!clean_repo.init()) {
            std::cerr << "disjoint merge repo init failed" << std::endl;
            return 35;
        }
        {
            std::ofstream file(clean_root / "lines.txt");
            file << "one\ntwo\nthree\nfour\n";
        }
        if (!clean_repo.add_file("lines.txt") || !clean_repo.commit("base") || !clean_repo.branch("side")) {
            std::cerr << "disjoint merge base setup failed" << std::endl;
            return 36;
        }
        {
            std::ofstream file(clean_root / "lines.txt", std::ios::trunc);
            file << "ONE\ntwo\nthree\nfour\n";
        }
        if (!clean_repo.add_file("lines.txt") || !clean_repo.commit("main line") || !clean_repo.checkout("side")) {
            std::cerr << "disjoint merge main setup failed" << std::endl;
            return 37;
        }
        {
            std::ofstream file(clean_root / "lines.txt", std::ios::trunc);
            file << "one\ntwo\nthree\nFOUR\n";
        }
        if (!clean_repo.add_file("lines.txt") || !clean_repo.commit("side line") || !clean_repo.checkout("main")) {
            std::cerr << "disjoint merge side setup failed" << std::endl;
            return 38;
        }
        const auto merged = clean_repo.merge("side");
        std::ifstream file(clean_root / "lines.txt");
        const std::string contents((std::istreambuf_iterator<char>(file)), {});
        if (merged.status != twig::MergeStatus::Clean || contents != "ONE\ntwo\nthree\nFOUR\n") {
            std::cerr << "non-overlapping line edits did not merge cleanly; status="
                      << static_cast<int>(merged.status) << ", contents=" << contents << std::endl;
            for (const auto& message : merged.messages) std::cerr << message << std::endl;
            return 39;
        }
        std::filesystem::remove_all(clean_root);
    }

    {
        const auto context_root = std::filesystem::temp_directory_path() / "twig-test-merge-context";
        std::filesystem::remove_all(context_root);
        std::filesystem::create_directories(context_root);
        twig::Repository context_repo(context_root);
        if (!context_repo.init()) {
            std::cerr << "context merge repo init failed" << std::endl;
            return 40;
        }
        {
            std::ofstream file(context_root / "lines.txt");
            file << "first\nsecond\nthird\nfourth\nfifth\n";
        }
        if (!context_repo.add_file("lines.txt") || !context_repo.commit("base") || !context_repo.branch("side")) {
            std::cerr << "context merge base setup failed" << std::endl;
            return 41;
        }
        {
            std::ofstream file(context_root / "lines.txt", std::ios::trunc);
            file << "first\nsecond\nOURS\nfourth\nfifth\n";
        }
        if (!context_repo.add_file("lines.txt") || !context_repo.commit("main conflict") || !context_repo.checkout("side")) {
            std::cerr << "context merge main setup failed" << std::endl;
            return 42;
        }
        {
            std::ofstream file(context_root / "lines.txt", std::ios::trunc);
            file << "first\nsecond\nTHEIRS\nfourth\nfifth\n";
        }
        if (!context_repo.add_file("lines.txt") || !context_repo.commit("side conflict") || !context_repo.checkout("main")) {
            std::cerr << "context merge side setup failed" << std::endl;
            return 43;
        }
        const auto merged = context_repo.merge("side");
        std::ifstream file(context_root / "lines.txt");
        const std::string contents((std::istreambuf_iterator<char>(file)), {});
        if (merged.status != twig::MergeStatus::Conflict ||
            contents.find("second\n<<<<<<< HEAD\nOURS\n=======\nTHEIRS\n>>>>>>> side\nfourth") == std::string::npos) {
            std::cerr << "conflict block did not preserve surrounding lines outside markers" << std::endl;
            return 44;
        }
        std::filesystem::remove_all(context_root);
    }

    {
        const auto delete_root = std::filesystem::temp_directory_path() / "twig-test-modify-delete";
        std::filesystem::remove_all(delete_root);
        std::filesystem::create_directories(delete_root);
        twig::Repository delete_repo(delete_root);
        if (!delete_repo.init()) {
            std::cerr << "modify/delete repo init failed" << std::endl;
            return 45;
        }
        {
            std::ofstream file(delete_root / "file.txt");
            file << "base\n";
        }
        if (!delete_repo.add_file("file.txt") || !delete_repo.commit("base") || !delete_repo.branch("side")) {
            std::cerr << "modify/delete base setup failed" << std::endl;
            return 46;
        }
        {
            std::ofstream file(delete_root / "file.txt", std::ios::trunc);
            file << "changed\n";
        }
        if (!delete_repo.add_file("file.txt") || !delete_repo.commit("modify") || !delete_repo.checkout("side")) {
            std::cerr << "modify/delete modify setup failed" << std::endl;
            return 47;
        }
        std::filesystem::remove(delete_root / "file.txt");
        {
            std::ofstream index(delete_root / ".twig" / "index", std::ios::trunc);
        }
        if (!delete_repo.commit("delete") || !delete_repo.checkout("main")) {
            std::cerr << "modify/delete delete setup failed" << std::endl;
            return 48;
        }
        const auto merged = delete_repo.merge("side");
        if (merged.status != twig::MergeStatus::Conflict || !std::filesystem::exists(delete_root / "file.txt")) {
            std::cerr << "modify/delete conflict was not retained in the working tree" << std::endl;
            return 49;
        }
        std::filesystem::remove_all(delete_root);
    }

    {
        const auto diff_root = std::filesystem::temp_directory_path() / "twig-test-multihunk-diff";
        std::filesystem::remove_all(diff_root);
        std::filesystem::create_directories(diff_root);
        twig::Repository diff_repo(diff_root);
        if (!diff_repo.init()) {
            std::cerr << "multi-hunk diff repo init failed" << std::endl;
            return 50;
        }
        {
            std::ofstream file(diff_root / "lines.txt");
            for (int line = 1; line <= 20; ++line) file << "line " << line << '\n';
        }
        if (!diff_repo.add_file("lines.txt") || !diff_repo.commit("base")) {
            std::cerr << "multi-hunk diff base setup failed" << std::endl;
            return 51;
        }
        {
            std::ofstream file(diff_root / "lines.txt", std::ios::trunc);
            for (int line = 1; line <= 20; ++line) file << ((line == 2 || line == 18) ? "changed " : "line ") << line << '\n';
        }
        const auto hunks = diff_repo.diff_file("lines.txt");
        size_t hunk_count = 0;
        bool context_found = false;
        for (const auto& line : hunks) {
            if (line.rfind("@@ ", 0) == 0) ++hunk_count;
            if (line == " line 1" || line == " line 3") context_found = true;
        }
        if (hunk_count != 2 || !context_found) {
            std::cerr << "multi-hunk diff did not include separate hunks and context" << std::endl;
            return 52;
        }
        std::filesystem::remove_all(diff_root);
    }

    if (!repo.merge("main")) {
        std::cerr << "merge operation failed" << std::endl;
        return 16;
    }

    const auto directory_root = std::filesystem::temp_directory_path() / "twig-test-directory-add";
    std::filesystem::remove_all(directory_root);
    std::filesystem::create_directories(directory_root / "src" / "nested");
    twig::Repository directory_repo(directory_root);
    if (!directory_repo.init()) {
        std::cerr << "directory test init failed" << std::endl;
        return 17;
    }
    {
        std::ofstream nested_file(directory_root / "src" / "nested" / "unit.cpp");
        nested_file << "int value = 1;\n";
        std::ofstream internal_file(directory_root / ".twig" / "must-not-stage");
        internal_file << "internal\n";
    }
    if (!directory_repo.add_file(".")) {
        std::cerr << "directory add failed" << std::endl;
        return 18;
    }
    std::ifstream directory_index(directory_root / ".twig" / "index");
    bool nested_staged = false;
    bool internal_staged = false;
    while (std::getline(directory_index, index_line)) {
        nested_staged = nested_staged || index_line.rfind("src/nested/unit.cpp ", 0) == 0;
        internal_staged = internal_staged || index_line.find("must-not-stage") != std::string::npos;
    }
    if (!nested_staged || internal_staged) {
        std::cerr << "directory add did not stage nested files correctly or included .twig contents" << std::endl;
        return 19;
    }

    const auto merge_root = std::filesystem::temp_directory_path() / "twig-test-lca-merge";
    std::filesystem::remove_all(merge_root);
    std::filesystem::create_directories(merge_root);
    twig::Repository merge_repo(merge_root);
    if (!merge_repo.init()) {
        std::cerr << "merge test init failed" << std::endl;
        return 20;
    }
    {
        std::ofstream a_file(merge_root / "A.txt");
        a_file << "A base\n";
        std::ofstream b_file(merge_root / "B.txt");
        b_file << "B base\n";
    }
    if (!merge_repo.add_files({"A.txt", "B.txt"}) || !merge_repo.commit("merge base") ||
        !merge_repo.branch("side")) {
        std::cerr << "merge base setup failed" << std::endl;
        return 21;
    }
    {
        std::ofstream a_file(merge_root / "A.txt", std::ios::trunc);
        a_file << "A changed on main\n";
    }
    if (!merge_repo.add_file("A.txt") || !merge_repo.commit("change A on main") || !merge_repo.checkout("side")) {
        std::cerr << "main-side A branch setup failed" << std::endl;
        return 22;
    }
    {
        std::ofstream b_file(merge_root / "B.txt", std::ios::trunc);
        b_file << "B changed on side\n";
    }
    if (!merge_repo.add_file("B.txt") || !merge_repo.commit("change B on side") || !merge_repo.checkout("main")) {
        std::cerr << "side branch B setup failed" << std::endl;
        return 23;
    }
    if (!merge_repo.merge("side")) {
        std::cerr << "LCA three-way merge failed" << std::endl;
        return 24;
    }
    const auto read_text = [](const std::filesystem::path& path) {
        std::ifstream input(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    };
    if (read_text(merge_root / "A.txt") != "A changed on main\n" ||
        read_text(merge_root / "B.txt") != "B changed on side\n") {
        std::cerr << "three-way merge failed to preserve non-conflicting branch changes" << std::endl;
        return 25;
    }
    if (merge_repo.blob_id_for_file("A.txt") == "missing" || merge_repo.blob_id_for_file("B.txt") == "missing") {
        std::cerr << "merge result files could not be hashed" << std::endl;
        return 26;
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
        return 27;
    }
    if (!benchmark_console.str().empty()) {
        std::cerr << "benchmark emitted noisy staging output" << std::endl;
        return 28;
    }

    std::cout << "commit ok" << std::endl;
    std::filesystem::remove_all(root);
    std::filesystem::remove_all(directory_root);
    std::filesystem::remove_all(merge_root);
    return 0;
}
