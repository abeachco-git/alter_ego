#pragma once
// Functional equivalence for the v3 component graph.
//
// Equipment placement and aggregate requirements are deliberately separate.
// Shared by detailed Compare, recognition and advisory component hints.
#include "ComposableLoadouts.h"
#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace AlterEgo { namespace Composable { namespace Semantics {

struct Summary {
    // "unknown" is not zero and must not silently count as a match.
    bool known = false;
    std::map<std::string, int> counts;
};
inline std::string Identity(const Json& entry,
                            const char* idKey, const char* nameKey) {
    if (entry.contains(idKey) && entry.at(idKey).is_number_integer() &&
        entry.at(idKey).get<int64_t>() > 0)
        return "id:" + entry.at(idKey).dump();
    if (entry.contains(nameKey) && entry.at(nameKey).is_string() &&
        !entry.at(nameKey).get<std::string>().empty())
        return "name:" + entry.at(nameKey).get<std::string>();
    return {};
}
inline void Add(Summary& s, const std::string& id, int count = 1) {
    if (!id.empty() && count > 0) s.counts[id] += count;
}
inline Json Part(const Document& doc, const Loadout& loadout,
                 const std::string& kind) {
    const auto refs=EffectiveRefs(doc,loadout);
    auto it = refs.find(kind);
    if (it == refs.end()) return Json::object();
    const Component* item = Find(doc, kind, it->second);
    return item ? item->data : Json::object();
}
inline Summary Runes(const Json& runes,const std::set<std::string>& knownEmpty={}) {
    Summary summary;
    if (runes.contains("gear") && runes.at("gear").is_object()) {
        const auto& gear = runes.at("gear");
        // A partial rune record is useful as a preview, but cannot prove a
        // six-piece functional match. Names alone do not prove an item ID.
        bool complete = true;
        for (const char* slot : {"Helm", "Shoulders", "Coat",
                                 "Gloves", "Leggings", "Boots"}) {
            const auto it = gear.find(slot);
            if (it == gear.end() || !it->is_object()) {
                if(!knownEmpty.count(slot))complete = false;
                continue;
            }
            const std::string identity = Identity(*it, "rune_id", "rune");
            Add(summary, identity);
            if (identity.rfind("id:", 0) != 0) complete = false;
        }
        if (!summary.counts.empty()||complete) {
            summary.known = complete;
            return summary;
        }
    }
    // Legacy shared rune: its identity is known but its exact count is NOT
    // stated by the field. Do not manufacture 6 rune placements.
    const auto shared = Identity(runes, "rune_id", "rune_name");
    if (!shared.empty()) {
        summary.known = false;
        summary.counts["shared:" + shared] = 1;
    }
    return summary;
}
inline Summary Infusions(const Json& infusions) {
    Summary summary;
    // Aggregate requirement explicitly saved by the user takes precedence
    // over installed per-slot appearances; otherwise we'd count twice.
    if (infusions.contains("infusion_totals") &&
        infusions.at("infusion_totals").is_array() &&
        !infusions.at("infusion_totals").empty()) {
        for (const auto& entry : infusions.at("infusion_totals")) {
            if (!entry.is_object()) continue;
            const std::string id = Identity(entry, "item_id", "attribute");
            if (entry.contains("count") && entry.at("count").is_number_integer()) {
                const int qty = entry.at("count").get<int>();
                Add(summary, id, qty);
            }
        }
        summary.known = !summary.counts.empty();
        return summary;
    }
    if (infusions.contains("gear") && infusions.at("gear").is_object()) {
        for (auto it = infusions.at("gear").begin(); it != infusions.at("gear").end(); ++it) {
            const Json& slot = it.value();
            if (!slot.is_object()) continue;
            if (slot.contains("infusion_ids") && slot.at("infusion_ids").is_array()) {
                for (const auto& id : slot.at("infusion_ids")) {
                    if (id.is_number_integer() && id.get<int64_t>() > 0)
                        Add(summary, "id:" + id.dump());
                }
            }
            // A named infusion without an ID is preserved but cannot establish
            // equivalence to an identified item without further resolution.
            else if (slot.contains("infusion") && slot.at("infusion").is_string())
                Add(summary, "name:" + slot.at("infusion").get<std::string>());
        }
        summary.known = !summary.counts.empty();
    }
    // Manual stat totals are another semantic dimension. They are compared
    // independently from infusion item counts, not conflated with placement.
    return summary;
}
inline std::string WeaponSet(const std::string& slot) {
    if (slot == "WeaponA1" || slot == "WeaponA2") return "terrestrial:A";
    if (slot == "WeaponB1" || slot == "WeaponB2") return "terrestrial:B";
    if (IsAquaticComparisonSlot(slot)) return "aquatic:" + CanonicalComparisonSlot(slot);
    return slot; // Future/unknown set: compare conservatively.
}
inline Summary Sigils(const Json& sigils,bool observedComplete=false) {
    Summary summary;
    if (!sigils.contains("gear") || !sigils.at("gear").is_object())
        return summary;
    bool allIdentified = true;
    for (auto it = sigils.at("gear").begin(); it != sigils.at("gear").end(); ++it) {
        if (!it.value().is_object()) { allIdentified = false; continue; }
        const std::string set = WeaponSet(it.key()) + ":";
        const std::string first = Identity(it.value(), "sigil_id", "sigil");
        const std::string second = Identity(it.value(), "sigil2_id", "sigil2");
        if (!first.empty()) Add(summary, set + first);
        if (!second.empty()) Add(summary, set + second);
        if ((!first.empty() && first.rfind("id:", 0) != 0) ||
            (!second.empty() && second.rfind("id:", 0) != 0) ||
            (first.empty() && second.empty()))
            allIdentified = false;
    }
    summary.known = allIdentified && (!summary.counts.empty()||observedComplete);
    return summary;
}

// Map existing comparison rows onto the same nine units as a v3 Loadout.
// Physical slot rows are not all "gear": armor, weapon, trinket and relic
// have separate source components and separate match states.
inline std::string ComparisonComponent(const std::string& group,
                                       const std::string& field) {
    if (group == "Ranger pets") return "pets";
    if (group == "Revenant legends") return "legends";
    if (group == "Specializations" || group == "Traits" || group == "Build")
        return "base_build";
    if (group == "Terrestrial skills" || group == "Aquatic skills") return "skills";
    if (group == "Aquatic") return "aquatic";
    if (group == "Runes") return "runes";
    if (group == "Sigils") return "sigils";
    if (group == "Infusions" || group == "Infusion totals") return "infusions";
    if (group == "Relic") return "relic";
    if (group == "Weapon types") return EquipmentChoice::IsAquaticWeaponSlot(field)?"aquatic":"weapons";
    if (group == "Exact item IDs" && field == "Relic") return "relic";
    if (group == "Equipment" || group == "Equipment stats" ||
        group == "Exact item IDs") {
        return SlotKind(field);
    }
    return {}; // unmapped rows remain visible in an Other section
}

// A slot's order of utility skills is ergonomic, not a different build.
// Heal and elite retain distinct semantics, as do terrestrial and aquatic.
inline Json SkillBar(const Json& original) { return NormalizeUtilityBar(original); }
inline bool SameUtilities(const Json& a, const Json& b) {
    if (a.is_object() != b.is_object()) return false;
    return SkillBar(a) == SkillBar(b);
}
enum class Verdict { Match, Mismatch, Unverified };
inline Verdict Compare(const Summary& a, const Summary& b) {
    if (!a.known || !b.known) return Verdict::Unverified;
    return a.counts == b.counts ? Verdict::Match : Verdict::Mismatch;
}

// Comparison hints: never discard physical slots or unknown fields.
inline bool SimilarRequirements(const std::string& kind, const Json& a, const Json& b) {
    if (!a.is_object() || !b.is_object() || a.empty() || b.empty() || a == b) return false;
    if (kind == "runes") return Compare(Runes(a), Runes(b)) == Verdict::Match;
    if (kind == "sigils") return Compare(Sigils(a), Sigils(b)) == Verdict::Match;
    // Pooled legacy counts cannot prove slot/attribute/topology requirements.
    // Detailed typed infusion comparison is authoritative; suppress this hint.
    return false;
}

} } } // namespace AlterEgo::Composable::Semantics
