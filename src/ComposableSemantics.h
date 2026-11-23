#pragma once
// Functional equivalence for the v3 component graph.
//
// Equipment placement and aggregate requirements are deliberately separate.
// These summaries power future Compare UI; no live UI behavior changes here.
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
    auto it = loadout.refs.find(kind);
    if (it == loadout.refs.end()) return Json::object();
    const Component* item = Find(doc, kind, it->second);
    return item ? item->data : Json::object();
}
inline Summary Runes(const Json& runes) {
    Summary summary;
    if (runes.contains("gear") && runes.at("gear").is_object()) {
        for (auto it = runes["gear"].begin(); it != runes["gear"].end(); ++it) {
            if (!it.value().is_object()) continue;
            Add(summary, Identity(it.value(), "rune_id", "rune"));
        }
        if (!summary.counts.empty()) { summary.known = true; return summary; }
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
            if (slot.contains("infusion_ids") && slot.at("infusion_ids").is_array())
                for (const auto& id : slot.at("infusion_ids"))
                    if (id.is_number_integer() && id.get<int64_t>() > 0)
                        Add(summary, "id:" + id.dump());
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
    if (slot.rfind("Aquatic", 0) == 0) return "aquatic:" + slot;
    return slot; // Future/unknown set: compare conservatively.
}
inline Summary Sigils(const Json& sigils) {
    Summary summary;
    if (!sigils.contains("gear") || !sigils.at("gear").is_object())
        return summary;
    for (auto it = sigils.at("gear").begin(); it != sigils.at("gear").end(); ++it) {
        if (!it.value().is_object()) continue;
        const std::string set = WeaponSet(it.key()) + ":";
        Add(summary, set + Identity(it.value(), "sigil_id", "sigil"));
        Add(summary, set + Identity(it.value(), "sigil2_id", "sigil2"));
    }
    summary.known = !summary.counts.empty();
    return summary;
}

// A slot's order of utility skills is ergonomic, not a different build.
// Heal and elite retain distinct semantics, as do terrestrial and aquatic.
inline Json SkillBar(const Json& original) {
    if (!original.is_object()) return original;
    Json out = original;
    if (out.contains("utilities") && out.at("utilities").is_array()) {
        std::vector<Json> values;
        for (const auto& value : out.at("utilities")) values.push_back(value);
        std::sort(values.begin(), values.end(),
            [](const Json& a, const Json& b) { return a.dump() < b.dump(); });
        out["utilities"] = std::move(values);
    }
    return out;
}
inline bool SameUtilities(const Json& a, const Json& b) {
    if (a.is_object() != b.is_object()) return false;
    return SkillBar(a) == SkillBar(b);
}
enum class Verdict { Match, Mismatch, Unverified };
inline Verdict Compare(const Summary& a, const Summary& b) {
    if (!a.known || !b.known) return Verdict::Unverified;
    return a.counts == b.counts ? Verdict::Match : Verdict::Mismatch;
}

} } } // namespace AlterEgo::Composable::Semantics
