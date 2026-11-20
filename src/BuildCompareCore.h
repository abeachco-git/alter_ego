#pragma once

#include <string>
#include <vector>
#include <cstddef>
#include <algorithm>
#include <cstdint>

// UI-independent comparison results. Keys are the canonical values (IDs etc.);
// labels are resolved names and may differ when cache lookups are incomplete.
namespace AlterEgo {
// Relics can be shared across equipment tabs (tab 0) in Hoard & Seek's
// /equipment response, while regular gear belongs to one specific tab.
// Do not accidentally save other shared slots, such as gathering tools.
inline bool IsEquipmentInCapturedBuild(const std::string& slot,
                                       int itemTab, int selectedTab) {
    return itemTab == selectedTab || (slot == "Relic" && itemTab == 0);
}

// Distinguish a real equipment mismatch from incomplete or missing API data.
enum class GearMatchStatus {
    NoSavedGear, NoRetrievedGear, NotVerified, Different, MissingItem, Matches
};
inline GearMatchStatus ClassifyGearMatch(
    bool hasSavedGear, bool hasRetrievedGear,
    size_t comparedGearFields, bool fieldsMatch, bool completeSet,
    size_t missingItems = 0, bool requiredDataMissing = false) {
    if (!hasSavedGear) return GearMatchStatus::NoSavedGear;
    if (!hasRetrievedGear) return GearMatchStatus::NoRetrievedGear;
    if (missingItems != 0) return GearMatchStatus::MissingItem;
    if (!fieldsMatch) return GearMatchStatus::Different;
    if (comparedGearFields == 0 || requiredDataMissing || !completeSet)
        return GearMatchStatus::NotVerified;
    return GearMatchStatus::Matches;
}

// Rank gear variants *after* checking that their build templates match.
// A full gear match wins. Otherwise prefer fewer confirmed mismatches over
// incomplete snapshots or missing API data. Ties retain library order.
struct GearMatchCandidate {
    GearMatchStatus status = GearMatchStatus::NotVerified;
    size_t differingFields = 0;
    size_t missingItems = 0;
    size_t comparedFields = 0;
    size_t savedSlots = 0;
};
inline int GearMatchRank(GearMatchStatus status) {
    switch (status) {
        case GearMatchStatus::Matches: return 0;
        case GearMatchStatus::Different:
        case GearMatchStatus::MissingItem: return 1;
        case GearMatchStatus::NotVerified: return 2;
        case GearMatchStatus::NoRetrievedGear: return 3;
        case GearMatchStatus::NoSavedGear: return 4;
    }
    return 5;
}
inline bool PreferGearMatchCandidate(const GearMatchCandidate& candidate,
                                     const GearMatchCandidate& current) {
    const int a = GearMatchRank(candidate.status), b = GearMatchRank(current.status);
    if (a != b) return a < b;
    if (a == 1) {
        // Missing an entire item is more significant than a single changed
        // upgrade or stat; count the missing-field row plus an extra penalty.
        const size_t aDiff = candidate.differingFields + candidate.missingItems * 2;
        const size_t bDiff = current.differingFields + current.missingItems * 2;
        if (aDiff != bDiff) return aDiff < bDiff;
    }
    if (candidate.comparedFields != current.comparedFields)
        return candidate.comparedFields > current.comparedFields;
    if (candidate.savedSlots != current.savedSlots)
        return candidate.savedSlots > current.savedSlots;
    return false;
}

// Itemstat IDs are not canonical across all gear variants. Match different
// IDs only when the public metadata agrees on both name and attributes.
// If that metadata hasn't loaded, do not label identical names as a mismatch
// (or assume the gear is equivalent).
enum class StatEquivalence { Matches, Different, NotVerified };
inline StatEquivalence CompareStatDefinitions(
    uint32_t savedId, uint32_t liveId,
    const std::string& savedName, const std::string& liveName,
    const std::vector<std::string>& savedAttrs,
    const std::vector<std::string>& liveAttrs) {
    if (savedId != 0 && savedId == liveId) return StatEquivalence::Matches;
    if (!savedName.empty() && !liveName.empty() && savedName != liveName)
        return StatEquivalence::Different;
    if (!savedAttrs.empty() && !liveAttrs.empty()) {
        // Empty attribute entries are incomplete metadata, not evidence of
        // matching characteristics.
        if (std::find(savedAttrs.begin(), savedAttrs.end(), std::string()) != savedAttrs.end() ||
            std::find(liveAttrs.begin(), liveAttrs.end(), std::string()) != liveAttrs.end())
            return StatEquivalence::NotVerified;
        auto a = savedAttrs, b = liveAttrs;
        std::sort(a.begin(), a.end());
        std::sort(b.begin(), b.end());
        if (a != b) return StatEquivalence::Different;
        if (!savedName.empty() && !liveName.empty())
            return StatEquivalence::Matches;
    }
    return StatEquivalence::NotVerified;
}

struct ComparisonRow {
    std::string group;
    std::string field;
    std::string saved;
    std::string current;
    bool matches = false;
    bool verified = true; // NotVerified rows are neither matches nor differences
};

class BuildComparison {
public:
    void Add(const std::string& group, const std::string& field,
             const std::string& savedKey, const std::string& currentKey,
             const std::string& savedDisplay, const std::string& currentDisplay) {
        rows.push_back({group, field, savedDisplay, currentDisplay,
                        savedKey == currentKey});
        if (savedKey != currentKey) ++differences;
    }

    void AddUnverified(const std::string& group, const std::string& field,
                       const std::string& savedDisplay, const std::string& currentDisplay) {
        rows.push_back({group, field, savedDisplay, currentDisplay, false, false});
        ++unverified;
    }

    size_t DifferenceCount() const { return differences; }
    size_t UnverifiedCount() const { return unverified; }
    const std::vector<ComparisonRow>& Rows() const { return rows; }

private:
    std::vector<ComparisonRow> rows;
    size_t differences = 0;
    size_t unverified = 0;
};
} // namespace AlterEgo
