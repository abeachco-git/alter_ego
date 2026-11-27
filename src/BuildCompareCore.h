#pragma once

#include <string>
#include <vector>
#include <cstddef>
#include <algorithm>
#include <cstdint>
#include <array>
#include "AscendedStatNames.h"

// UI-independent comparison results. Keys are the canonical values (IDs etc.);
// labels are resolved names and may differ when cache lookups are incomplete.
namespace AlterEgo {
template<class Sequence> inline Sequence CanonicalUtilitySequence(Sequence ids) {
    std::sort(ids.begin(),ids.end());return ids;
}
inline std::array<uint32_t,3> CanonicalUtilityIds(std::array<uint32_t,3> ids){return CanonicalUtilitySequence(ids);}
inline bool CompleteUtilityIds(const std::array<uint32_t,3>& ids) {
    return std::all_of(ids.begin(),ids.end(),[](uint32_t id){return id!=0;});
}
inline std::string CanonicalComparisonSlot(const std::string& slot) {
    if(slot=="AquaticA")return "WeaponAquaticA";
    if(slot=="AquaticB")return "WeaponAquaticB";
    if(slot=="Breather")return "HelmAquatic";
    return slot;
}
inline bool IsAquaticComparisonSlot(const std::string& slot) {
    const auto key=CanonicalComparisonSlot(slot);
    return key=="WeaponAquaticA"||key=="WeaponAquaticB"||key=="HelmAquatic";
}

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
    // Display-only numeric placeholders are not canonical name evidence.
    if ((!savedName.empty() && savedName.front()=='#') ||
        (!liveName.empty() && liveName.front()=='#'))
        return StatEquivalence::NotVerified;
    const auto savedCanonical = NamedStats::CanonicalStatName(savedName);
    const auto liveCanonical = NamedStats::CanonicalStatName(liveName);
    // Cross-website names and GW2 itemstat labels can differ by prefix
    // (Zojja's vs Berserker's, Yassith's vs Viper's). Do not claim a
    // complete match from names alone unless the numeric ID or attributes
    // corroborate them.
    if (!savedName.empty() && !liveName.empty() &&
        ((savedCanonical.empty() || liveCanonical.empty())
            ? NamedStats::NormalizeKey(savedName) != NamedStats::NormalizeKey(liveName)
            : savedCanonical != liveCanonical))
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
    bool completeSkillIds = false; // typed evidence, never inferred from display labels
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

    void AddSkill(const std::string& group,const std::string& field,
                  const std::string& savedKey,const std::string& currentKey,
                  const std::string& savedDisplay,const std::string& currentDisplay,
                  bool completeIds) {
        if (!completeIds) { AddUnverified(group,field,savedDisplay,currentDisplay); return; }
        Add(group,field,savedKey,currentKey,savedDisplay,currentDisplay);
        rows.back().completeSkillIds=true;
    }
    size_t DifferenceCount() const { return differences; }
    size_t UnverifiedCount() const { return unverified; }
    const std::vector<ComparisonRow>& Rows() const { return rows; }

private:
    std::vector<ComparisonRow> rows;
    size_t differences = 0;
    size_t unverified = 0;
};

/* Classify the saved-vs-retrieved build template separately from equipment.
   A near-match is credible only when all non-skill fields agree and every
   changed skill slot has a known ID on both sides. The caller must verify
   complete matching gear before showing a skills-only warning. */
enum class TemplateMatchStatus { Different, SkillsDiffer, Exact };

inline bool IsGearComparisonGroup(const std::string& group) {
    return group == "Equipment" || group == "Equipment stats" ||
           group == "Upgrades" || group == "Runes" || group == "Sigils" ||
           group == "Weapon types" ||
           group == "Infusions" || group == "Infusion totals" || group == "Relic" ||
           group == "Exact item IDs" || group == "Aquatic";
}
// Slot infusion requirements affect equipment matching. Aggregate minimums
// remain independent warnings in the Characters summary.
inline bool IsCharacterEquipmentMatchGroup(const std::string& group) {
    return IsGearComparisonGroup(group) && group != "Infusion totals";
}
inline bool IsSkillComparisonGroup(const std::string& group) {
    return group == "Terrestrial skills" || group == "Aquatic skills";
}
inline TemplateMatchStatus ClassifyTemplateMatch(const BuildComparison& comparison) {
    bool skillsDiffer = false;
    for (const auto& row : comparison.Rows()) {
        if (IsGearComparisonGroup(row.group)) continue;
        if (!row.verified) return TemplateMatchStatus::Different;
        if (row.matches) continue;
        const bool isSkillSlot = row.field == "Heal" || row.field == "Elite" ||
                                 row.field.rfind("Utility ", 0) == 0 || row.field == "Utilities";
        // Unknown/empty skill data is not a confirmed skill change.
        if (!IsSkillComparisonGroup(row.group) || !isSkillSlot ||
            !row.completeSkillIds)
            return TemplateMatchStatus::Different;
        skillsDiffer = true;
    }
    return skillsDiffer ? TemplateMatchStatus::SkillsDiffer
                        : TemplateMatchStatus::Exact;
}
} // namespace AlterEgo
