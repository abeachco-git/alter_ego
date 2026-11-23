#pragma once
// Native, lossless v3 component editing controls.
// This header is included *inside* ComposableUI.h's anonymous namespace.
// Editing a field changes only the draft object. All other fields (including
// unknown future extensions) are retained verbatim for a safe round trip.
#include <climits>
#include <cstdint>

static void ComponentNativeNumber(ComponentJson& data, const char* key,
                                  const char* label, int maximum = INT_MAX) {
    int number = 0;
    auto it = data.find(key);
    if (it != data.end() && it->is_number_integer())
        number = static_cast<int>(std::min<int64_t>(
            std::max<int64_t>(0, it->get<int64_t>()), maximum));
    ImGui::SetNextItemWidth(150.0f);
    if (ImGui::InputInt(label, &number))
        data[key] = std::max(0, std::min(number, maximum));
}

static void ComponentNativeText(ComponentJson& data, const char* key,
                                const char* label) {
    const auto it = data.find(key);
    std::string current = it != data.end() && it->is_string()
        ? it->get<std::string>() : std::string();
    ImGui::SetNextItemWidth(ImMax(160.0f, ImGui::GetContentRegionAvail().x * 0.75f));
    if (InputTextString(label, current)) data[key] = current;
}

static std::string ComponentNativeProfession(const std::string& kind,
                                              const std::string& id,
                                              const ComponentJson& data) {
    if (kind == "base_build") return data.value("profession", std::string());
    const auto& doc = ComponentUISnapshot();
    std::string profession;
    if (!doc.contains("loadouts") || !doc["loadouts"].is_array()) return {};
    for (const auto& loadout : doc["loadouts"]) {
        if (!loadout.is_object() || !loadout.contains("refs") ||
            !loadout["refs"].is_object()) continue;
        const auto& refs = loadout["refs"];
        if (refs.value(kind, std::string()) != id) continue;
        const auto* base = ComponentUIFind(doc, "base_build",
                                            refs.value("base_build", std::string()));
        if (!base || !base->contains("data")) continue;
        const auto name = base->at("data").value("profession", std::string());
        if (profession.empty()) profession = name;
        else if (profession != name) return {}; // ambiguous shared skill bar
    }
    return profession;
}

static std::string ComponentNativeSkillName(uint32_t id) {
    if (!id) return "(empty)";
    const auto* skill = AlterEgo::GW2API::GetSkillInfo(id);
    return skill ? skill->name : "#" + std::to_string(id);
}
static std::string ComponentNativeSpecName(uint32_t id) {
    if (!id) return "(empty)";
    const auto* spec = AlterEgo::GW2API::GetSpecInfo(id);
    return spec ? spec->name : "#" + std::to_string(id);
}

static void ComponentNativeSkill(ComponentJson& parent, const char* key,
                                 const char* label,
                                 const std::string& profession) {
    ImGui::PushID(key);
    const uint32_t id = parent.value(key, uint32_t(0));
    ComponentNativeNumber(parent, key, label);
    if (id) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", ComponentNativeSkillName(id).c_str());
    }
    if (!profession.empty()) {
        const auto choices = AlterEgo::GW2API::GetProfessionSkillIds(profession);
        ImGui::SameLine();
        if (ImGui::BeginCombo("##skill_options", "Choose...")) {
            if (ImGui::Selectable("(empty)")) parent[key] = 0;
            for (uint32_t choice : choices) {
                const std::string shown = ComponentNativeSkillName(choice) +
                    " (#" + std::to_string(choice) + ")";
                if (ImGui::Selectable(shown.c_str(), choice == id)) parent[key] = choice;
            }
            ImGui::EndCombo();
        }
    }
    ImGui::PopID();
}

static void ComponentNativeSkillBar(ComponentJson& data, const char* key,
                                    const char* title,
                                    const std::string& profession) {
    if (!ImGui::TreeNodeEx(key, ImGuiTreeNodeFlags_DefaultOpen, "%s", title)) return;
    // Do not fill an absent skill bar merely because the editor was opened.
    if (!data.contains(key)) {
        if (RenderActionButton((std::string("Add ") + title + "##bar").c_str()))
            data[key] = {{"heal", 0},
                         {"utilities", ComponentJson::array({0, 0, 0})},
                         {"elite", 0}};
        ImGui::TreePop();
        return;
    }
    if (!data[key].is_object()) {
        ImGui::TextDisabled("Unsupported skill format; existing value preserved.");
        ImGui::TreePop();
        return;
    }
    auto& bar = data[key];
    ComponentNativeSkill(bar, "heal", "Heal", profession);
    ComponentJson utilities = bar.contains("utilities") &&
        bar["utilities"].is_array() ? bar["utilities"] : ComponentJson::array();
    while (utilities.size() < 3) utilities.push_back(0);
    for (int i = 0; i < 3; ++i) {
        ImGui::PushID(i);
        ComponentJson field = {{"skill", utilities[i]}};
        ComponentNativeSkill(field, "skill",
            (std::string("Utility ") + std::to_string(i + 1)).c_str(), profession);
        if (field["skill"] != utilities[i]) {
            utilities[i] = field["skill"];
            bar["utilities"] = utilities;
        }
        ImGui::PopID();
    }
    ComponentNativeSkill(bar, "elite", "Elite", profession);
    ImGui::TreePop();
}

static void ComponentNativeSpecializations(ComponentJson& data,
                                             const std::string& profession) {
    if (!ImGui::TreeNodeEx("##component_specs", ImGuiTreeNodeFlags_DefaultOpen,
                            "Specializations and traits")) return;
    if (!data.contains("specializations")) {
        if (RenderActionButton("Add specialization lines##component"))
            data["specializations"] = ComponentJson::array();
    }
    if (!data.contains("specializations")) {
        ImGui::TreePop();
        return;
    }
    if (!data["specializations"].is_array()) {
        ImGui::TextDisabled("Unrecognized specialization format; preserved.");
        ImGui::TreePop();
        return;
    }
    auto& specs = data["specializations"];
    const auto candidates = profession.empty()
        ? std::vector<uint32_t>() : AlterEgo::GW2API::GetProfessionSpecIds(profession);
    const int count = std::min<int>(3, static_cast<int>(specs.size()));
    for (int i = 0; i < count; ++i) {
        ImGui::PushID(i);
        if (!specs[i].is_object()) {
            ImGui::TextDisabled("Unrecognized specialization line; preserved.");
            ImGui::PopID();
            continue;
        }
        auto& spec = specs[i];
        const uint32_t priorId = spec.value("id", uint32_t(0));
        ImGui::Separator();
        ImGui::Text("Line %d", i + 1);
        ComponentNativeNumber(spec, "id", "Specialization ID");
        if (!candidates.empty()) {
            ImGui::SameLine();
            if (ImGui::BeginCombo("##specialization", ComponentNativeSpecName(priorId).c_str())) {
                if (ImGui::Selectable("(none)", !priorId)) spec["id"] = 0;
                for (uint32_t choice : candidates)
                    if (ImGui::Selectable((ComponentNativeSpecName(choice) +
                        "##" + std::to_string(choice)).c_str(), choice == priorId))
                        spec["id"] = choice;
                ImGui::EndCombo();
            }
        }
        const uint32_t specId = spec.value("id", uint32_t(0));
        if (specId != priorId) spec["traits"] = ComponentJson::array({0, 0, 0});
        ComponentJson traits = spec.contains("traits") && spec["traits"].is_array()
            ? spec["traits"] : ComponentJson::array();
        while (traits.size() < 3) traits.push_back(0);
        const auto* definition = AlterEgo::GW2API::GetSpecInfo(specId);
        const char* tiers[] = {"Adept", "Master", "Grandmaster"};
        for (int tier = 0; tier < 3; ++tier) {
            ImGui::PushID(tier);
            int choice = traits[tier].is_number_integer()
                ? traits[tier].get<int>() : 0;
            int entered = choice;
            ImGui::SetNextItemWidth(150);
            if (ImGui::InputInt(tiers[tier], &entered)) {
                traits[tier] = entered; // negative import placeholders are preserved
                spec["traits"] = traits;
            }
            if (definition && definition->major_traits.size() >= 9) {
                ImGui::SameLine();
                if (ImGui::BeginCombo("##trait", "Choose trait...")) {
                    if (ImGui::Selectable("(none)", choice == 0)) {
                        traits[tier] = 0;
                        spec["traits"] = traits;
                    }
                    for (int j = 0; j < 3; ++j) {
                        const uint32_t tid = definition->major_traits[tier * 3 + j];
                        const auto* trait = AlterEgo::GW2API::GetTraitInfo(tid);
                        const std::string label = (trait ? trait->name :
                            "#" + std::to_string(tid)) + "##" + std::to_string(tid);
                        if (ImGui::Selectable(label.c_str(), choice == static_cast<int>(tid))) {
                            traits[tier] = tid;
                            spec["traits"] = traits;
                        }
                    }
                    ImGui::EndCombo();
                }
            }
            ImGui::PopID();
        }
        ImGui::PopID();
    }
    if (specs.size() < 3 && RenderActionButton("Add specialization line##component"))
        specs.push_back({{"id", 0}, {"traits", ComponentJson::array({0,0,0})}});
    if (candidates.empty() && !profession.empty()) {
        ImGui::TextDisabled("Specialization metadata has not loaded.");
        if (RenderActionButton("Fetch profession data##component"))
            AlterEgo::GW2API::FetchProfessionInfoAsync(profession);
    }
    ImGui::TreePop();
}

static void ComponentNativeIntegerArray(ComponentJson& data, const char* key,
                                        const char* label, size_t length) {
    if (!ImGui::TreeNode(key, "%s", label)) return;
    if (!data.contains(key)) {
        if (RenderActionButton((std::string("Add ") + label + "##array").c_str()))
            data[key] = ComponentJson::array();
        ImGui::TreePop();
        return;
    }
    if (!data[key].is_array()) {
        ImGui::TextDisabled("Unrecognized array format; preserved.");
        ImGui::TreePop();
        return;
    }
    auto& values = data[key];
    for (size_t i = 0; i < length; ++i) {
        ImGui::PushID(static_cast<int>(i));
        const ComponentJson original = i < values.size()
            ? values[i] : ComponentJson(0);
        ComponentJson one = {{"value", original}};
        ComponentNativeNumber(one, "value", ("Entry " + std::to_string(i+1)).c_str());
        if (one["value"] != original) {
            while (values.size() <= i) values.push_back(0);
            values[i] = one["value"];
        }
        ImGui::PopID();
    }
    ImGui::TreePop();
}

// Physical gear stays per slot. Upgrade-only components own only their
// upgrades; base equipment components own item IDs, stats and weapon types.
static const char* kNativeGearSlots[] = {
    "Helm","Shoulders","Coat","Gloves","Leggings","Boots",
    "WeaponA1","WeaponA2","WeaponB1","WeaponB2",
    "AquaticA","AquaticB",
    "Backpack","Accessory1","Accessory2","Amulet","Ring1","Ring2","Relic"
};

static bool ComponentNativeSlotAllowed(const std::string& kind,
                                       const std::string& slot) {
    if (kind == "runes") return AlterEgo::Composable::SlotKind(slot) == "armor";
    if (kind == "sigils") return AlterEgo::Composable::SlotKind(slot) == "weapons";
    if (kind == "infusions") return true;
    return AlterEgo::Composable::SlotKind(slot) == kind;
}

static void ComponentNativeInfusionSlots(ComponentJson& part) {
    // Optional infusion arrays must stay absent until an actual edit.
    ComponentJson ids = part.contains("infusion_ids") &&
        part["infusion_ids"].is_array() ? part["infusion_ids"] : ComponentJson::array();
    ComponentJson attrs = part.contains("infusion_attributes") &&
        part["infusion_attributes"].is_array() ? part["infusion_attributes"] : ComponentJson::array();
    while (attrs.size() < ids.size()) attrs.push_back("");
    int remove = -1;
    for (size_t i = 0; i < ids.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        ComponentJson value = {{"id", ids[i]}};
        ComponentNativeNumber(value, "id", "Infusion item ID");
        if (value["id"] != ids[i]) {
            ids[i] = value["id"];
            part["infusion_ids"] = ids;
        }
        ImGui::SameLine();
        if (RenderActionButton("Remove##infusion")) remove = static_cast<int>(i);
        ComponentJson attribute = {{"name", attrs[i]}};
        ComponentNativeText(attribute, "name", "Attribute");
        if (attribute["name"] != attrs[i]) {
            attrs[i] = attribute["name"];
            part["infusion_attributes"] = attrs;
        }
        ImGui::PopID();
    }
    if (remove >= 0) {
        ids.erase(ids.begin() + remove);
        attrs.erase(attrs.begin() + remove);
        part["infusion_ids"] = ids;
        if (part.contains("infusion_attributes") || !attrs.empty())
            part["infusion_attributes"] = attrs;
    }
    if (RenderActionButton("Add infusion##component")) {
        ids.push_back(0);
        attrs.push_back("");
        part["infusion_ids"] = ids;
        part["infusion_attributes"] = attrs;
    }
    bool snapshot = part.value("infusion_snapshot", false);
    if (ImGui::Checkbox("Recorded equipment snapshot", &snapshot))
        part["infusion_snapshot"] = snapshot;
}

static void ComponentNativeGear(ComponentJson& data, const std::string& kind) {
    ImGui::TextUnformatted("Equipment slots");
    ImGui::TextDisabled("Item and stat definitions use the same stored IDs as the Loadout viewer.");
    if (data.contains("gear") && !data["gear"].is_object()) {
        ImGui::TextDisabled("Unrecognized gear data; preserved without alteration.");
        return;
    }
    ComponentJson placeholder = ComponentJson::object();
    ComponentJson& gear = data.contains("gear") ? data["gear"] : placeholder;
    if (ImGui::BeginCombo("Add slot##component_gear", "Choose slot...")) {
        for (const char* slot : kNativeGearSlots) {
            if (!ComponentNativeSlotAllowed(kind, slot) || gear.contains(slot)) continue;
            if (ImGui::Selectable(slot)) {
                // Defer display of new slot to the next frame if this
                // component didn't already have a gear object.
                data["gear"][slot] = ComponentJson::object();
            }
        }
        ImGui::EndCombo();
    }
    std::string remove;
    for (auto it = gear.begin(); it != gear.end(); ++it) {
        const std::string slot = it.key();
        ImGui::PushID(slot.c_str());
        if (ImGui::TreeNodeEx("##slot", ImGuiTreeNodeFlags_DefaultOpen, "%s", slot.c_str())) {
            if (!it.value().is_object()) {
                ImGui::TextDisabled("Unrecognized slot representation; kept unchanged.");
            } else {
                auto& item = it.value();
                if (kind == "armor" || kind == "weapons" || kind == "trinkets") {
                    ComponentNativeNumber(item, "item_id", "Item ID");
                    const uint32_t id = item.value("item_id", uint32_t(0));
                    if (const auto* info = AlterEgo::GW2API::GetItemInfo(id))
                        ImGui::TextDisabled("%s", info->name.c_str());
                    ComponentNativeNumber(item, "stat_id", "Stat ID");
                    ComponentNativeText(item, "stat_name", "Stat name");
                    if (kind == "weapons")
                        ComponentNativeText(item, "weapon_type", "Weapon type");
                } else if (kind == "runes") {
                    ComponentNativeNumber(item, "rune_id", "Rune ID");
                    ComponentNativeText(item, "rune", "Rune name");
                } else if (kind == "sigils") {
                    ComponentNativeNumber(item, "sigil_id", "Sigil 1 ID");
                    ComponentNativeText(item, "sigil", "Sigil 1 name");
                    ComponentNativeNumber(item, "sigil2_id", "Sigil 2 ID");
                    ComponentNativeText(item, "sigil2", "Sigil 2 name");
                } else if (kind == "infusions") {
                    ComponentNativeInfusionSlots(item);
                    ComponentNativeText(item, "infusion", "Legacy infusion label");
                }
                if (RenderActionButton("Remove slot##component"))
                    remove = slot;
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    if (!remove.empty()) data["gear"].erase(remove);
}

static void ComponentNativeInfusionRequirements(ComponentJson& data) {
    if (!ImGui::TreeNodeEx("##infusion_reqs", ImGuiTreeNodeFlags_DefaultOpen,
                            "Aggregate infusion requirements")) return;
    if (data.contains("infusion_totals") && !data["infusion_totals"].is_array()) {
        ImGui::TextDisabled("Unknown aggregate format; preserved.");
    } else {
        ComponentJson none = ComponentJson::array();
        ComponentJson& totals = data.contains("infusion_totals")
            ? data["infusion_totals"] : none;
        int remove = -1;
        for (size_t i = 0; i < totals.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            if (!totals[i].is_object()) {
                ImGui::TextDisabled("Unrecognized requirement; preserved.");
                ImGui::PopID();
                continue;
            }
            ComponentNativeNumber(totals[i], "item_id", "Item ID");
            ComponentNativeNumber(totals[i], "count", "Quantity", 25);
            ComponentNativeText(totals[i], "attribute", "Attribute");
            if (RenderActionButton("Remove requirement##native"))
                remove = static_cast<int>(i);
            ImGui::Separator();
            ImGui::PopID();
        }
        if (remove >= 0) data["infusion_totals"].erase(remove);
        if (RenderActionButton("Add requirement##native"))
            data["infusion_totals"].push_back(
                {{"item_id", 0}, {"count", 1}, {"attribute", ""}});
    }
    bool present = data.value("infusion_summary_present", false);
    if (ImGui::Checkbox("Saved aggregate totals available", &present))
        data["infusion_summary_present"] = present;
    if (present) {
        ComponentNativeNumber(data, "infusion_ar_total", "Agony resistance");
        if (data.contains("infusion_stat_totals") &&
            data["infusion_stat_totals"].is_object()) {
            for (auto it = data["infusion_stat_totals"].begin();
                 it != data["infusion_stat_totals"].end(); ++it) {
                ImGui::PushID(it.key().c_str());
                ComponentJson value = {{"amount", it.value()}};
                ComponentNativeNumber(value, "amount", it.key().c_str());
                if (value["amount"] != it.value()) it.value() = value["amount"];
                ImGui::PopID();
            }
        }
    }
    ImGui::TreePop();
}

static void RenderComponentNativeEditor(const std::string& kind,
                                        const std::string& id,
                                        ComponentJson& data) {
    if (!data.is_object()) data = ComponentJson::object();
    std::string profession = ComponentNativeProfession(kind, id, data);
    if (kind == "skills") {
        if (profession.empty()) {
            // New/unattached Skills have no profession ownership encoded.
            // This is intentionally a UI-only filter, never a stored root field.
            static const char* professions[] = {
                "Elementalist","Engineer","Guardian","Mesmer","Necromancer",
                "Ranger","Revenant","Thief","Warrior"
            };
            ImGui::TextDisabled("Skill picker profession (UI-only; not saved)");
            ImGui::SetNextItemWidth(210);
            const char* preview = g_ComponentSkillProfessionHint.empty()
                ? "Choose profession..." : g_ComponentSkillProfessionHint.c_str();
            if (ImGui::BeginCombo("##skill_profession_hint", preview)) {
                for (const char* candidate : professions)
                    if (ImGui::Selectable(candidate,
                            g_ComponentSkillProfessionHint == candidate))
                        g_ComponentSkillProfessionHint = candidate;
                ImGui::EndCombo();
            }
            profession = g_ComponentSkillProfessionHint;
        } else {
            ImGui::TextDisabled("Skill profession: %s (inferred from Loadout)",
                                profession.c_str());
        }
        if (!profession.empty() &&
            AlterEgo::GW2API::GetProfessionSkillIds(profession).empty()) {
            ImGui::SameLine();
            if (RenderActionButton("Fetch skill options##component"))
                AlterEgo::GW2API::FetchProfessionPaletteAsync(profession);
        }
    }
    if (kind == "base_build") {
        ComponentNativeText(data, "profession", "Profession");
        ComponentNativeSpecializations(data, data.value("profession", std::string()));
        ComponentNativeIntegerArray(data, "pets", "Ranger pets (terrestrial, aquatic)", 4);
        ComponentNativeIntegerArray(data, "legend_codes", "Revenant legends", 4);
        if (ImGui::TreeNode("##weapon_proficiency", "Build template weapon IDs")) {
            if (data.contains("weapons") && !data["weapons"].is_array()) {
                ImGui::TextDisabled("Unrecognized weapon definition; preserved.");
                ImGui::TreePop();
            } else {
            ComponentJson none = ComponentJson::array();
            auto& weapons = data.contains("weapons") ? data["weapons"] : none;
            int remove = -1;
            for (size_t i = 0; i < weapons.size(); ++i) {
                ImGui::PushID(static_cast<int>(i));
                ComponentJson temp = {{"id", weapons[i]}};
                ComponentNativeNumber(temp, "id", "Weapon ID");
                if (weapons[i] != temp["id"])
                    data["weapons"][i] = temp["id"];
                ImGui::SameLine();
                if (RenderActionButton("Remove##template_weapon"))
                    remove = static_cast<int>(i);
                ImGui::PopID();
            }
            if (remove >= 0) data["weapons"].erase(remove);
            if (RenderActionButton("Add weapon ID##template"))
                data["weapons"].push_back(0);
            ImGui::TreePop();
            }
        }
    } else if (kind == "skills") {
        if (profession.empty())
            ImGui::TextDisabled("Assign to a Loadout to enable profession-filtered skill choices.");
        ComponentNativeSkillBar(data, "skills", "Terrestrial skills", profession);
        ComponentNativeSkillBar(data, "aquatic_skills", "Aquatic skills", profession);
    } else if (kind == "relic") {
        ComponentNativeNumber(data, "relic_id", "Relic item ID");
        ComponentNativeText(data, "relic_name", "Relic name");
    } else {
        if (kind == "runes") {
            ComponentNativeNumber(data, "rune_id", "Shared rune ID");
            ComponentNativeText(data, "rune_name", "Shared rune name");
            ImGui::TextDisabled("An unspecified shared rune count is unverified in Compare.");
        }
        if (kind == "infusions") ComponentNativeInfusionRequirements(data);
        ComponentNativeGear(data, kind);
    }
    if (ImGui::CollapsingHeader("Advanced: exact stored fields (read only)")) {
        ImGui::BeginChild("##component_native_json", ImVec2(0, 130), true);
        ImGui::TextUnformatted(data.dump(2).c_str());
        ImGui::EndChild();
    }
}
