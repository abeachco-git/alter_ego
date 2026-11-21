#pragma once

// Pure, UI-independent comparison of individual infusion requirements.
// Existing saved builds use exact item IDs. An optional attribute override
// allows cosmetic/AR variants (and weaker stat bonuses) to count as equivalent.
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace AlterEgo {

enum class InfusionMatchVerdict { Matches, Different, NotVerified };

struct InfusionRequirement {
    uint32_t exactId = 0;
    std::string attribute; // Empty means exact item ID, nonempty means any positive bonus.
};

struct ObservedInfusion {
    uint32_t itemId = 0;
    std::vector<std::string> positiveAttributes;
    bool metadataKnown = false;
};

inline bool CanMatchInfusion(const InfusionRequirement& req,
                             const ObservedInfusion& item,
                             bool allowUnknownMetadata) {
    if (req.attribute.empty()) return req.exactId != 0 && req.exactId == item.itemId;
    if (!item.metadataKnown) return allowUnknownMetadata;
    return std::find(item.positiveAttributes.begin(), item.positiveAttributes.end(),
                     req.attribute) != item.positiveAttributes.end();
}

inline bool HasCompleteInfusionMatching(
    const std::vector<InfusionRequirement>& saved,
    const std::vector<ObservedInfusion>& live,
    bool allowUnknownMetadata) {
    if (saved.size() != live.size()) return false;
    std::vector<int> matched(live.size(), -1);
    for (size_t requirement = 0; requirement < saved.size(); ++requirement) {
        std::vector<bool> used(live.size(), false);
        // Infusion items are few (typically one to three per equipment piece).
        // Try alternate assignments before rejecting two requirements that
        // might fit multiple otherwise-identical infusions.
        const auto assign = [&](const auto& self, size_t req) -> bool {
            for (size_t j = 0; j < live.size(); ++j) {
                if (used[j] || !CanMatchInfusion(saved[req], live[j], allowUnknownMetadata))
                    continue;
                used[j] = true;
                if (matched[j] == -1 || self(self, static_cast<size_t>(matched[j]))) {
                    matched[j] = static_cast<int>(req);
                    return true;
                }
            }
            return false;
        };
        if (!assign(assign, requirement)) return false;
    }
    return true;
}

inline InfusionMatchVerdict CompareInfusionRequirements(
    const std::vector<InfusionRequirement>& saved,
    const std::vector<ObservedInfusion>& live) {
    if (saved.size() != live.size()) return InfusionMatchVerdict::Different;
    if (HasCompleteInfusionMatching(saved, live, false))
        return InfusionMatchVerdict::Matches;
    if (HasCompleteInfusionMatching(saved, live, true))
        return InfusionMatchVerdict::NotVerified;
    return InfusionMatchVerdict::Different;
}
} // namespace AlterEgo
