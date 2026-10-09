#include "twig/repository.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace twig {

std::vector<std::string> Repository::diff_file(const std::filesystem::path& relative_path) const {
    const auto normalized = normalize_rel_path(relative_path);
    const auto abs_path = root_ / normalized;
    const auto entries = read_index_entries();
    const auto it = entries.find(normalized.string());
    if (it == entries.end()) {
        return {};
    }

    const std::string old_hash = it->second;
    const std::string old_text = read_blob(old_hash);
    const auto path_string = normalized.generic_string();

    if (!std::filesystem::exists(abs_path)) {
        std::vector<std::string> out;
        const auto hunks = compute_line_diff(old_text, "");
        out.push_back("diff --git a/" + path_string + " b/" + path_string);
        out.push_back("deleted file mode 100644");
        out.push_back("index " + old_hash.substr(0, 7) + "..0000000");
        out.push_back("--- a/" + path_string);
        out.push_back("+++ /dev/null");
        out.insert(out.end(), hunks.begin(), hunks.end());
        return out;
    }

    const std::string new_text = read_file_contents(abs_path);
    if (old_text == new_text) {
        return {};
    }

    const std::vector<std::string> hunks = compute_line_diff(old_text, new_text);
    if (hunks.empty()) {
        return {};
    }

    std::vector<std::string> out;
    out.push_back("diff --git a/" + path_string + " b/" + path_string);
    out.push_back("index " + old_hash.substr(0, 7) + ".." + blob_id_for_file(normalized).substr(0, 7) + " 100644");
    out.push_back("--- a/" + path_string);
    out.push_back("+++ b/" + path_string);
    out.insert(out.end(), hunks.begin(), hunks.end());
    return out;
}

std::string Repository::read_file_contents(const std::filesystem::path& path) const {
    std::error_code error;
    const auto file_size = std::filesystem::file_size(path, error);
    if (error || file_size > static_cast<uintmax_t>(std::numeric_limits<std::streamsize>::max()) ||
        file_size > static_cast<uintmax_t>(std::numeric_limits<size_t>::max())) {
        return "";
    }

    std::string content(static_cast<size_t>(file_size), '\0');
    std::ifstream input(path, std::ios::binary);
    if (!input || (!content.empty() && !input.read(content.data(), static_cast<std::streamsize>(content.size())))) {
        return "";
    }
    return content;
}

std::vector<std::string> Repository::compute_line_diff(const std::string& old_text, const std::string& new_text) const {
    auto split_lines = [](const std::string& text) {
        std::vector<std::string> lines;
        if (text.empty()) {
            return lines;
        }
        size_t start = 0;
        while (start < text.size()) {
            const auto end = text.find('\n', start);
            if (end == std::string::npos) {
                lines.push_back(text.substr(start));
                break;
            }
            lines.push_back(text.substr(start, end - start));
            start = end + 1;
        }
        return lines;
    };

    const auto old_lines = split_lines(old_text);
    const auto new_lines = split_lines(new_text);
    if (old_text == new_text) {
        return {};
    }
    const auto lines_equal = [&](size_t old_index, size_t new_index) {
        if (old_lines[old_index] != new_lines[new_index]) {
            return false;
        }
        const bool old_is_final = old_index + 1 == old_lines.size();
        const bool new_is_final = new_index + 1 == new_lines.size();
        if (old_is_final && new_is_final) {
            return old_text.empty() || new_text.empty() || (old_text.back() == '\n') == (new_text.back() == '\n');
        }
        return true;
    };

    const size_t rows = old_lines.size() + 1;
    const size_t cols = new_lines.size() + 1;
    std::vector<std::vector<int>> dp(rows, std::vector<int>(cols, 0));

    for (size_t i = 1; i < rows; ++i) {
        for (size_t j = 1; j < cols; ++j) {
            if (lines_equal(i - 1, j - 1)) {
                dp[i][j] = dp[i - 1][j - 1] + 1;
            } else {
                dp[i][j] = std::max(dp[i - 1][j], dp[i][j - 1]);
            }
        }
    }

    using Op = std::pair<char, std::pair<size_t, size_t>>;
    std::vector<Op> ops;
    size_t i = old_lines.size();
    size_t j = new_lines.size();
    while (i > 0 && j > 0) {
        if (lines_equal(i - 1, j - 1)) {
            ops.emplace_back(' ', std::make_pair(i - 1, j - 1));
            --i;
            --j;
        } else if (dp[i][j - 1] >= dp[i - 1][j]) {
            ops.emplace_back('+', std::make_pair(i, j - 1));
            --j;
        } else {
            ops.emplace_back('-', std::make_pair(i - 1, j));
            --i;
        }
    }
    while (j > 0) {
        ops.emplace_back('+', std::make_pair(i, j - 1));
        --j;
    }
    while (i > 0) {
        ops.emplace_back('-', std::make_pair(i - 1, j));
        --i;
    }
    std::reverse(ops.begin(), ops.end());

    for (size_t idx = 0; idx < ops.size();) {
        if (ops[idx].first == ' ') {
            ++idx;
            continue;
        }
        size_t end_idx = idx;
        while (end_idx < ops.size() && ops[end_idx].first != ' ') {
            ++end_idx;
        }
        std::stable_sort(ops.begin() + static_cast<std::ptrdiff_t>(idx),
                         ops.begin() + static_cast<std::ptrdiff_t>(end_idx),
                         [](const Op& a, const Op& b) { return a.first == '-' && b.first == '+'; });
        idx = end_idx;
    }

    struct HunkRange {
        size_t begin;
        size_t end;
    };
    std::vector<HunkRange> hunks;
    for (size_t op_index = 0; op_index < ops.size(); ++op_index) {
        if (ops[op_index].first == ' ') {
            continue;
        }
        const size_t begin = op_index > 3 ? op_index - 3 : 0;
        const size_t end = std::min(ops.size(), op_index + 4);
        if (!hunks.empty() && begin <= hunks.back().end) {
            hunks.back().end = std::max(hunks.back().end, end);
        } else {
            hunks.push_back({begin, end});
        }
    }

    const auto format_range = [](size_t start, size_t count) {
        std::string value = std::to_string(start);
        if (count != 1) {
            value += "," + std::to_string(count);
        }
        return value;
    };
    std::vector<std::string> result;
    for (const auto& hunk : hunks) {
        size_t old_before = 0;
        size_t new_before = 0;
        size_t old_count = 0;
        size_t new_count = 0;
        for (size_t op_index = 0; op_index < hunk.begin; ++op_index) {
            if (ops[op_index].first != '+') ++old_before;
            if (ops[op_index].first != '-') ++new_before;
        }
        for (size_t op_index = hunk.begin; op_index < hunk.end; ++op_index) {
            if (ops[op_index].first != '+') ++old_count;
            if (ops[op_index].first != '-') ++new_count;
        }
        const size_t old_start = old_count == 0 ? old_before : old_before + 1;
        const size_t new_start = new_count == 0 ? new_before : new_before + 1;
        result.push_back("@@ -" + format_range(old_start, old_count) + " +" +
                         format_range(new_start, new_count) + " @@");

        for (size_t op_index = hunk.begin; op_index < hunk.end; ++op_index) {
            const auto& [kind, indices] = ops[op_index];
            if (kind == ' ') {
                result.push_back(" " + old_lines[indices.first]);
                if (indices.first + 1 == old_lines.size() && !old_text.empty() && old_text.back() != '\n') {
                    result.push_back("\\ No newline at end of file");
                }
            } else if (kind == '-') {
                result.push_back("-" + old_lines[indices.first]);
                if (indices.first + 1 == old_lines.size() && !old_text.empty() && old_text.back() != '\n') {
                    result.push_back("\\ No newline at end of file");
                }
            } else {
                result.push_back("+" + new_lines[indices.second]);
                if (indices.second + 1 == new_lines.size() && !new_text.empty() && new_text.back() != '\n') {
                    result.push_back("\\ No newline at end of file");
                }
            }
        }
    }
    return result;
}

}  // namespace twig
