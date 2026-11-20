#pragma once

#include <string>
#include <vector>

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

struct ComparisonRow {
    std::string group;
    std::string field;
    std::string saved;
    std::string current;
    bool matches = false;
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

    size_t DifferenceCount() const { return differences; }
    const std::vector<ComparisonRow>& Rows() const { return rows; }

private:
    std::vector<ComparisonRow> rows;
    size_t differences = 0;
};
} // namespace AlterEgo
