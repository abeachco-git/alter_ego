#pragma once
// Alter Ego v3 composable Loadout model.
//
// This is a pure, platform-independent data layer. Build templates, physical
// equipment, and functional upgrade requirements are different concerns.
// The existing SavedBuild representation remains a compatibility view.
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace AlterEgo { namespace Composable {

using Json = nlohmann::json;
constexpr int kVersion = 3;
constexpr const char* kFormat = "alter_ego_composable_loadouts";

inline const std::array<std::string, 9>& Kinds() {
    static const std::array<std::string, 9> values = {{
        "base_build", "skills", "weapons", "armor", "trinkets",
        "runes", "sigils", "infusions", "relic"
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
};
struct Document {
    std::map<std::string, std::vector<Component>> components;
    std::vector<Loadout> loadouts;
};

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
        for (const auto& [kind, id] : item.refs) {
            if (!IsKind(kind) || id.empty() || !Find(doc, kind, id))
                return Fail(error, "Unresolved Loadout component: " + kind);
        }
        if (!item.refs.count("base_build") || !item.refs.count("skills"))
            return Fail(error, "Loadout lacks a Build or skill-selection component.");
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
        const std::string displayName = old.value("name", std::string("Unnamed"));
        const auto parts = Split(old);
        for (const auto& [kind, data] : parts) {
            Component part;
            part.id = kind + ":" + loadout.id;
            part.name = displayName + " / " + kind;
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
    for (const auto& [kind, id] : loadout.refs) {
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
            {"fashion_id", item.fashion_id}
        });
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
            next.loadouts.push_back(std::move(item));
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
    kind = parsedKind;
    component = {"", input.at("name").get<std::string>(), data};
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
inline bool Assign(Document& doc, const std::string& loadoutId,
                   const std::string& kind, const std::string& id,
                   std::string& error) {
    return Transaction(doc, [&](Document& next, std::string& err) {
        auto* item = FindLoadout(next, loadoutId);
        if (!item || !IsKind(kind) || !Find(next, kind, id))
            return Fail(err, "Loadout or component not found.");
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
        copied.name = name;
        next.components[kind].push_back(std::move(copied));
        item->refs[kind] = id;
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
                      Document& output, std::string& error) {
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
                next.components[kind].push_back({
                    componentId, item.value("name", std::string("Unnamed")) + " / " + kind,
                    data
                });
                created.refs[kind] = componentId;
            }
            order.push_back(std::move(created));
            continue;
        }
        Loadout updated = *existing;
        updated.metadata = Metadata(item);
        const auto parts = Split(item);
        for (const auto& [kind, data] : parts) {
            const std::string ref = updated.refs.at(kind);
            Component* previous = Find(next, kind, ref);
            if (!previous) return Fail(error, "Missing component during reconciliation.");
            if (previous->data == data) continue;

            unsigned users = 0;
            for (const auto& use : original.loadouts)
                if (use.refs.count(kind) && use.refs.at(kind) == ref) ++users;
            if (users > 1) {
                std::string privateId = kind + ":" + id;
                unsigned sequence = 1;
                while (Find(next, kind, privateId))
                    privateId = kind + ":" + id + ":" + std::to_string(sequence++);
                next.components[kind].push_back({
                    privateId, previous->name + " (private)", data
                });
                updated.refs[kind] = privateId;
            } else previous->data = data;
        }
        order.push_back(std::move(updated));
    }
    next.loadouts = std::move(order); // User's existing order; leave orphan components.
    if (!Validate(next, error)) return false;
    Json assembled;
    if (!ToLegacy(next, assembled, error) ||
        assembled.at("builds") != expected)
        return Fail(error, "Compatibility view round-trip mismatch.");
    output = std::move(next);
    return true;
}

// Selective backup import. Replace retains the full imported graph; Merge
// retains existing Loadout IDs, imports new ones, and remaps colliding component
// IDs so an imported item cannot overwrite a user's existing shared component.
inline bool Merge(const Document& current, const Document& incoming,
                  bool replace, Document& output,
                  size_t& added, size_t& skipped, std::string& error) {
    added = skipped = 0;
    if (!Validate(current, error) || !Validate(incoming, error)) return false;
    if (replace) {
        added = incoming.loadouts.size();
        output = incoming;
        return true;
    }
    Document merged = current;
    std::map<std::string, std::map<std::string,std::string>> remap;
    for (const auto& [kind, list] : incoming.components) {
        for (const auto& incomingPart : list) {
            std::string targetId = incomingPart.id;
            if (const Component* existing = Find(merged, kind, targetId)) {
                if (existing->data != incomingPart.data) {
                    unsigned n = 1;
                    targetId = incomingPart.id + ":import:" + std::to_string(n++);
                    while (Find(merged, kind, targetId))
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
    for (const auto& incomingLoadout : incoming.loadouts) {
        if (FindLoadout(merged, incomingLoadout.id)) {
            ++skipped;
            continue;
        }
        Loadout copy = incomingLoadout;
        for (auto& [kind, id] : copy.refs)
            id = remap.at(kind).at(id);
        merged.loadouts.push_back(std::move(copy));
        ++added;
    }
    if (!Validate(merged, error)) return false;
    output = std::move(merged);
    return true;
}

} } // namespace AlterEgo::Composable
