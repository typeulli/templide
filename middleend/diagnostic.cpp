#include "diagnostic.h"

#include <algorithm>
#include <cctype>

namespace templide::middleend {
    const char* level_name(Level level) {
        switch (level) {
            case Level::Error:
                return "error";
            case Level::Warning:
                return "warning";
            case Level::Info:
                return "info";
        }
        return "error";
    }

    const CatalogEntry* find_catalog(std::string_view code) {
        for (const auto& entry : catalog()) {
            if (code == entry.code) {
                return &entry;
            }
        }
        return nullptr;
    }

    namespace {
        std::string lowered(std::string_view text) {
            std::string result;
            for (const char c : text) {
                result += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            return result;
        }

        // 글자 하나를 지우고 넣고 바꾸거나 이웃한 둘을 맞바꾸는 횟수 (optimal string alignment)
        std::size_t edit_distance(const std::string& a, const std::string& b) {
            std::vector<std::vector<std::size_t>> d(a.size() + 1, std::vector<std::size_t>(b.size() + 1));
            for (std::size_t i = 0; i <= a.size(); ++i) {
                d[i][0] = i;
            }
            for (std::size_t j = 0; j <= b.size(); ++j) {
                d[0][j] = j;
            }
            for (std::size_t i = 1; i <= a.size(); ++i) {
                for (std::size_t j = 1; j <= b.size(); ++j) {
                    const std::size_t cost = a[i - 1] == b[j - 1] ? 0 : 1;
                    d[i][j] = std::min({d[i - 1][j] + 1, d[i][j - 1] + 1, d[i - 1][j - 1] + cost});
                    if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1]) {
                        d[i][j] = std::min(d[i][j], d[i - 2][j - 2] + 1);
                    }
                }
            }
            return d[a.size()][b.size()];
        }
    }

    std::optional<std::string> closest_name(std::string_view name, const std::vector<std::string>& candidates) {
        if (name.size() < 3) {
            return std::nullopt;
        }
        const std::string target = lowered(name);
        const std::size_t limit = std::clamp<std::size_t>(name.size() / 3, 1, 3);
        std::optional<std::string> best;
        std::size_t best_distance = limit + 1;
        for (const auto& candidate : candidates) {
            if (candidate == name) {
                continue;
            }
            const std::size_t distance = edit_distance(target, lowered(candidate));
            if (distance < best_distance) {
                best_distance = distance;
                best = candidate;
            }
        }
        return best;
    }

    std::string did_you_mean(std::string_view name, const std::vector<std::string>& candidates) {
        const auto closest = closest_name(name, candidates);
        return closest ? "did you mean '" + *closest + "'?" : std::string();
    }
}
