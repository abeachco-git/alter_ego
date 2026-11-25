#pragma once
#include <cstdint>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace AlterEgo::AchievementNames {
using Json = nlohmann::json;
inline constexpr int64_t RefreshInterval = 86400;
inline constexpr int64_t RetryInterval = 300;
inline constexpr size_t BatchSize = 200;

inline uint32_t Id(const Json& value) {
    if (!value.is_number_integer()) throw std::runtime_error("Invalid achievement ID");
    if (!value.is_number_unsigned() && value.get<int64_t>() <= 0)
        throw std::runtime_error("Invalid achievement ID");
    const auto id = value.get<uint64_t>();
    if (!id || id > std::numeric_limits<uint32_t>::max())
        throw std::runtime_error("Invalid achievement ID");
    return static_cast<uint32_t>(id);
}

inline std::vector<uint32_t> ParseIds(const Json& response) {
    if (!response.is_array() || response.empty())
        throw std::runtime_error("Empty or invalid achievement list");
    std::set<uint32_t> ids;
    for (const auto& item : response)
        if (!ids.insert(Id(item)).second) throw std::runtime_error("Duplicate achievement ID");
    return {ids.begin(), ids.end()};
}

template<class NameMap>
std::vector<uint32_t> Plan(const std::vector<uint32_t>& ids, const NameMap& known, bool full) {
    std::vector<uint32_t> result;
    for (auto id : ids) {
        const auto it = known.find(id);
        if (full || it == known.end() || it->second.empty()) result.push_back(id);
    }
    return result;
}

inline void ValidateBatch(const std::vector<uint32_t>& requested, const Json& response) {
    if (!response.is_array() || response.size() != requested.size())
        throw std::runtime_error("Incomplete achievement batch");
    std::set<uint32_t> expected(requested.begin(), requested.end());
    if (expected.size() != requested.size()) throw std::runtime_error("Duplicate request ID");
    for (const auto& row : response) {
        if (!row.is_object() || !expected.erase(Id(row.at("id"))) ||
            !row.at("name").is_string() || row.at("name").get<std::string>().empty())
            throw std::runtime_error("Invalid achievement definition");
    }
    if (!expected.empty()) throw std::runtime_error("Missing achievement IDs");
}

inline bool Due(int64_t verifiedAt, int64_t now, int64_t retryAt, bool automatic) {
    if (now < retryAt) return false;
    // First catalog initialization is lazy on entry, not an automatic startup scan.
    if (verifiedAt <= 0) return true;
    return automatic && (now < verifiedAt || now - verifiedAt >= RefreshInterval);
}
} // namespace AlterEgo::AchievementNames
