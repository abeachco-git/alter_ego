#pragma once
#include <cstdint>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <type_traits>
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

// Validate both complete public catalogs before replacing a working projection.
// Categories can legitimately be ungrouped and membership can overlap.
inline void ValidateGroupCatalog(const Json& groups, const Json& categories) {
    if (!groups.is_array() || groups.empty() || !categories.is_array() || categories.empty())
        throw std::runtime_error("Incomplete achievement group/category catalog");
    std::set<uint32_t> categoryIds;
    for (const auto& category : categories) {
        if (!category.is_object() || !categoryIds.insert(Id(category.at("id"))).second ||
            !category.at("name").is_string() || category.at("name").get<std::string>().empty() ||
            !category.at("achievements").is_array())
            throw std::runtime_error("Invalid achievement category");
        std::set<uint32_t> members;
        for (const auto& id : category.at("achievements"))
            if (!members.insert(Id(id)).second) throw std::runtime_error("Duplicate achievement membership");
    }
    std::set<std::string> groupIds;
    for (const auto& group : groups) {
        const auto id = group.at("id").get<std::string>();
        if (id.empty() || !groupIds.insert(id).second ||
            !group.at("name").is_string() || group.at("name").get<std::string>().empty() ||
            !group.at("categories").is_array())
            throw std::runtime_error("Invalid achievement group");
        std::set<uint32_t> members;
        for (const auto& value : group.at("categories")) {
            const auto categoryId = Id(value);
            if (!categoryIds.count(categoryId) || !members.insert(categoryId).second)
                throw std::runtime_error("Missing or duplicate group membership");
        }
    }
}
// Compare ids=all against the independently returned public ID list.
template<class IdType>
inline void ValidateCatalogIdentities(const Json& ids, const Json& rows) {
    if (!ids.is_array() || ids.empty() || !rows.is_array() || ids.size()!=rows.size())
        throw std::runtime_error("Incomplete catalog ID coverage");
    std::set<IdType> expected;
    const auto read=[](const Json& value)->IdType {
        if constexpr (std::is_same_v<IdType,uint32_t>) return Id(value);
        else {
            auto id=value.get<std::string>();
            if(id.empty()) throw std::runtime_error("Empty catalog ID");
            return id;
        }
    };
    for(const auto& id:ids) if(!expected.insert(read(id)).second) throw std::runtime_error("Duplicate catalog ID");
    for(const auto& row:rows) if(!expected.erase(read(row.at("id")))) throw std::runtime_error("Unexpected catalog ID");
    if(!expected.empty()) throw std::runtime_error("Missing catalog IDs");
}

inline bool Due(int64_t verifiedAt, int64_t now, int64_t retryAt, bool automatic) {
    if (now < retryAt) return false;
    // First catalog initialization is lazy on entry, not an automatic startup scan.
    if (verifiedAt <= 0) return true;
    return automatic && (now < verifiedAt || now - verifiedAt >= RefreshInterval);
}
} // namespace AlterEgo::AchievementNames
