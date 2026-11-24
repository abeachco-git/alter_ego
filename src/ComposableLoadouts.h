#pragma once
// Alter Ego v3 composable Loadout model.
//
// This is a pure, platform-independent data layer. Build templates, physical
// equipment, and functional upgrade requirements are different concerns.
// The existing SavedBuild representation remains a compatibility view.
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cctype>
#include <limits>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace AlterEgo { namespace Composable {

using Json = nlohmann::json;
constexpr int kVersion = 2;
constexpr const char* kFormat = "alter_ego_component_graph";

inline const std::array<std::string, 9>& Kinds() {
    static const std::array<std::string, 9> values = {{
        "base_build", "skills", "weapons", "sigils", "armor",
        "runes", "trinkets", "relic", "infusions"
    }};
    return values;
}
inline bool IsKind(const std::string& kind) {
    return std::find(Kinds().begin(), Kinds().end(), kind) != Kinds().end();
}
inline bool Fail(std::string& error, const std::string& what) {
    error = what;
    return false;
}

struct Component {
    std::string id, name;
    Json data = Json::object();
};
struct Loadout {
    std::string id;
    Json metadata = Json::object(); // Existing name, notes, modes, provenance, etc.
    std::map<std::string, std::string> refs; // Stable component IDs by category.
    std::string fashion_id;          // Optional reference to FashionLibrary::Look::id.
    std::string equipment_set_id;    // Linked set owns only its included categories.
};
struct EquipmentSet {
    std::string id, name;
    std::map<std::string, std::string> refs;
};
struct Document {
    std::map<std::string, std::vector<Component>> components;
    std::vector<Loadout> loadouts;
    std::vector<EquipmentSet> equipment_sets;
};
inline bool IsEquipmentKind(const std::string& kind) {
    return IsKind(kind) && kind != "base_build" && kind != "skills";
}
inline EquipmentSet* FindEquipmentSet(Document& doc,const std::string& id) {
    for(auto& set:doc.equipment_sets) if(set.id==id)return &set;
    return nullptr;
}
inline const EquipmentSet* FindEquipmentSet(const Document& doc,const std::string& id) {
    for(const auto& set:doc.equipment_sets) if(set.id==id)return &set;
    return nullptr;
}

inline bool SetOwns(const Document& doc,const Loadout& loadout,const std::string& kind) {
    const auto* set=FindEquipmentSet(doc,loadout.equipment_set_id);
    return set&&set->refs.count(kind);
}
// Expand only at projection/edit boundaries; never persist this map.
inline std::map<std::string,std::string> EffectiveRefs(const Document& doc,const Loadout& loadout) {
    auto refs=loadout.refs;
    if(const auto* set=FindEquipmentSet(doc,loadout.equipment_set_id))
        refs.insert(set->refs.begin(),set->refs.end());
    return refs;
}
inline std::vector<std::string> EffectiveComponentUsers(const Document& doc,const std::string& kind,const std::string& id) {
    std::vector<std::string> users;
    for(const auto& l:doc.loadouts) {
        const auto refs=EffectiveRefs(doc,l);auto ref=refs.find(kind);
        if(ref!=refs.end()&&ref->second==id)users.push_back(l.id);
    }
    return users;
}
inline std::vector<std::string> EquipmentSetUsers(const Document& doc,const std::string& id) {
    std::vector<std::string> users;
    for(const auto& l:doc.loadouts)if(l.equipment_set_id==id)users.push_back(l.id);
    return users;
}
inline const Component* Find(const Document& doc, const std::string& kind,
                              const std::string& id) {
    auto it = doc.components.find(kind);
    if (it == doc.components.end()) return nullptr;
    for (const auto& item : it->second) if (item.id == id) return &item;
    return nullptr;
}
inline Component* Find(Document& doc, const std::string& kind,
                       const std::string& id) {
    auto it = doc.components.find(kind);
    if (it == doc.components.end()) return nullptr;
    for (auto& item : it->second) if (item.id == id) return &item;
    return nullptr;
}
inline Loadout* FindLoadout(Document& doc, const std::string& id) {
    for (auto& item : doc.loadouts) if (item.id == id) return &item;
    return nullptr;
}

inline bool Validate(const Document& doc, std::string& error) {
    error.clear();
    for (const auto& [kind, list] : doc.components) {
        if (!IsKind(kind)) return Fail(error, "Unknown component kind: " + kind);
        std::set<std::string> ids;
        for (const auto& item : list) {
            if (item.id.empty() || !ids.insert(item.id).second ||
                !item.data.is_object())
                return Fail(error, "Invalid or duplicate component: " + kind);
        }
    }
    std::set<std::string> loadoutIds;
    for (const auto& item : doc.loadouts) {
        if (item.id.empty() || !loadoutIds.insert(item.id).second ||
            !item.metadata.is_object())
            return Fail(error, "Invalid or duplicate Loadout ID.");
        if (!item.metadata.contains("id") ||
            !item.metadata.at("id").is_string() ||
            item.metadata.at("id").get<std::string>() != item.id)
            return Fail(error, "Loadout metadata ID mismatch.");
        if(!item.equipment_set_id.empty()&&!FindEquipmentSet(doc,item.equipment_set_id))
            return Fail(error,"Unknown linked equipment set.");
        for (const auto& [kind, id] : item.refs) {
            if(SetOwns(doc,item,kind))
                return Fail(error,"Loadout and equipment set cannot both own a category.");
            if (!IsKind(kind) || id.empty() || !Find(doc, kind, id))
                return Fail(error, "Unresolved Loadout component: " + kind);
        }
        if (!item.refs.count("base_build") || !item.refs.count("skills"))
            return Fail(error, "Loadout lacks a Build or skill-selection component.");
    }
    std::set<std::string> setIds;
    for(const auto& set:doc.equipment_sets) {
        if(set.id.empty()||set.name.empty()||!setIds.insert(set.id).second||set.refs.empty())
            return Fail(error,"Invalid equipment set.");
        for(const auto& [kind,id]:set.refs)
            if(!IsEquipmentKind(kind)||!Find(doc,kind,id))
                return Fail(error,"Unresolved equipment set component: "+kind);
    }
    return true;
}

// Slot groups are based on current SavedBuild serialization, not player
// preferences. Unknown future slots remain in trinkets for round-trip safety.
inline std::string SlotKind(const std::string& slot) {
    if (slot.rfind("Weapon", 0) == 0 || slot.rfind("Aquatic", 0) == 0 ||
        slot.rfind("BreatherWeapon", 0) == 0) return "weapons";
    if (slot == "Helm" || slot == "Shoulders" || slot == "Coat" ||
        slot == "Gloves" || slot == "Leggings" || slot == "Boots") return "armor";
    return "trinkets";
}
inline std::string UpgradeKind(const std::string& key) {
    if (key == "rune" || key == "rune_id") return "runes";
    if (key == "sigil" || key == "sigil_id" || key == "sigil2" ||
        key == "sigil2_id") return "sigils";
    if (key == "infusion" || key == "infusion_ids" ||
        key == "infusion_attributes" || key == "infusion_snapshot")
        return "infusions";
    return {};
}
inline const std::map<std::string, std::string>& RootOwnership() {
    static const std::map<std::string, std::string> owners = {
        {"profession","base_build"}, {"specializations","base_build"},
        {"pets","base_build"}, {"legend_codes","base_build"},
        {"weapons","base_build"}, // In-game Build Template weapon skills, NOT equipped items.
        {"skills","skills"}, {"aquatic_skills","skills"},
        {"rune_name","runes"}, {"rune_id","runes"},
        {"relic_name","relic"}, {"relic_id","relic"},
        {"infusion_totals","infusions"},
        {"infusion_summary_present","infusions"},
        {"infusion_ar_total","infusions"},
        {"infusion_stat_totals","infusions"}
    };
    return owners;
}

// A legacy SavedBuild is split without discarding unknown root/slot fields.
// Per-slot upgrades retain their exact physical position for inspection and
// export; comparisons use independently derived functional requirements.
inline std::map<std::string, Json> Split(const Json& old) {
    std::map<std::string, Json> parts;
    for (const auto& kind : Kinds()) parts[kind] = Json::object();
    for (const auto& [field, kind] : RootOwnership())
        if (old.contains(field)) parts[kind][field] = old.at(field);
    if (old.contains("gear") && old.at("gear").is_object()) {
        for (auto it = old["gear"].begin(); it != old["gear"].end(); ++it) {
            const auto& slot = it.key();
            const Json& item = it.value();
            if (!item.is_object()) {
                parts[SlotKind(slot)]["gear"][slot] = item;
                continue;
            }
            // Preserve even an empty slot object, and keep all unknown fields.
            Json base = Json::object();
            for (auto field = item.begin(); field != item.end(); ++field) {
                const auto owner = UpgradeKind(field.key());
                if (owner.empty()) base[field.key()] = field.value();
                else parts[owner]["gear"][slot][field.key()] = field.value();
            }
            parts[SlotKind(slot)]["gear"][slot] = std::move(base);
        }
        if (old.at("gear").empty()) parts["trinkets"]["gear"] = Json::object();
    }
    return parts;
}
inline Json Metadata(const Json& old) {
    Json meta = old;
    for (const auto& [field, kind] : RootOwnership()) {
        (void)kind;
        meta.erase(field);
    }
    meta.erase("gear");
    return meta;
}
// Names describe reusable contents, not the source Loadout.
using ComponentNameLookup = std::function<std::string(const std::string&, uint32_t)>;
inline std::string SuggestComponentName(const std::string& kind, const Json& data,
                                        const ComponentNameLookup& lookup = {}) {
    std::map<std::string, int> names;
    auto add = [&](std::string value) {
        for (const std::string prefix : {"Superior Sigil of the ", "Superior Rune of the ", "Superior Sigil of ", "Superior Rune of ", "Sigil of the ", "Rune of the ", "Sigil of ", "Rune of "}) {
            if (value.rfind(prefix, 0) == 0) { value.erase(0, prefix.size()); break; }
        }
        if (!value.empty()) ++names[value];
    };
    auto field = [&](const Json& obj, const char* key, const char* id = nullptr,
                     const std::string& category = "item") -> std::string {
        if (!obj.is_object()) return {};
        auto it = obj.find(key);
        if (it != obj.end() && it->is_string() && !it->get<std::string>().empty())
            return it->get<std::string>();
        if (id) {
            it = obj.find(id);
            if (it != obj.end() && it->is_number_integer() && *it != 0) {
                if (lookup) {
                    auto resolved = lookup(category, it->get<uint32_t>());
                    if (!resolved.empty()) return resolved;
                }
                return "#" + it->dump();
            }
        }
        return {};
    };
    if (kind == "relic") add(field(data, "relic_name", "relic_id"));
    if (data.contains("gear") && data["gear"].is_object()) {
        for (const auto& slot : data["gear"]) {
            if (kind == "armor" || kind == "trinkets" || kind == "weapons") {
                std::string stat = field(slot, "stat_name", "stat_id", "stat");
                std::string weapon = kind == "weapons" ? field(slot, "weapon_type") : "";
                add(stat.empty() ? weapon : weapon.empty() ? stat : stat + " " + weapon);
            } else if (kind == "runes") add(field(slot, "rune", "rune_id"));
            else if (kind == "sigils") {
                add(field(slot, "sigil", "sigil_id"));
                add(field(slot, "sigil2", "sigil2_id"));
            }
        }
    }
    if (kind == "runes" && names.empty()) add(field(data, "rune_name", "rune_id"));
    if (kind == "infusions") {
        if (data.contains("infusion_stat_totals") && data["infusion_stat_totals"].is_object())
            for (auto it = data["infusion_stat_totals"].begin(); it != data["infusion_stat_totals"].end(); ++it)
                if (it.value().is_number_integer() && it.value().get<int>() > 0)
                    add(it.key() + " +" + it.value().dump());
        if (data.contains("infusion_ar_total") && data["infusion_ar_total"].is_number_integer() &&
            data["infusion_ar_total"].get<int>() > 0)
            add(data["infusion_ar_total"].dump() + " AR");
        if (names.empty() && data.contains("infusion_totals") && data["infusion_totals"].is_array())
            for (const auto& item : data["infusion_totals"])
                add(field(item, "attribute", "item_id"));
    }
    if (kind == "base_build") {
        std::string profession = field(data, "profession");
        if (data.contains("specializations") && data["specializations"].is_array())
            for (const auto& spec : data["specializations"]) {
                const auto id = field(spec, "name", "id", "spec");
                if (!id.empty()) profession += " / " + id;
            }
        return profession.empty() ? "Base Build" : profession;
    }
    if (kind == "skills") {
        std::string name = "Skills";
        if (data.contains("skills") && data["skills"].is_object()) {
            const auto& bar = data["skills"];
            auto elite = field(bar, "elite_name", "elite", "skill");
            auto heal = field(bar, "heal_name", "heal", "skill");
            if (!elite.empty()) name = elite;
            if (!heal.empty()) name += " / " + heal;
            if (bar.contains("utilities") && bar["utilities"].is_array()) {
                std::vector<std::string> utilities;
                for (const auto& id : bar["utilities"]) {
                    if (!id.is_number_integer() || id == 0) continue;
                    const auto skill = field(Json{{"id", id}}, "name", "id", "skill");
                    if (!skill.empty()) utilities.push_back(skill);
                }
                std::sort(utilities.begin(), utilities.end());
                for (const auto& skill : utilities) name += " / " + skill;
            }
        }
        return name;
    }
    std::string name;
    for (const auto& entry : names) {
        if (!name.empty()) name += " / ";
        name += entry.first;
        if (entry.second > 1 && (names.size() > 1 || kind == "runes") && kind != "sigils")
            name += " x" + std::to_string(entry.second);
    }
    if (!name.empty()) return name;
    if (kind == "armor") return "Armor (unspecified)";
    if (kind == "trinkets") return "Trinkets (unspecified)";
    if (kind == "weapons") return "Weapons (unspecified)";
    if (kind == "sigils") return "Sigils (none specified)";
    if (kind == "runes") return "Runes (none specified)";
    if (kind == "relic") return "Relic (none specified)";
    return "Infusions (none specified)";
}

inline const Component* ExactReusableComponent(const Document& doc, const std::string& kind,
                                               const Json& data) {
    // Skills are profession-sensitive; empty placeholders should remain private.
    if (kind == "skills" || data.empty()) return nullptr;
    auto group = doc.components.find(kind);
    if (group != doc.components.end())
        for (const auto& candidate : group->second)
            if (candidate.data == data) return &candidate;
    return nullptr;
}
// Compare only transformations that cannot change equipment compatibility.
// Utility order and complete rune permutations are functionally interchangeable.
// Preserve unknown fields, exact IDs, and physical weapon/infusion capacity.
inline Json ReuseKey(const std::string& kind, Json data) {
    if (kind == "skills") {
        for (const char* key : {"skills", "aquatic_skills"}) {
            if (!data.contains(key) || !data[key].is_object()) continue;
            auto& bar = data[key];
            if (bar.contains("utilities") && bar["utilities"].is_array())
                std::sort(bar["utilities"].begin(), bar["utilities"].end());
        }
    }
    if (kind == "runes" && data.contains("gear") && data["gear"].is_object() &&
        data["gear"].size() == 6) {
        static const std::array<std::string,6> slots = {
            "Helm","Shoulders","Coat","Gloves","Leggings","Boots"};
        std::vector<Json> entries;
        for (const auto& slot : slots) {
            if (!data["gear"].contains(slot)) return data;
            const auto& part = data["gear"][slot];
            if (!part.is_object() || !part.contains("rune_id") ||
                !part["rune_id"].is_number_integer() || part["rune_id"] <= 0)
                return data;
            for (auto it = part.begin(); it != part.end(); ++it)
                if (it.key() != "rune_id" && it.key() != "rune") return data;
            entries.push_back(part);
        }
        std::sort(entries.begin(), entries.end());
        for (size_t i = 0; i < slots.size(); ++i) data["gear"][slots[i]] = entries[i];
    }
    if (kind == "sigils" && data.contains("gear") && data["gear"].is_object()) {
        // Swapping two sigils on the same two-slot weapon changes no effect
        // or capacity. Different physical weapon layouts remain separate.
        for (auto& entry : data["gear"]) {
            if (!entry.is_object() || !entry.contains("sigil_id") || !entry.contains("sigil2_id") ||
                !entry["sigil_id"].is_number_integer() || !entry["sigil2_id"].is_number_integer() ||
                entry["sigil_id"] <= 0 || entry["sigil2_id"] <= 0) continue;
            bool unknown = false;
            for (auto it = entry.begin(); it != entry.end(); ++it)
                if (it.key() != "sigil_id" && it.key() != "sigil" &&
                    it.key() != "sigil2_id" && it.key() != "sigil2") unknown = true;
            if (unknown) continue;
            Json a = {{"id",entry["sigil_id"]}}, b = {{"id",entry["sigil2_id"]}};
            if (entry.contains("sigil")) a["name"] = entry["sigil"];
            if (entry.contains("sigil2")) b["name"] = entry["sigil2"];
            if (b < a) std::swap(a,b);
            entry = {{"sigil_id",a["id"]},{"sigil2_id",b["id"]}};
            if (a.contains("name")) entry["sigil"] = a["name"];
            if (b.contains("name")) entry["sigil2"] = b["name"];
        }
    }
    if (kind == "infusions" && data.contains("gear") && data["gear"].is_object()) {
        for (auto& entry : data["gear"]) {
            if (!entry.is_object() || !entry.contains("infusion_ids") ||
                !entry["infusion_ids"].is_array()) continue;
            const auto& ids = entry["infusion_ids"];
            const auto attrs = entry.value("infusion_attributes", Json::array());
            if (!attrs.is_array() || attrs.size() > ids.size()) continue;
            std::vector<std::pair<Json,Json>> pairs;
            bool valid = true;
            for (size_t i = 0; i < ids.size(); ++i) {
                if (!ids[i].is_number_integer() ||
                    (i < attrs.size() && !attrs[i].is_string())) { valid = false; break; }
                pairs.push_back({ids[i],i < attrs.size() ? attrs[i] : Json("")});
            }
            if (!valid) continue;
            std::sort(pairs.begin(),pairs.end());
            entry["infusion_ids"] = Json::array();
            entry.erase("infusion_attributes");
            Json normalized = Json::array();
            bool any = false;
            for (const auto& pair : pairs) {
                entry["infusion_ids"].push_back(pair.first);
                normalized.push_back(pair.second);
                any = any || pair.second != "";
            }
            if (any) entry["infusion_attributes"] = std::move(normalized);
        }
    }
    return data;
}
inline const Component* ReusableForLoadout(const Document& doc, const std::string& kind,
                                          const Json& data, const std::string& profession) {
    if (data.empty()) return nullptr;
    auto group = doc.components.find(kind);
    if (group == doc.components.end()) return nullptr;
    const auto key = data.dump();
    for (const auto& candidate : group->second) {
        if (candidate.data.dump() != key) continue;
        if (kind == "skills") {
            // Unattached legacy skills lack ownership; only reuse when a
            // same-profession owner proves their context and none conflicts.
            bool known = false, conflict = false;
            for (const auto& loadout : doc.loadouts) {
                auto ref = loadout.refs.find(kind);
                if (ref == loadout.refs.end() || ref->second != candidate.id) continue;
                auto base = loadout.refs.find("base_build");
                const auto* part = base == loadout.refs.end() ? nullptr :
                    Find(doc, "base_build", base->second);
                const std::string owner = part ? part->data.value("profession", std::string()) : "";
                if (!profession.empty() && owner == profession) known = true;
                else conflict = true;
            }
            if (!known || conflict) continue;
        }
        return &candidate;
    }
    return nullptr;
}
inline Json ReuseComparableBuild(const Json& raw) {
    auto parts = Split(raw);
    for (const auto& kind : {std::string("skills"), std::string("runes"), std::string("sigils"), std::string("infusions")})
        parts[kind] = ReuseKey(kind, parts.at(kind));
    return Json{{"metadata", Metadata(raw)}, {"components", parts}};
}
inline std::string ComponentNameKey(std::string name) {
    auto space = [](unsigned char c) { return std::isspace(c) != 0; };
    while (!name.empty() && space(name.front())) name.erase(name.begin());
    while (!name.empty() && space(name.back())) name.pop_back();
    for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return name;
}
inline bool ComponentNameAvailable(const Document& doc, const std::string& kind,
                                   const std::string& name, const std::string& exceptId = {}) {
    auto group = doc.components.find(kind);
    if (group == doc.components.end()) return true;
    const auto key = ComponentNameKey(name);
    for (const auto& part : group->second)
        if (part.id != exceptId && ComponentNameKey(part.name) == key) return false;
    return true;
}
inline std::string UniqueComponentName(const Document& doc, const std::string& kind,
                                       const std::string& proposed) {
    auto group = doc.components.find(kind);
    if (group == doc.components.end()) return proposed;
    std::string name = proposed;
    for (unsigned suffix = 2; ; ++suffix) {
        if (ComponentNameAvailable(doc, kind, name)) return name;
        name = proposed + " (" + std::to_string(suffix) + ")";
    }
}

inline bool SplitLegacy(const Json& legacy, Document& output, std::string& error) {
    error.clear();
    if (!legacy.is_object() || !legacy.contains("builds") ||
        !legacy.at("builds").is_array())
        return Fail(error, "Legacy build library requires a builds array.");
    Document next;
    for (const auto& old : legacy.at("builds")) {
        if (!old.is_object() || !old.contains("id") || !old.at("id").is_string() ||
            old.at("id").get<std::string>().empty())
            return Fail(error, "Legacy build has an invalid ID.");
        Loadout loadout;
        loadout.id = old.at("id").get<std::string>();
        loadout.metadata = Metadata(old);
        const auto parts = Split(old);
        for (const auto& [kind, data] : parts) {
            Component part;
            part.id = kind + ":" + loadout.id;
            part.name = UniqueComponentName(next, kind, SuggestComponentName(kind, data));
            part.data = data;
            next.components[kind].push_back(std::move(part));
            loadout.refs[kind] = kind + ":" + loadout.id;
        }
        next.loadouts.push_back(std::move(loadout));
    }
    if (!Validate(next, error)) return false;
    output = std::move(next);
    return true;
}

// Compose the complete legacy view, including exact positions of runes,
// sigils and infusions. No item identifiers or imported partials are invented.
inline bool Resolve(const Document& doc, const Loadout& loadout,
                    Json& legacy, std::string& error) {
    Json result = loadout.metadata;
    Json gear = Json::object();
    bool hadGear = false;
    if(!loadout.equipment_set_id.empty()&&!FindEquipmentSet(doc,loadout.equipment_set_id))
        return Fail(error,"Missing linked equipment set.");
    for (const auto& [kind, id] : EffectiveRefs(doc,loadout)) {
        const auto* part = Find(doc, kind, id);
        if (!part) return Fail(error, "Missing component during Resolve: " + kind);
        for (auto it = part->data.begin(); it != part->data.end(); ++it) {
            if (it.key() != "gear") {
                if (result.contains(it.key()))
                    return Fail(error, "Conflicting root ownership: " + it.key());
                result[it.key()] = it.value();
            } else {
                if (!it.value().is_object())
                    return Fail(error, "Gear component must use a slot object.");
                hadGear = true;
                for (auto slot = it.value().begin(); slot != it.value().end(); ++slot) {
                    if (!gear.contains(slot.key())) gear[slot.key()] = Json::object();
                    if (!gear[slot.key()].is_object() || !slot.value().is_object()) {
                        if (gear[slot.key()] != Json::object() && gear[slot.key()] != slot.value())
                            return Fail(error, "Conflicting gear slot representation.");
                        gear[slot.key()] = slot.value();
                    } else {
                        for (auto field = slot.value().begin(); field != slot.value().end(); ++field) {
                            if (gear[slot.key()].contains(field.key()))
                                return Fail(error, "Duplicate gear field ownership: " + field.key());
                            gear[slot.key()][field.key()] = field.value();
                        }
                    }
                }
            }
        }
    }
    if (hadGear) result["gear"] = std::move(gear);
    legacy = std::move(result);
    return true;
}
inline bool ToLegacy(const Document& doc, Json& legacy, std::string& error) {
    if (!Validate(doc, error)) return false;
    Json next = Json::object();
    next["builds"] = Json::array();
    for (const auto& loadout : doc.loadouts) {
        Json assembled;
        if (!Resolve(doc, loadout, assembled, error)) return false;
        next["builds"].push_back(std::move(assembled));
    }
    legacy = std::move(next);
    return true;
}
inline Json ToJson(const Document& doc) {
    Json output = {
        {"format", kFormat}, {"version", kVersion},
        {"components", Json::object()}, {"loadouts", Json::array()}
    };
    for (const auto& [kind, list] : doc.components) {
        Json& entries = output["components"][kind] = Json::array();
        for (const auto& item : list)
            entries.push_back({{"id", item.id}, {"name", item.name}, {"data", item.data}});
    }
    for (const auto& item : doc.loadouts)
        output["loadouts"].push_back({
            {"id", item.id}, {"metadata", item.metadata}, {"refs", item.refs},
            {"fashion_id", item.fashion_id}, {"equipment_set_id", item.equipment_set_id}
        });
    if(!doc.equipment_sets.empty()) {
        output["equipment_sets"]=Json::array();
        for(const auto& set:doc.equipment_sets)
            output["equipment_sets"].push_back({{"id",set.id},{"name",set.name},{"refs",set.refs}});
    }
    return output;
}
inline bool FromJson(const Json& input, Document& output, std::string& error) {
    error.clear();
    if (!input.is_object() || !input.contains("format") ||
        !input.at("format").is_string() ||
        input.at("format").get<std::string>() != kFormat ||
        !input.contains("version") || !input.at("version").is_number_integer() ||
        input.at("version").get<int>() != kVersion ||
        !input.contains("components") || !input.at("components").is_object() ||
        !input.contains("loadouts") || !input.at("loadouts").is_array())
        return Fail(error, "Unsupported or corrupt composable Loadout document.");
    Document next;
    try {
        for (auto it = input["components"].begin(); it != input["components"].end(); ++it) {
            if (!it.value().is_array())
                return Fail(error, "Component collection must be an array.");
            for (const auto& raw : it.value())
                next.components[it.key()].push_back({
                    raw.at("id").get<std::string>(), raw.at("name").get<std::string>(),
                    raw.at("data")
                });
        }
        for (const auto& raw : input["loadouts"]) {
            Loadout item;
            item.id = raw.at("id").get<std::string>();
            item.metadata = raw.at("metadata");
            item.refs = raw.at("refs").get<std::map<std::string,std::string>>();
            item.fashion_id = raw.value("fashion_id", std::string());
            item.equipment_set_id = raw.value("equipment_set_id", std::string());
            next.loadouts.push_back(std::move(item));
        }
        if(input.contains("equipment_sets")) {
            if(!input.at("equipment_sets").is_array())return Fail(error,"Invalid equipment sets.");
            for(const auto& raw:input.at("equipment_sets"))
                next.equipment_sets.push_back({raw.at("id").get<std::string>(),
                    raw.at("name").get<std::string>(),raw.at("refs").get<std::map<std::string,std::string>>()});
        }
    } catch (const std::exception& e) {
        return Fail(error, std::string("Invalid Loadout fields: ") + e.what());
    }
    if (!Validate(next, error)) return false;
    output = std::move(next);
    return true;
}

// A portable, single-component interchange format. The original upstream
// monolithic Build format remains the compatibility interface; this v3-only
// format never uses an existing component ID as its import identity.
constexpr const char* kComponentFormat = "alter_ego_loadout_component";
constexpr int kComponentVersion = 1;
inline Json ExportComponent(const std::string& kind, const Component& component) {
    return Json{{"format", kComponentFormat}, {"version", kComponentVersion},
                {"kind", kind}, {"name", component.name}, {"data", component.data}};
}
// Validate only fields understood by upstream SavedBuild. Extension fields
// remain intentionally unrestricted/lossless. This prevents malformed pasted
// JSON from entering native ImGui editors or unsigned GW2 ID conversions.
inline bool NonNegativeInteger(const Json& value) {
    if (value.is_number_unsigned()) return value.get<uint64_t>() <= std::numeric_limits<uint32_t>::max();
    return value.is_number_integer() &&
           value.get<int64_t>() >= 0 && value.get<int64_t>() <= std::numeric_limits<uint32_t>::max();
}
// SQLite returns signed integers even for valid unsigned GW2 item IDs.
inline bool ReadItemId(const Json& value,uint32_t& id) {
    if(!NonNegativeInteger(value))return false;
    id=value.get<uint32_t>();return true;
}
inline bool ValidPortableGearField(const std::string& field, const Json& value) {
    if (field == "item_id" || field == "stat_id" ||
        field == "rune_id" || field == "sigil_id" || field == "sigil2_id")
        return NonNegativeInteger(value);
    if (field == "infusion_snapshot") return value.is_boolean();
    if (field == "stat_name" || field == "rune" || field == "sigil" ||
        field == "sigil2" || field == "infusion" || field == "weapon_type")
        return value.is_string();
    if (field == "infusion_ids") {
        if (!value.is_array()) return false;
        for (const auto& id : value) if (!NonNegativeInteger(id)) return false;
    }
    if (field == "infusion_attributes") {
        if (!value.is_array()) return false;
        for (const auto& name : value) if (!name.is_string()) return false;
    }
    return true;
}
inline bool ValidatePortablePayload(const std::string& kind,
                                    const Json& data, std::string& error) {
    (void)kind;
    if (!data.is_object()) return Fail(error, "Component data must be an object.");
    for (auto it = data.begin(); it != data.end(); ++it) {
        const auto& field = it.key();
        const auto& value = it.value();
        if (field == "gear") {
            if (!value.is_object()) return Fail(error, "Gear must be a slot object.");
            for (auto slot = value.begin(); slot != value.end(); ++slot) {
                if (!slot.value().is_object())
                    return Fail(error, "Gear slot payload must be an object.");
                for (auto part = slot.value().begin(); part != slot.value().end(); ++part)
                    if (!ValidPortableGearField(part.key(), part.value()))
                        return Fail(error, "Invalid gear field: " + part.key());
            }
        } else if (field == "profession" || field == "rune_name" ||
                   field == "relic_name") {
            if (!value.is_string()) return Fail(error, "Invalid text field: " + field);
        } else if (field == "rune_id" || field == "relic_id" ||
                   field == "infusion_ar_total") {
            if (!NonNegativeInteger(value))
                return Fail(error, "Invalid numeric field: " + field);
        } else if (field == "infusion_summary_present") {
            if (!value.is_boolean())
                return Fail(error, "Invalid infusion-summary flag.");
        } else if (field == "weapons" || field == "pets" ||
                   field == "legend_codes") {
            if (!value.is_array())
                return Fail(error, "Invalid build array: " + field);
            for (const auto& number : value)
                if (!NonNegativeInteger(number))
                    return Fail(error, "Invalid build array entry: " + field);
        } else if (field == "specializations") {
            if (!value.is_array())
                return Fail(error, "Specializations must be an array.");
            for (const auto& spec : value) {
                if (!spec.is_object()) return Fail(error, "Invalid specialization.");
                if (spec.contains("id") && !NonNegativeInteger(spec["id"]))
                    return Fail(error, "Invalid specialization ID.");
                if (spec.contains("traits")) {
                    if (!spec["traits"].is_array())
                        return Fail(error, "Traits must be an array.");
                    for (const auto& trait : spec["traits"])
                        if (!trait.is_number_integer())
                            return Fail(error, "Invalid trait selection.");
                }
            }
        } else if (field == "skills" || field == "aquatic_skills") {
            if (!value.is_object()) return Fail(error, "Skill bar must be an object.");
            for (const auto& skillType : {"heal", "elite"})
                if (value.contains(skillType) && !NonNegativeInteger(value[skillType]))
                    return Fail(error, "Invalid skill ID.");
            if (value.contains("utilities")) {
                if (!value["utilities"].is_array())
                    return Fail(error, "Utilities must be an array.");
                for (const auto& skill : value["utilities"])
                    if (!NonNegativeInteger(skill))
                        return Fail(error, "Invalid utility skill ID.");
            }
        } else if (field == "infusion_totals") {
            if (!value.is_array()) return Fail(error, "Infusion totals must be an array.");
            for (const auto& req : value) {
                if (!req.is_object()) return Fail(error, "Invalid infusion requirement.");
                if (req.contains("item_id") && !NonNegativeInteger(req["item_id"]))
                    return Fail(error, "Invalid infusion requirement ID.");
                if (req.contains("count") && !NonNegativeInteger(req["count"]))
                    return Fail(error, "Invalid infusion count.");
                if (req.contains("attribute") && !req["attribute"].is_string())
                    return Fail(error, "Invalid infusion attribute.");
            }
        } else if (field == "infusion_stat_totals") {
            if (!value.is_object()) return Fail(error, "Infusion stats must be an object.");
            for (auto attr = value.begin(); attr != value.end(); ++attr)
                if (!NonNegativeInteger(attr.value()))
                    return Fail(error, "Invalid infusion stat total.");
        }
    }
    return true;
}

inline bool ImportComponent(const Json& input, std::string& kind,
                            Component& component, std::string& error) {
    error.clear();
    if (!input.is_object() || !input.contains("format") ||
        !input.at("format").is_string() ||
        input.at("format").get<std::string>() != kComponentFormat ||
        !input.contains("version") || !input.at("version").is_number_integer() ||
        input.at("version").get<int>() != kComponentVersion ||
        !input.contains("kind") || !input.at("kind").is_string() ||
        !input.contains("name") || !input.at("name").is_string() ||
        !input.contains("data") || !input.at("data").is_object())
        return Fail(error, "Not a supported component export.");
    const std::string parsedKind = input.at("kind").get<std::string>();
    if (!IsKind(parsedKind) || input.at("name").get<std::string>().empty())
        return Fail(error, "Invalid component type or name.");
    const Json& data = input.at("data");
    // A v3 component owns only its root fields. This makes exported pieces
    // assignable without causing duplicate root ownership during Resolve().
    for (auto it = data.begin(); it != data.end(); ++it) {
        if (it.key() == "gear") {
            if (!it.value().is_object())
                return Fail(error, "Gear fields must form a slot object.");
            for (auto slot = it.value().begin(); slot != it.value().end(); ++slot) {
                if (parsedKind != "runes" && parsedKind != "sigils" &&
                    parsedKind != "infusions" && SlotKind(slot.key()) != parsedKind)
                    return Fail(error, "Equipment slot belongs to another component.");
                if (!slot.value().is_object())
                    return Fail(error, "Gear slot must contain named fields.");
                for (auto field = slot.value().begin(); field != slot.value().end(); ++field) {
                    const std::string upgrade = UpgradeKind(field.key());
                    if (upgrade.empty()) {
                        if (parsedKind == "runes" || parsedKind == "sigils" ||
                            parsedKind == "infusions")
                            return Fail(error, "Physical equipment field in upgrade component.");
                    } else if (upgrade != parsedKind)
                        return Fail(error, "Upgrade field belongs to another component.");
                }
            }
        } else {
            const auto owner = RootOwnership().find(it.key());
            if (owner != RootOwnership().end() && owner->second != parsedKind)
                return Fail(error, "Field belongs to another component: " + it.key());
        }
    }
    if (!ValidatePortablePayload(parsedKind, data, error)) return false;
    kind = parsedKind;
    component = {"", input.at("name").get<std::string>(), data};
    return true;
}

// Single-Loadout portable interchange. Embedded components have no library
// identity; importing always creates new independent component IDs, preventing
// accidental edits to an unrelated existing shared component.
constexpr const char* kPortableLoadoutFormat = "alter_ego_portable_loadout";
constexpr int kPortableLoadoutVersion = 2;
inline bool ExportPortableLoadout(const Document& doc, const std::string& id,
                                  Json& packet, std::string& error) {
    if (!Validate(doc, error)) return false;
    const auto it = std::find_if(doc.loadouts.begin(), doc.loadouts.end(),
        [&](const Loadout& item){ return item.id == id; });
    if (it == doc.loadouts.end()) return Fail(error, "Loadout not found.");
    Json components = Json::object();
    for (const auto& [kind, ref] : EffectiveRefs(doc,*it)) {
        const Component* part = Find(doc, kind, ref);
        if (!part) return Fail(error, "Loadout references an unavailable component.");
        components[kind] = {{"name", part->name}, {"data", part->data}};
    }
    packet = Json{{"format", kPortableLoadoutFormat},
                  {"version", kPortableLoadoutVersion},
                  {"metadata", it->metadata},
                  {"components", components},
                  {"fashion_id", it->fashion_id}};
    if(const auto* set=FindEquipmentSet(doc,it->equipment_set_id)) {
        packet["equipment_set_name"]=set->name;
        packet["equipment_set_kinds"]=Json::array();
        for(const auto& [kind,id]:set->refs)packet["equipment_set_kinds"].push_back(kind);
    }
    return true;
}

// Transactional mutation utilities, shared by the future UI and the
// compatibility adapter. Existing component updates are intentionally shared.
inline bool Transaction(Document& doc,
    const std::function<bool(Document&,std::string&)>& operation, std::string& error) {
    error.clear();
    Document next = doc;
    if (!operation(next, error) || !Validate(next, error)) return false;
    doc = std::move(next);
    return true;
}
inline bool ImportPortableLoadout(Document& doc, const Json& packet,
                                  const std::string& newId, std::string& error) {
    if (!packet.is_object() || !packet.contains("format") ||
        !packet["format"].is_string() ||
        packet["format"].get<std::string>() != kPortableLoadoutFormat ||
        !packet.contains("version") || !packet["version"].is_number_integer() ||
        packet["version"].get<int>() != kPortableLoadoutVersion ||
        !packet.contains("metadata") || !packet["metadata"].is_object() ||
        !packet.contains("components") || !packet["components"].is_object() ||
        newId.empty())
        return Fail(error, "Unsupported or incomplete portable Loadout.");
    // Parse completely before transaction. A malformed component must leave
    // both references and source library untouched.
    std::map<std::string, Component> imported;
    for (auto it = packet["components"].begin();
         it != packet["components"].end(); ++it) {
        if (!IsKind(it.key()) || !it.value().is_object() ||
            !it.value().contains("name") ||
            !it.value().contains("data"))
            return Fail(error, "Invalid embedded component.");
        std::string kind;
        Component part;
        if (!ImportComponent(Json{
            {"format", kComponentFormat},
            {"version", kComponentVersion},
            {"kind", it.key()},
            {"name", it.value()["name"]},
            {"data", it.value()["data"]}}, kind, part, error))
            return false;
        imported[kind] = std::move(part);
    }
    if (!imported.count("base_build") || !imported.count("skills"))
        return Fail(error, "Portable Loadout requires Base Build and Skills.");
    const auto fashion = packet.find("fashion_id");
    if (fashion != packet.end() && !fashion->is_string())
        return Fail(error, "Invalid optional Fashion reference.");
    return Transaction(doc, [&](Document& next, std::string& err) {
        if (FindLoadout(next, newId))
            return Fail(err, "New Loadout ID is already in use.");
        Loadout loadout;
        loadout.id = newId;
        loadout.metadata = packet["metadata"];
        loadout.metadata["id"] = newId;
        loadout.fashion_id = fashion == packet.end()
            ? std::string() : fashion->get<std::string>();
        const auto profession = imported.at("base_build").data.value("profession", std::string());
        for (auto& [kind, part] : imported) {
            if (const auto* match = ReusableForLoadout(next, kind, part.data, profession)) {
                loadout.refs[kind] = match->id;
                continue;
            }
            std::string componentId = kind + ":" + newId;
            unsigned sequence = 1;
            while (Find(next, kind, componentId))
                componentId = kind + ":" + newId + ":" +
                              std::to_string(sequence++);
            part.id = componentId;
            part.name = UniqueComponentName(next, kind, part.name);
            next.components[kind].push_back(std::move(part));
            loadout.refs[kind] = componentId;
        }
        if(packet.contains("equipment_set_name")) {
            if(!packet.at("equipment_set_name").is_string()||packet.at("equipment_set_name").get<std::string>().empty())
                return Fail(err,"Invalid portable equipment set name.");
            EquipmentSet set{"equipment-set:"+newId,packet.at("equipment_set_name").get<std::string>(),{}};
            for(unsigned n=1;FindEquipmentSet(next,set.id);++n)set.id="equipment-set:"+newId+":"+std::to_string(n);
            if(!packet.contains("equipment_set_kinds")||!packet.at("equipment_set_kinds").is_array())
                return Fail(err,"Missing portable equipment set membership.");
            for(const auto& value:packet.at("equipment_set_kinds")) {
                if(!value.is_string())return Fail(err,"Invalid portable set category.");
                const auto kind=value.get<std::string>();
                auto ref=loadout.refs.find(kind);
                if(!IsEquipmentKind(kind)||ref==loadout.refs.end())return Fail(err,"Invalid or duplicate portable set category.");
                set.refs.insert(*ref);loadout.refs.erase(ref);
            }
            loadout.equipment_set_id=set.id;
            next.equipment_sets.push_back(std::move(set));
        }
        next.loadouts.push_back(std::move(loadout));
        return true;
    }, error);
}

inline std::string ProfessionOf(const Document& doc, const Loadout& loadout) {
    const auto ref = loadout.refs.find("base_build");
    if (ref == loadout.refs.end()) return {};
    const auto* part = Find(doc, "base_build", ref->second);
    return part ? part->data.value("profession", std::string()) : std::string();
}

inline bool AssignCompatible(const Document& doc, const Loadout& target,
                             const std::string& kind, const std::string& id,
                             std::string& error) {
    if (kind != "base_build" && kind != "skills") return true;
    const auto* incoming = Find(doc, kind, id);
    if (!incoming) return Fail(error, "Component does not exist.");
    const std::string targetProfession = ProfessionOf(doc, target);
    if (kind == "base_build") {
        const std::string incomingProfession =
            incoming->data.value("profession", std::string());
        if (!incomingProfession.empty() && !targetProfession.empty() &&
            incomingProfession != targetProfession)
            return Fail(error, "Cannot assign a Base Build from another profession.");
    } else if (!targetProfession.empty()) {
        for (const auto& loadout : doc.loadouts) {
            const auto ref = loadout.refs.find("skills");
            if (ref == loadout.refs.end() || ref->second != id) continue;
            const std::string usedBy = ProfessionOf(doc, loadout);
            if (!usedBy.empty() && usedBy != targetProfession)
                return Fail(error, "Skills component is used by another profession.");
        }
    }
    return true;
}

inline bool Assign(Document& doc, const std::string& loadoutId,
                   const std::string& kind, const std::string& id,
                   std::string& error) {
    return Transaction(doc, [&](Document& next, std::string& err) {
        auto* item = FindLoadout(next, loadoutId);
        if (!item || !IsKind(kind) || !Find(next, kind, id))
            return Fail(err, "Loadout or component not found.");
        if(SetOwns(next,*item,kind))return Fail(err,"Edit the linked equipment set to assign this category.");
        if (!AssignCompatible(next, *item, kind, id, err)) return false;
        item->refs[kind] = id;
        return true;
    }, error);
}
inline bool UpdateShared(Document& doc, const std::string& kind,
                         const std::string& id, const Json& data,
                         std::string& error) {
    return Transaction(doc, [&](Document& next, std::string& err) {
        auto* item = Find(next, kind, id);
        if (!item || !data.is_object()) return Fail(err, "Invalid shared component.");
        if (kind == "base_build") {
            const auto previous = item->data.value("profession", std::string());
            const auto nextProfession = data.value("profession", std::string());
            if (previous != nextProfession)
                for (const auto& loadout : next.loadouts) {
                    auto ref = loadout.refs.find("base_build");
                    if (ref != loadout.refs.end() && ref->second == id)
                        return Fail(err, "Cannot change profession of a referenced Base Build.");
                }
        }
        item->data = data;
        return true;
    }, error);
}
// A captured skill bar is profession-specific even though the Skills payload
// intentionally has no profession root field. Verify ALL current users before
// replacing an existing shared component from live character data.
inline bool CaptureProfessionCompatible(const Document& doc,
                                         const std::string& kind,
                                         const std::string& id,
                                         const std::string& profession,
                                         std::string& error) {
    if (kind != "base_build" && kind != "skills") return true;
    if (profession.empty()) return Fail(error, "Captured profession is unknown.");
    const auto* target = Find(doc, kind, id);
    if (!target) return Fail(error, "Target component does not exist.");
    if (kind == "base_build") {
        const std::string prior = target->data.value("profession", std::string());
        if (!prior.empty() && prior != profession)
            return Fail(error, "Captured Build profession differs from saved profession.");
    }
    for (const auto& loadout : doc.loadouts) {
        const auto ref = loadout.refs.find(kind);
        if (ref == loadout.refs.end() || ref->second != id) continue;
        const auto baseRef = loadout.refs.find("base_build");
        if (baseRef == loadout.refs.end()) continue;
        const auto* base = Find(doc, "base_build", baseRef->second);
        if (!base) continue;
        const std::string expected = base->data.value("profession", std::string());
        if (!expected.empty() && expected != profession)
            return Fail(error,
                "Captured profession differs from a Loadout using this component.");
    }
    return true;
}

// A live character snapshot may not know third-party/custom component fields.
// Replace recorded fields, but retain only *unknown* root/gear extensions.
// Known equipment or upgrade fields omitted by a new capture are intentionally
// removed; they must not masquerade as current installed items.
inline Json ReplaceCapturedFieldsPreservingExtensions(const Json& previous,
                                                      const Json& captured) {
    if (!previous.is_object() || !captured.is_object()) return captured;
    Json result = captured;
    for (auto it = previous.begin(); it != previous.end(); ++it) {
        if (it.key() == "gear" && it.value().is_object()) {
            for (auto slot = it.value().begin(); slot != it.value().end(); ++slot) {
                if (!slot.value().is_object()) continue;
                for (auto field = slot.value().begin();
                     field != slot.value().end(); ++field) {
                    static const std::set<std::string> knownFields = {
                        "item_id", "infusion_snapshot", "infusion_ids",
                        "infusion_attributes", "stat_id", "stat_name",
                        "rune", "rune_id", "sigil", "sigil_id",
                        "sigil2", "sigil2_id", "infusion", "weapon_type"
                    };
                    if (knownFields.count(field.key())) continue;
                    if (!result.contains("gear")) result["gear"] = Json::object();
                    if (!result["gear"].contains(slot.key()))
                        result["gear"][slot.key()] = Json::object();
                    if (result["gear"][slot.key()].is_object() &&
                        !result["gear"][slot.key()].contains(field.key()))
                        result["gear"][slot.key()][field.key()] = field.value();
                }
            }
        } else if (!RootOwnership().count(it.key()) &&
                   !result.contains(it.key())) {
            result[it.key()] = it.value();
        }
    }
    return result;
}

// Rename and edit together, with one validation/commit. A failure cannot
// leave the component's name and payload at different revisions.
inline bool UpdateSharedNamed(Document& doc, const std::string& kind,
                              const std::string& id, const std::string& name,
                              const Json& data, std::string& error) {
    return Transaction(doc, [&](Document& next, std::string& err) {
        auto* item = Find(next, kind, id);
        if (!item || name.empty() || !data.is_object())
            return Fail(err, "Invalid shared component name or payload.");
        if (kind == "base_build" &&
            item->data.value("profession", std::string()) !=
            data.value("profession", std::string())) {
            for (const auto& loadout : next.loadouts) {
                const auto ref = loadout.refs.find("base_build");
                if (ref != loadout.refs.end() && ref->second == id)
                    return Fail(err, "Cannot change profession of a referenced Base Build.");
            }
        }
        if (ComponentNameKey(name) != ComponentNameKey(item->name) &&
            !ComponentNameAvailable(next, kind, name, id))
            return Fail(err, "A component with this name already exists.");
        item->name = name;
        item->data = data;
        return true;
    }, error);
}
inline bool CopyForLoadout(Document& doc, const std::string& loadoutId,
                          const std::string& kind, const std::string& id,
                          const std::string& name, std::string& error) {
    return Transaction(doc, [&](Document& next, std::string& err) {
        auto* item = FindLoadout(next, loadoutId);
        if (!item || !IsKind(kind) || !item->refs.count(kind) ||
            id.empty() || Find(next, kind, id))
            return Fail(err, "Cannot copy component for this Loadout.");
        const auto* source = Find(next, kind, item->refs.at(kind));
        if (!source) return Fail(err, "Original component missing.");
        Component copied = *source;
        copied.id = id;
        copied.name = UniqueComponentName(next, kind, name);
        next.components[kind].push_back(std::move(copied));
        item->refs[kind] = id;
        return true;
    }, error);
}
// Copy & Edit is a single, cancellable action. The UI prepares the draft
// without changing the original graph, and only this transaction creates the
// new component and switches the one Loadout's reference.
inline bool CopyEditedForLoadout(Document& doc, const std::string& loadoutId,
                                 const std::string& kind, const std::string& newId,
                                 const std::string& name, const Json& editedData,
                                 std::string& error,
                                 const std::string& expectedSourceId = {}) {
    return Transaction(doc, [&](Document& next, std::string& err) {
        auto* loadout = FindLoadout(next, loadoutId);
        if (!loadout || !IsKind(kind) || !loadout->refs.count(kind) ||
            newId.empty() || name.empty() || !editedData.is_object() ||
            Find(next, kind, newId))
            return Fail(err, "Invalid private component request.");
        if (!expectedSourceId.empty() &&
            loadout->refs.at(kind) != expectedSourceId)
            return Fail(err, "The Loadout component changed while the private editor was open.");
        const auto* source = Find(next, kind, loadout->refs.at(kind));
        if (!source) return Fail(err, "Original component no longer exists.");
        // Do not change the owning Loadout's profession behind the
        // existing Skills reference, even in a private component copy.
        if (kind == "base_build" &&
            source->data.value("profession", std::string()) !=
            editedData.value("profession", std::string()))
            return Fail(err, "Cannot change profession of a referenced Base Build.");
        Component copy = *source;
        copy.id = newId;
        copy.name = UniqueComponentName(next, kind, name);
        copy.data = editedData;
        next.components[kind].push_back(std::move(copy));
        loadout->refs[kind] = newId;
        return true;
    }, error);
}

inline bool LinkFashion(Document& doc, const std::string& loadoutId,
                        const std::string& fashionId, std::string& error) {
    return Transaction(doc, [&](Document& next, std::string& err) {
        auto* item = FindLoadout(next, loadoutId);
        if (!item) return Fail(err, "Loadout not found.");
        // Fashion Library is a separate store. Missing appearance is handled
        // in UI as an unresolved optional reference; don't erase a user ID.
        item->fashion_id = fashionId;
        return true;
    }, error);
}
inline bool RemoveLoadout(Document& doc, const std::string& id, std::string& error) {
    return Transaction(doc, [&](Document& next, std::string& err) {
        auto it = std::find_if(next.loadouts.begin(), next.loadouts.end(),
            [&](const Loadout& value) { return value.id == id; });
        if (it == next.loadouts.end()) return Fail(err, "Loadout not found.");
        next.loadouts.erase(it); // Never delete reusable referenced components.
        return true;
    }, error);
}
inline bool RemoveUnreferenced(Document& doc, const std::string& kind,
                              const std::string& id, std::string& error) {
    return Transaction(doc, [&](Document& next, std::string& err) {
        if (!IsKind(kind)) return Fail(err, "Unknown component category.");
        for (const auto& item : next.loadouts)
            if (item.refs.count(kind) && item.refs.at(kind) == id)
                return Fail(err, "Cannot remove a component still in use.");
        for(const auto& set:next.equipment_sets)
            if(set.refs.count(kind)&&set.refs.at(kind)==id)
                return Fail(err,"Cannot remove a component used by an equipment set.");
        auto& list = next.components[kind];
        auto it = std::find_if(list.begin(), list.end(),
            [&](const Component& component) { return component.id == id; });
        if (it == list.end()) return Fail(err, "Component not found.");
        list.erase(it);
        return true;
    }, error);
}

// Reconcile a legacy UI view into a detached v3 document. Unmodified shared
// references remain shared. An old monolithic editor changing only one Loadout
// creates a private component automatically rather than changing its peers.
// Keep unrecognized fields from newer/foreign legacy formats when an older
// UI changes an otherwise supported SavedBuild. Known optional keys may be
// intentionally removed, so only unknown keys are inherited.
inline const std::set<std::string>& KnownLegacyKeys() {
    static const std::set<std::string> keys = {
        "id", "name", "source_character", "source_url", "import_warning",
        "chat_link", "ae2_code", "profession", "game_mode", "game_modes",
        "notes", "created", "specializations", "skills", "aquatic_skills",
        "pets", "legend_codes", "weapons", "gear", "infusion_totals",
        "infusion_summary_present", "infusion_ar_total", "infusion_stat_totals",
        "rune_name", "rune_id", "relic_name", "relic_id"
    };
    return keys;
}
inline Json PreserveUnknown(const Json& prior, const Json& edited) {
    Json result = edited;
    for (auto it = prior.begin(); it != prior.end(); ++it) {
        if (!KnownLegacyKeys().count(it.key()) && !result.contains(it.key()))
            result[it.key()] = it.value();
    }
    if (prior.contains("gear") && prior.at("gear").is_object() &&
        result.contains("gear") && result.at("gear").is_object()) {
        for (auto slot = result["gear"].begin(); slot != result["gear"].end(); ++slot) {
            if (!slot.value().is_object() ||
                !prior.at("gear").contains(slot.key()) ||
                !prior.at("gear")[slot.key()].is_object()) continue;
            for (auto field = prior.at("gear")[slot.key()].begin();
                 field != prior.at("gear")[slot.key()].end(); ++field) {
                const std::string& key = field.key();
                static const std::set<std::string> gearFields = {
                    "item_id", "infusion_snapshot", "infusion_ids",
                    "infusion_attributes", "stat_id", "stat_name",
                    "rune", "rune_id", "sigil", "sigil_id",
                    "sigil2", "sigil2_id", "infusion", "weapon_type"
                };
                if (!gearFields.count(key) && !slot.value().contains(key))
                    slot.value()[key] = field.value();
            }
        }
    }
    return result;
}

inline bool Reconcile(const Document& original, const Json& legacy,
                      Document& output, std::string& error,
                      const ComponentNameLookup& lookup = {}) {
    if (!Validate(original, error)) return false;
    Document next = original;
    if (!legacy.is_object() || !legacy.contains("builds") ||
        !legacy.at("builds").is_array())
        return Fail(error, "Reconcile requires a builds array.");
    std::set<std::string> seen;
    std::vector<Loadout> order;
    Json expected = Json::array();
    for (const auto& raw : legacy.at("builds")) {
        const auto* preceding = (raw.is_object() && raw.contains("id") && raw.at("id").is_string())
            ? FindLoadout(next, raw.at("id").get<std::string>()) : nullptr;
        Json previous;
        if (preceding && !Resolve(next, *preceding, previous, error)) return false;
        const Json item = preceding ? PreserveUnknown(previous, raw) : raw;
        expected.push_back(item);
        if (!item.is_object() || !item.contains("id") ||
            !item.at("id").is_string() || item.at("id").get<std::string>().empty())
            return Fail(error, "Legacy view has a missing ID.");
        const std::string id = item.at("id").get<std::string>();
        if (!seen.insert(id).second) return Fail(error, "Duplicate Loadout ID.");
        Loadout* existing = FindLoadout(next, id);
        if (!existing) {
            Loadout created;
            created.id = id;
            created.metadata = Metadata(item);
            const auto parts = Split(item);
            for (const auto& [kind, data] : parts) {
                std::string componentId = kind + ":" + id;
                unsigned sequence = 1;
                while (Find(next, kind, componentId))
                    componentId = kind + ":" + id + ":" + std::to_string(sequence++);
                if (const auto* match = ReusableForLoadout(next, kind, data,
                        item.value("profession", std::string()))) {
                    created.refs[kind] = match->id;
                    continue;
                }
                next.components[kind].push_back({
                    componentId, UniqueComponentName(next, kind, SuggestComponentName(kind, data, lookup)), data
                });
                created.refs[kind] = componentId;
            }
            order.push_back(std::move(created));
            continue;
        }
        Loadout updated = *existing;
        updated.metadata = Metadata(item);
        const auto parts = Split(item);
        if(!updated.equipment_set_id.empty()) {
            const auto oldParts=Split(previous);
            for(const auto& [kind,data]:parts)if(SetOwns(next,updated,kind)&&oldParts.at(kind).dump()!=data.dump())
                return Fail(error,"Detach the linked equipment set before editing equipment.");
            for(const auto& [kind,ref]:EffectiveRefs(next,updated))if(IsEquipmentKind(kind)) {
                const auto* part=Find(next,kind,ref);
                if(part)for(auto it=part->data.begin();it!=part->data.end();++it)
                    if(it.key()!="gear")updated.metadata.erase(it.key());
            }
        }
        for (const auto& [kind, data] : parts) {
            if(SetOwns(next,updated,kind))continue;
            const auto oldRef = updated.refs.find(kind);
            if (oldRef == updated.refs.end()) {
                if (const auto* match = ReusableForLoadout(next, kind, data,
                        item.value("profession", std::string()))) {
                    updated.refs[kind] = match->id;
                } else {
                    std::string newId = kind + ":" + id;
                    unsigned sequence = 1;
                    while (Find(next,kind,newId))
                        newId = kind + ":" + id + ":" + std::to_string(sequence++);
                    next.components[kind].push_back({newId,
                        UniqueComponentName(next,kind,SuggestComponentName(kind,data,lookup)),data});
                    updated.refs[kind] = newId;
                }
                continue;
            }
            const std::string ref = oldRef->second;
            Component* previous = Find(next, kind, ref);
            if (!previous) return Fail(error, "Missing component during reconciliation.");
            if (previous->data == data) continue;
            if (const auto* match = ReusableForLoadout(next, kind, data,
                    item.value("profession", std::string()))) {
                updated.refs[kind] = match->id;
                continue;
            }

            unsigned users = 0;
            for (const auto& use : original.loadouts)
                if (use.refs.count(kind) && use.refs.at(kind) == ref) ++users;
            // A newly created Loadout earlier in this reconciliation may
            // already reference this component. Never mutate its snapshot.
            for (const auto& use : order)
                if (use.refs.count(kind) && use.refs.at(kind) == ref &&
                    std::none_of(original.loadouts.begin(), original.loadouts.end(),
                        [&](const Loadout& old) {
                            return old.id == use.id && old.refs.count(kind) &&
                                   old.refs.at(kind) == ref;
                        })) ++users;
            for(const auto& set:original.equipment_sets)
                if(set.refs.count(kind)&&set.refs.at(kind)==ref)++users;
            if (users > 1) {
                std::string privateId = kind + ":" + id;
                unsigned sequence = 1;
                while (Find(next, kind, privateId))
                    privateId = kind + ":" + id + ":" + std::to_string(sequence++);
                next.components[kind].push_back({
                    privateId, UniqueComponentName(next, kind, SuggestComponentName(kind, data, lookup)), data
                });
                updated.refs[kind] = privateId;
            } else previous->data = data;
        }
        order.push_back(std::move(updated));
    }
    next.loadouts = std::move(order); // User's existing order; leave orphan components.
    if (!Validate(next, error)) return false;
    Json assembled;
    if (!ToLegacy(next, assembled, error) || assembled.at("builds").size() != expected.size())
        return Fail(error, "Compatibility view round-trip mismatch.");
    for (size_t i = 0; i < expected.size(); ++i)
        if (assembled.at("builds")[i].dump() != expected[i].dump())
            return Fail(error, "Compatibility view round-trip mismatch.");
    output = std::move(next);
    return true;
}

// Selective backup import. Replace retains the full imported graph; Merge
// retains existing Loadout IDs, imports new ones, and remaps colliding component
// IDs so an imported item cannot overwrite a user's existing shared component.
inline bool Merge(const Document& current, const Document& incoming,
                  bool replace, Document& output,
                  size_t& added, size_t& skipped, std::string& error,
                  size_t* setsAdded=nullptr,size_t* setsSkipped=nullptr) {
    added = skipped = 0;
    if(setsAdded)*setsAdded=0;
    if(setsSkipped)*setsSkipped=0;
    if (!Validate(current, error) || !Validate(incoming, error)) return false;
    if (replace) {
        added = incoming.loadouts.size();
        if(setsAdded)*setsAdded=incoming.equipment_sets.size();
        output = incoming;
        return true;
    }
    Document merged = current;
    // Skill payloads do not carry a profession. A collision can be shared
    // only if both original graphs independently prove the same context.
    auto verifiedProfession=[](const Document& doc,const std::string& id) {
        std::string profession;
        for(const auto& l:doc.loadouts) {
            const auto ref=l.refs.find("skills");
            if(ref==l.refs.end()||ref->second!=id)continue;
            const auto p=ProfessionOf(doc,l);
            if(p.empty()||(!profession.empty()&&profession!=p))return std::string();
            profession=p;
        }
        return profession;
    };
    std::map<std::string, std::map<std::string,std::string>> remap;
    for (const auto& [kind, list] : incoming.components) {
        for (const auto& incomingPart : list) {
            std::string targetId = incomingPart.id;
            if (const Component* existing = Find(merged, kind, targetId)) {
                bool same=existing->name==incomingPart.name &&
                    existing->data.dump()==incomingPart.data.dump();
                if(same&&kind=="skills") {
                    const auto local=verifiedProfession(current,targetId);
                    const auto remote=verifiedProfession(incoming,incomingPart.id);
                    same=!local.empty()&&local==remote;
                }
                if (!same) {
                    unsigned n = 1;
                    targetId = incomingPart.id + ":import:" + std::to_string(n++);
                    // Reserve every original incoming ID, including those
                    // not processed yet. Distinct imported records must
                    // never collapse into an allocated collision suffix.
                    while (Find(merged, kind, targetId)||Find(incoming,kind,targetId))
                        targetId = incomingPart.id + ":import:" + std::to_string(n++);
                }
            }
            remap[kind][incomingPart.id] = targetId;
            if (!Find(merged, kind, targetId)) {
                Component copy = incomingPart;
                copy.id = targetId;
                merged.components[kind].push_back(std::move(copy));
            }
        }
    }
    std::map<std::string,std::string> setRemap;
    for(auto set:incoming.equipment_sets) {
        for(auto& [kind,id]:set.refs)id=remap.at(kind).at(id);
        const auto original=set.id;
        const auto* existing=FindEquipmentSet(merged,set.id);
        if(existing&&existing->name==set.name&&existing->refs==set.refs) {
            setRemap[original]=set.id;
            if(setsSkipped)++*setsSkipped;
            continue;
        }
        for(unsigned i=1;FindEquipmentSet(merged,set.id)|| (set.id!=original&&FindEquipmentSet(incoming,set.id));++i)
            set.id=original+":import:"+std::to_string(i);
        setRemap[original]=set.id;
        merged.equipment_sets.push_back(std::move(set));
        if(setsAdded)++*setsAdded;
    }
    for (const auto& incomingLoadout : incoming.loadouts) {
        if (FindLoadout(merged, incomingLoadout.id)) {++skipped;continue;}
        Loadout copy = incomingLoadout;
        for (auto& [kind, id] : copy.refs)id = remap.at(kind).at(id);
        if(!copy.equipment_set_id.empty())copy.equipment_set_id=setRemap.at(copy.equipment_set_id);
        merged.loadouts.push_back(std::move(copy));
        ++added;
    }
    if (!Validate(merged, error)) return false;
    output = std::move(merged);
    return true;
}

} } // namespace AlterEgo::Composable
