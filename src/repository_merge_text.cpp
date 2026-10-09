#include "twig/repository.hpp"

#include <algorithm>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace twig {

std::string Repository::merge_text(const std::string& base_text, const std::string& ours_text,
                                   const std::string& theirs_text,
                                   const std::string& theirs_label) const {
    if (base_text == ours_text) {
        return theirs_text;
    }
    if (base_text == theirs_text) {
        return ours_text;
    }
    if (ours_text == theirs_text) {
        return ours_text;
    }

    const auto split_lines = [](const std::string& text) {
        std::vector<std::string> lines;
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
    struct Edit {
        size_t start;
        size_t end;
        std::vector<std::string> replacement;
    };
    const auto make_edits = [&](const std::vector<std::string>& variant) {
        const auto base = split_lines(base_text);
        const size_t rows = base.size() + 1;
        const size_t cols = variant.size() + 1;
        std::vector<std::vector<size_t>> lcs(rows, std::vector<size_t>(cols, 0));
        for (size_t i = 1; i < rows; ++i) {
            for (size_t j = 1; j < cols; ++j) {
                lcs[i][j] = base[i - 1] == variant[j - 1]
                                ? lcs[i - 1][j - 1] + 1
                                : std::max(lcs[i - 1][j], lcs[i][j - 1]);
            }
        }
        std::vector<std::pair<char, std::pair<size_t, size_t>>> ops;
        size_t i = base.size();
        size_t j = variant.size();
        while (i > 0 && j > 0) {
            if (base[i - 1] == variant[j - 1]) {
                ops.push_back({' ', {i - 1, j - 1}});
                --i;
                --j;
            } else if (lcs[i][j - 1] >= lcs[i - 1][j]) {
                ops.push_back({'+', {i, j - 1}});
                --j;
            } else {
                ops.push_back({'-', {i - 1, j}});
                --i;
            }
        }
        while (j > 0) {
            ops.push_back({'+', {i, j - 1}});
            --j;
        }
        while (i > 0) {
            ops.push_back({'-', {i - 1, j}});
            --i;
        }
        std::reverse(ops.begin(), ops.end());

        std::vector<Edit> edits;
        size_t base_pos = 0;
        for (size_t op = 0; op < ops.size();) {
            if (ops[op].first == ' ') {
                ++base_pos;
                ++op;
                continue;
            }
            Edit edit{base_pos, base_pos, {}};
            while (op < ops.size() && ops[op].first != ' ') {
                if (ops[op].first == '-') {
                    ++base_pos;
                    edit.end = base_pos;
                } else {
                    edit.replacement.push_back(variant[ops[op].second.second]);
                }
                ++op;
            }
            edits.push_back(std::move(edit));
        }
        return edits;
    };

    const auto base = split_lines(base_text);
    const auto ours = split_lines(ours_text);
    const auto theirs = split_lines(theirs_text);
    const auto ours_edits = make_edits(ours);
    const auto theirs_edits = make_edits(theirs);
    std::vector<std::string> merged;
    size_t cursor = 0;
    size_t oi = 0;
    size_t ti = 0;
    bool has_conflict = false;

    const auto append_base = [&](size_t begin, size_t end, std::vector<std::string>& destination) {
        for (size_t line = begin; line < end && line < base.size(); ++line) {
            destination.push_back(base[line]);
        }
    };
    const auto apply_edits = [&](const std::vector<Edit>& edits, size_t first, size_t last, size_t begin, size_t end) {
        std::vector<std::string> output;
        size_t position = begin;
        for (size_t index = first; index < last; ++index) {
            const auto& edit = edits[index];
            append_base(position, edit.start, output);
            output.insert(output.end(), edit.replacement.begin(), edit.replacement.end());
            position = edit.end;
        }
        append_base(position, end, output);
        return output;
    };
    const auto append_lines = [&](const std::vector<std::string>& lines) {
        merged.insert(merged.end(), lines.begin(), lines.end());
    };

    while (oi < ours_edits.size() || ti < theirs_edits.size()) {
        const size_t ours_start = oi < ours_edits.size() ? ours_edits[oi].start : std::numeric_limits<size_t>::max();
        const size_t theirs_start = ti < theirs_edits.size() ? theirs_edits[ti].start : std::numeric_limits<size_t>::max();
        const size_t next_start = std::min(ours_start, theirs_start);
        append_base(cursor, next_start, merged);
        size_t cluster_start = next_start;
        size_t cluster_end = cluster_start;
        size_t ours_end = oi;
        size_t theirs_end = ti;
        if (ours_start == next_start) {
            cluster_end = std::max(cluster_end, ours_edits[ours_end++].end);
        }
        if (theirs_start == next_start) {
            cluster_end = std::max(cluster_end, theirs_edits[theirs_end++].end);
        }
        bool expanded = true;
        while (expanded) {
            expanded = false;
            while (ours_end < ours_edits.size() && ours_edits[ours_end].start < cluster_end) {
                cluster_end = std::max(cluster_end, ours_edits[ours_end].end);
                ++ours_end;
                expanded = true;
            }
            while (theirs_end < theirs_edits.size() && theirs_edits[theirs_end].start < cluster_end) {
                cluster_end = std::max(cluster_end, theirs_edits[theirs_end].end);
                ++theirs_end;
                expanded = true;
            }
        }
        const auto ours_region = apply_edits(ours_edits, oi, ours_end, cluster_start, cluster_end);
        const auto theirs_region = apply_edits(theirs_edits, ti, theirs_end, cluster_start, cluster_end);
        if (oi == ours_end) {
            append_lines(theirs_region);
        } else if (ti == theirs_end) {
            append_lines(ours_region);
        } else if (ours_region == theirs_region) {
            append_lines(ours_region);
        } else {
            has_conflict = true;
            size_t common_prefix = 0;
            while (common_prefix < ours_region.size() && common_prefix < theirs_region.size() &&
                   ours_region[common_prefix] == theirs_region[common_prefix]) {
                merged.push_back(ours_region[common_prefix]);
                ++common_prefix;
            }
            size_t common_suffix = 0;
            while (common_suffix + common_prefix < ours_region.size() &&
                   common_suffix + common_prefix < theirs_region.size() &&
                   ours_region[ours_region.size() - common_suffix - 1] ==
                       theirs_region[theirs_region.size() - common_suffix - 1]) {
                ++common_suffix;
            }
            merged.push_back("<<<<<<< HEAD");
            merged.insert(merged.end(), ours_region.begin() + static_cast<std::ptrdiff_t>(common_prefix),
                          ours_region.end() - static_cast<std::ptrdiff_t>(common_suffix));
            merged.push_back("=======");
            merged.insert(merged.end(), theirs_region.begin() + static_cast<std::ptrdiff_t>(common_prefix),
                          theirs_region.end() - static_cast<std::ptrdiff_t>(common_suffix));
            merged.push_back(">>>>>>> " + theirs_label);
            if (common_suffix > 0) {
                merged.insert(merged.end(), ours_region.end() - static_cast<std::ptrdiff_t>(common_suffix),
                              ours_region.end());
            }
        }
        cursor = cluster_end;
        oi = ours_end;
        ti = theirs_end;
    }
    append_base(cursor, base.size(), merged);

    if (!has_conflict && ours_text == theirs_text) {
        return ours_text;
    }
    const bool preserve_final_newline = has_conflict ||
                                        (!ours_text.empty() && ours_text.back() == '\n') ||
                                        (!theirs_text.empty() && theirs_text.back() == '\n');
    std::string output;
    for (size_t index = 0; index < merged.size(); ++index) {
        output += merged[index];
        if (index + 1 < merged.size() || preserve_final_newline) {
            output += '\n';
        }
    }
    return output;
}

}  // namespace twig
