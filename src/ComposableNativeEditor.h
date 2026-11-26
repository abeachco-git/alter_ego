#pragma once
// Native, lossless v3 component editing controls.
// This header is included *inside* ComposableUI.h's anonymous namespace.
// Editing a field changes only the draft object. All other fields (including
// unknown future extensions) are retained verbatim for a safe round trip.
#include <climits>
#include <cstdint>

// Slot picker state is scoped to the active component draft and reset whenever
// an editor opens. It never lives in canonical v3 component data.
static std::string g_ComponentGearSelectedSlot;
static std::string g_ComponentGearRemovalPending;

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
    if (AlterEgo::UI::InputText(label, current)) data[key] = current;
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
        if (g_ComponentEditorMode == "copy_for_loadout" &&
            loadout.value("id", std::string()) != g_ComponentEditorPrivateLoadoutId)
            continue;
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
    const auto skill = AlterEgo::GW2API::GetSkillInfo(id);
    return skill ? skill->name : "#" + std::to_string(id);
}
static std::string ComponentNativeSpecName(uint32_t id) {
    if (!id) return "(empty)";
    const auto spec = AlterEgo::GW2API::GetSpecInfo(id);
    return spec ? spec->name : "#" + std::to_string(id);
}

// Determine which elite specs *all* referencing Loadouts have equipped.
// An unassigned Skills component has no known spec context, so its editor
// explicitly offers all profession skills. Shared components use the
// intersection to prevent suggesting an elite-only skill that another
// referencing Loadout cannot legally slot.
static std::pair<std::vector<uint32_t>, bool> ComponentNativeSkillSpecs(
    const std::string& componentId) {
    std::vector<uint32_t> common;
    bool found = false;
    const auto& doc = ComponentUISnapshot();
    if (!doc.contains("loadouts") || !doc["loadouts"].is_array())
        return {common, found};
    for (const auto& loadout : doc["loadouts"]) {
        if (!loadout.is_object() || !loadout.contains("refs") ||
            !loadout["refs"].is_object()) continue;
        if (g_ComponentEditorMode == "copy_for_loadout" &&
            loadout.value("id", std::string()) != g_ComponentEditorPrivateLoadoutId)
            continue;
        const auto& refs = loadout["refs"];
        if (refs.value("skills", std::string()) != componentId) continue;
        const auto* base = ComponentUIFind(doc, "base_build",
            refs.value("base_build", std::string()));
        std::vector<uint32_t> ids;
        if (base && base->contains("data") &&
            (*base)["data"].contains("specializations") &&
            (*base)["data"]["specializations"].is_array()) {
            for (const auto& spec : (*base)["data"]["specializations"])
                if (spec.is_object() && spec.value("id", uint32_t(0)))
                    ids.push_back(spec.value("id", uint32_t(0)));
        }
        if (!found) {
            common = std::move(ids);
            found = true;
        } else {
            common.erase(std::remove_if(common.begin(), common.end(),
                [&](uint32_t value) {
                    return std::find(ids.begin(), ids.end(), value) == ids.end();
                }), common.end());
        }
    }
    return {common, found};
}

static bool ComponentNativeSkill(ComponentJson& parent,const char* key,const char* label,
        const char* role,const std::string& profession,const std::vector<uint32_t>& specs,bool allowAnySpec){
    ImGui::PushID(key);
    const uint32_t id=parent.contains(key)&&parent[key].is_number_integer()?parent[key].get<uint32_t>():0;
    bool changed=false;
    ImGui::AlignTextToFramePadding();ImGui::TextUnformatted(label);ImGui::SameLine(140.0f);
    if(RenderEditableSkillIcon(id,38.0f,false))ImGui::OpenPopup("##component_skill_palette");
    auto apply=[&](uint32_t next){if(next!=id){parent[key]=next;changed=true;}};
    if(ImGui::BeginPopupContextItem("##component_skill_id")){
        const auto skill=AlterEgo::GW2API::GetSkillInfo(id);
        if(skill)CopyNameMenuItem(skill->name);
        if(ImGui::MenuItem("Clear Skill",nullptr,false,id!=0))apply(0);
        ImGui::TextDisabled("Advanced: exact skill ID");
        ComponentNativeNumber(parent,key,"Skill ID");
        if(parent.contains(key)&&parent[key].is_number_integer()&&parent[key].get<uint32_t>()!=id)changed=true;
        ImGui::EndPopup();
    }
    uint32_t next=id;
    if(RenderSkillPickerPopup("##component_skill_palette",profession,role,specs,allowAnySpec,id,next))apply(next);
    ImGui::SameLine();ImGui::TextDisabled("%s",ComponentNativeSkillName(id).c_str());
    ImGui::PopID();return changed;
}

static void ComponentNativeSkillBar(ComponentJson& data, const char* key,
                                    const char* title,
                                    const std::string& profession,
                                    const std::vector<uint32_t>& specs,
                                    bool allowAnySpec) {
    if (!ImGui::TreeNodeEx(key, ImGuiTreeNodeFlags_DefaultOpen, "%s", title)) return;
    // Missing bars are optional and must not be created by inspecting the editor.
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
    ComponentNativeSkill(bar, "heal", "Heal", "Heal",
                         profession, specs, allowAnySpec);
    ComponentJson utilities = bar.contains("utilities") &&
        bar["utilities"].is_array() ? bar["utilities"] : ComponentJson::array();
    while (utilities.size() < 3) utilities.push_back(0);
    for (int i = 0; i < 3; ++i) {
        ImGui::PushID(i);
        ComponentJson field = {{"skill", utilities[i]}};
        if (ComponentNativeSkill(field, "skill",
                (std::string("Utility ") + std::to_string(i+1)).c_str(),
                "Utility", profession, specs, allowAnySpec)) {
            if(AlterEgo::SkillEdit::SetUtility(utilities,i,field["skill"].get<uint32_t>()))
                bar["utilities"]=utilities;
        }
        ImGui::PopID();
    }
    ComponentNativeSkill(bar, "elite", "Elite", "Elite",
                         profession, specs, allowAnySpec);
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
        if(!profession.empty()){
            ImGui::SameLine();
            if(AlterEgo::UI::BeginCombo("##specialization",ComponentNativeSpecName(priorId).c_str())){
                std::array<uint32_t,3> ids{};
                for(int j=0;j<count;++j)if(specs[j].is_object())ids[j]=specs[j].value("id",uint32_t(0));
                uint32_t next=ids[i];
                if(RenderSpecializationChoices(profession,ids,i,next)&&next!=ids[i]){
                    spec["id"]=next;
                    auto info=AlterEgo::GW2API::GetSpecInfo(next);
                    if(info&&info->elite)for(int j=0;j<count;++j)if(j!=i&&specs[j].is_object()){
                        auto other=AlterEgo::GW2API::GetSpecInfo(ids[j]);
                        if(other&&other->elite){specs[j]["id"]=0;specs[j]["traits"]=ComponentJson::array({0,0,0});}
                    }
                }
                ImGui::EndCombo();
            }
        }
        const uint32_t specId = spec.value("id", uint32_t(0));
        if (specId != priorId) spec["traits"] = ComponentJson::array({0, 0, 0});
        ComponentJson traits = spec.contains("traits") && spec["traits"].is_array()
            ? spec["traits"] : ComponentJson::array();
        while (traits.size() < 3) traits.push_back(0);
        const auto definition = AlterEgo::GW2API::GetSpecInfo(specId);
        const char* tiers[] = {"Adept", "Master", "Grandmaster"};
        for (int tier = 0; tier < 3; ++tier) {
            ImGui::PushID(tier);
            const int choice = traits[tier].is_number_integer()
                ? traits[tier].get<int>() : 0;
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(tiers[tier]);
            ImGui::SameLine(140.0f);
            if (definition && definition->major_traits.size() >= 9) {
                // Same trait icons and selection treatment as the full
                // Loadout editor, not a separate text-only trait picker.
                if (definition->minor_traits.size() > static_cast<size_t>(tier)) {
                    RenderTraitIcon(definition->minor_traits[tier], true, true, 34);
                    ImGui::SameLine(0, 8);
                }
                for (int option = 0; option < 3; ++option) {
                    if (option) ImGui::SameLine(0, 6);
                    const uint32_t tid = definition->major_traits[tier * 3 + option];
                    const bool selected = choice == static_cast<int>(tid) ||
                        (choice < 0 && -choice == option + 1);
                    bool clicked = false;
                    RenderTraitIcon(tid, selected, false, 34, true, &clicked);
                    if (clicked) {
                        traits[tier] = tid;
                        spec["traits"] = traits;
                    }
                }
            } else {
                ImGui::TextDisabled("Trait metadata pending");
            }
            ImGui::SameLine();
            if (AlterEgo::UI::SmallButton("Clear")) {
                traits[tier] = 0;
                spec["traits"] = traits;
            }
            if (ImGui::TreeNode("##advanced_trait", "Advanced trait ID")) {
                int entered = choice;
                ImGui::SetNextItemWidth(160);
                if (ImGui::InputInt("Trait ID", &entered)) {
                    traits[tier] = entered; // retain negative import placeholders
                    spec["traits"] = traits;
                }
                ImGui::TreePop();
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

static void ComponentNativeProfessionPair(ComponentJson& data,const char* key,
                                           const char* label,bool legend,bool eligible) {
    if(!eligible&&!data.contains(key))return;
    if(!ImGui::TreeNode(key,"%s",label))return;
    if(data.contains(key)&&!data[key].is_array()){
        ImGui::TextDisabled("Unrecognized array format; preserved.");ImGui::TreePop();return;
    }
    ComponentJson values=data.contains(key)?data[key]:ComponentJson::array();
    auto get=[&](size_t i)->uint32_t{
        if(i>=values.size()||!values[i].is_number_integer())return 0;
        auto n=values[i].get<int64_t>();return n>=0&&n<=UINT32_MAX?static_cast<uint32_t>(n):0;
    };
    auto set=[&](size_t i,uint32_t value){
        while(values.size()<=i)values.push_back(0);
        values[i]=value;data[key]=values;
    };
    for(int env=0;env<2;++env){
        ImGui::TextDisabled("%s",env?"Aquatic":"Terrestrial");
        for(int i=0;i<2;++i){
            size_t slot=env*2+i,other=env*2+1-i;
            if(i)ImGui::SameLine();ImGui::PushID(static_cast<int>(slot));
            uint32_t current=get(slot),next=current;
            if(eligible&&RenderProfessionPairChoice(legend,current,get(other),next)&&next!=current)
                set(slot,next);
            ImGui::PopID();
        }
    }
    if(ImGui::TreeNode("##advanced_ids","Advanced exact IDs")){
        for(size_t i=0;i<4;++i){
            ImGui::PushID(static_cast<int>(i));
            ComponentJson original=i<values.size()?values[i]:ComponentJson(0);
            ComponentJson one={{"value",original}};
            ComponentNativeNumber(one,"value",("Entry "+std::to_string(i+1)).c_str(),legend?255:INT_MAX);
            if(one["value"]!=original){while(values.size()<=i)values.push_back(0);values[i]=one["value"];data[key]=values;}
            ImGui::PopID();
        }
        ImGui::TreePop();
    }
    ImGui::TreePop();
}

// Physical gear stays per slot. Upgrade-only components own only their
// upgrades; base equipment components own item IDs, stats and weapon types.
static const char* kNativeGearSlots[] = {
    "Helm","Shoulders","Coat","Gloves","Leggings","Boots",
    "WeaponA1","WeaponA2","WeaponB1","WeaponB2",
    "Backpack","Accessory1","Accessory2","Amulet","Ring1","Ring2","Relic",
    "HelmAquatic","WeaponAquaticA","WeaponAquaticB"
};

static bool ComponentNativeSlotAllowed(const std::string& kind,
                                       const std::string& slot) {
    if (kind == "runes") return AlterEgo::Composable::SlotKind(slot) == "armor";
    if (kind == "sigils") return AlterEgo::Composable::SlotKind(slot) == "weapons";
    if (kind == "infusions") return !AlterEgo::EquipmentChoice::IsAquaticGearSlot(slot)&&slot!="Amulet"&&slot!="Relic";
    return AlterEgo::Composable::SlotKind(slot) == kind;
}

static void ComponentNativeInfusionSlots(ComponentJson& part,const std::string& slot) {
    // Preserve unfamiliar imported formats instead of converting them merely
    // because the user opened the graphical editor.
    if ((part.contains("infusion_ids") && !part["infusion_ids"].is_array()) ||
        (part.contains("infusion_attributes") &&
         !part["infusion_attributes"].is_array())) {
        ImGui::TextDisabled("Unsupported infusion array format; preserved.");
        return;
    }
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
        part["infusion_snapshot"]=true;part.erase("infusion");
    }
    // A standalone upgrade component may not know the weapon or exact item.
    // Enforce the physical upper bound; keep imported over-capacity values
    // visible and do not trim them merely by opening the editor.
    int capacity=AlterEgo::GearEdit::InfusionCapacity(slot,true);
    const auto exact=part.value("item_id",uint32_t(0));
    if(exact){
        const auto info=AlterEgo::GW2API::GetItemInfo(exact);
        if(info&&info->details.is_object()&&info->details.contains("infusion_slots")&&
           info->details["infusion_slots"].is_array())
            capacity=std::min(capacity,static_cast<int>(info->details["infusion_slots"].size()));
    }
    if(static_cast<int>(ids.size())>capacity)ImGui::TextDisabled("Imported requirements exceed known capacity; preserved.");
    if (static_cast<int>(ids.size())<capacity && RenderActionButton("Add infusion##component")) {
        ids.push_back(0);
        attrs.push_back("");
        part["infusion_ids"] = ids;
        part["infusion_attributes"] = attrs;
        part["infusion_snapshot"]=true;part.erase("infusion");
    }
    bool snapshot = part.value("infusion_snapshot", false);
    if (ImGui::Checkbox("Recorded equipment snapshot", &snapshot))
        part["infusion_snapshot"] = snapshot;
}

// Reuse the same database-backed upgrade catalogs as the traditional
// customization panel. The text/ID pair is updated together; unfamiliar
// imported items remain editable via the exact-ID fallback fields.
static std::string g_ComponentCatalogSearch;
static std::string g_ComponentStatSearch;
static void ComponentNativeCatalogChoice(ComponentJson& item,
                                          const char* idField,
                                          const char* nameField,
                                          const char* family) {
    ImGui::PushID(idField);
    const std::string selected = item.value(nameField, std::string());
    const auto storedId = item.find(idField);
    const uint32_t selectedId = storedId != item.end() && storedId->is_number_integer()
        ? storedId->get<uint32_t>() : 0;
    const std::string shown = !selected.empty() ? selected :
        (storedId != item.end() && storedId->is_number_integer() && *storedId != 0)
            ? ("#" + storedId->dump() + " (not in catalog)") : "(choose item)";
    if (AlterEgo::UI::BeginCombo("##catalog", shown.c_str(), ImGuiComboFlags_HeightLarge)) {
        AlterEgo::UI::SearchField("catalog", "Search items...", g_ComponentCatalogSearch);
        if (ImGui::Selectable("(none)")) {
            item.erase(idField);
            item.erase(nameField);
        }
        const char* kind=std::strcmp(family,"runes")==0?"Rune":std::strcmp(family,"sigils")==0?"Sigil":"Relic";
        RenderGearCatalogChoices(kind,g_ComponentCatalogSearch,selectedId,selected,false,
            [&](uint32_t id,const std::string& name){item[idField]=id;item[nameField]=name;g_ComponentCatalogSearch.clear();});
        ImGui::EndCombo();
    }
    if (ImGui::BeginPopupContextItem("##native_upgrade_context")) {
        const auto info=selectedId?AlterEgo::GW2API::GetItemInfo(selectedId):nullptr;
        CopyNameMenuItem(info&&!info->name.empty()?info->name:selected);
        if (ImGui::MenuItem("Clear")) { item.erase(idField); item.erase(nameField); }
        ImGui::EndPopup();
    }
    ImGui::PopID();
}

static void ComponentNativeStatChoice(ComponentJson& item) {
    ImGui::PushID("##native_stats");
    const uint32_t id = item.value("stat_id", uint32_t(0));
    const auto info = AlterEgo::GW2API::GetItemStatInfo(id);
    const std::string storedName = item.value("stat_name", std::string());
    const std::string shown = !storedName.empty() ? storedName :
        info ? info->name : id ? "#" + std::to_string(id) : "(choose stats)";
    if (AlterEgo::UI::BeginCombo("##stats", shown.c_str(), ImGuiComboFlags_HeightLarge)) {
        AlterEgo::UI::SearchField("stats", "Search stats...", g_ComponentStatSearch);
        if (ImGui::Selectable("(none)")) {
            item.erase("stat_id");
            item.erase("stat_name");
        }
        RenderGearStatChoices(g_ComponentStatSearch.c_str(),id,false,[&](uint32_t next,const std::string& name){
            item["stat_id"]=next;item["stat_name"]=name;g_ComponentStatSearch.clear();
        });
        ImGui::EndCombo();
    }
    ImGui::PopID();
}

static void ComponentNativeWeaponChoice(ComponentJson& item,
                                        const std::string& profession,
                                        const std::string& slot) {
    if (profession.empty()) {
        ImGui::TextDisabled("Assign to a Loadout for profession-filtered weapons.");
        return;
    }
    const auto weapons = AlterEgo::GW2API::GetProfessionWeapons(profession);
    if (!weapons || weapons->empty()) {
        ImGui::TextDisabled("Weapon definitions not available.");
        if (RenderActionButton("Fetch weapon data##component"))
            AlterEgo::GW2API::FetchProfessionPaletteAsync(profession);
        return;
    }
    const std::string current = item.value("weapon_type", std::string());
    if (AlterEgo::UI::BeginCombo("Weapon type##native_weapon", current.empty()
                         ? "(choose type)" : WeaponDisplayName(current))) {
        if (ImGui::Selectable("(none)", current.empty())&&!current.empty()) {
            item.erase("weapon_type");item.erase("item_id");item.erase("item_name");
        }
        for (const auto& [name, definition] : *weapons) {
            // Match the hand-eligibility policy of the original gear picker.
            if (!AlterEgo::EquipmentChoice::WeaponTypeEligible(
                    slot, name, definition.mainhand, definition.offhand,
                    definition.two_handed)) continue;
            if (ImGui::Selectable(WeaponDisplayName(name), name == current)&&name!=current){
                item["weapon_type"] = name;
                item.erase("item_id");item.erase("item_name");
            }
        }
        ImGui::EndCombo();
    }
}

// Same canonical slot order and readable details as Components > Inventory.
// Graphical choice controls below reuse the original Loadout's profession,
// stat, item, rune/sigil and relic metadata; all writes target only the draft.
// Read-only presentation action, shared by the editor and component viewer.
// Resolve exact stored names without mutating imported data or inventing IDs.
static void ComponentStoredNameActions(const ComponentJson& item){
    if(!item.is_object())return;
    const auto text=[&](const char* field){
        auto it=item.find(field);
        return it!=item.end()&&it->is_string()?it->get<std::string>():std::string{};
    };
    const auto name=[&](const char* field,const char* idField){
        auto id=item.find(idField);
        if(id!=item.end()&&id->is_number_integer()){
            const auto info=AlterEgo::GW2API::GetItemInfo(id->get<uint32_t>());
            if(info&&!info->name.empty())return info->name;
        }
        return text(field);
    };
    CopyNameMenuItem(name("item_name","item_id"));
    CopyNameMenuItem(name("rune","rune_id"),"Copy Rune Name");
    CopyNameMenuItem(name("sigil","sigil_id"),"Copy Sigil Name");
    CopyNameMenuItem(name("sigil2","sigil2_id"),"Copy Second Sigil Name");
    auto infusions=item.find("infusion_ids");
    if(infusions!=item.end()&&infusions->is_array()){
        std::vector<uint32_t> ids;
        for(const auto& id:*infusions)if(id.is_number_integer())ids.push_back(id.get<uint32_t>());
        CopyCachedItemNamesMenu(ids,"Copy Infusion Name");
    }
}
static void ComponentNativeGear(ComponentJson& data, const std::string& kind,
                                const std::string& profession) {
    ImGui::TextUnformatted("Equipment slots");
    ImGui::SameLine();
    ImGui::TextDisabled("(select a slot to customize)");
    if (data.contains("gear") && !data["gear"].is_object()) {
        ImGui::TextDisabled("Unrecognized gear data; preserved without alteration.");
        return;
    }
    ComponentJson empty = ComponentJson::object();
    const ComponentJson& savedGear = data.contains("gear") ? data["gear"] : empty;
    if (AlterEgo::UI::BeginCombo("Add equipment slot##component_gear", "Choose slot...")) {
        for (const char* slot : kNativeGearSlots) {
            if (!ComponentNativeSlotAllowed(kind, slot) || savedGear.contains(slot))
                continue;
            if (ImGui::Selectable(slot)) {
                data["gear"][slot] = ComponentJson::object();
                g_ComponentGearSelectedSlot = slot;
                g_ComponentGearRemovalPending.clear();
            }
        }
        ImGui::EndCombo();
    }

    if (!data.contains("gear") || data["gear"].empty()) {
        ImGui::TextDisabled("No equipment recorded. Add a slot to start.");
        return;
    }
    const auto slots = AlterEgo::Composable::Presentation::SortedGearSlots(data["gear"]);
    static ComponentJson copiedSlot;
    static std::string copiedKind, copiedFrom;
    if (g_ComponentGearSelectedSlot.empty() ||
        !data["gear"].contains(g_ComponentGearSelectedSlot)) {
        g_ComponentGearSelectedSlot = slots.front();
        g_ComponentGearRemovalPending.clear();
    }
    // List and details each have independent scrolling; opening one item's
    // customization no longer expands every armor/weapon/trinket field.
    const float width = ImGui::GetContentRegionAvail().x;
    const float listWidth = ImMin(195.0f, ImMax(135.0f, width * 0.30f));
    const float height = ImMax(280.0f, ImMin(510.0f, ImGui::GetContentRegionAvail().y - 18.0f));
    ImGui::BeginChild("##component_slot_list", ImVec2(listWidth, height), true);
    for (const auto& slot : slots) {
        ImGui::PushID(slot.c_str());
        const bool selected = g_ComponentGearSelectedSlot == slot;
        if (ImGui::Selectable(GearSlotDisplayName(slot), selected)) {
            g_ComponentGearSelectedSlot = slot;
            g_ComponentGearRemovalPending.clear();
            g_ComponentCatalogSearch.clear();
            g_ComponentStatSearch.clear();
        }
        if (ImGui::BeginPopupContextItem("##native_slot_context")) {
            ComponentStoredNameActions(data["gear"].at(slot));
            if (ImGui::MenuItem("Copy")) {
                copiedSlot = data["gear"].at(slot);
                copiedKind = kind;
                copiedFrom = slot;
            }
            const bool compatible = copiedKind == kind && copiedSlot.is_object() &&
                (copiedFrom == slot || kind == "armor" || kind == "trinkets" || kind == "runes");
            if (ImGui::MenuItem("Paste",nullptr,false,compatible)) {
                if (copiedFrom == slot) data["gear"][slot] = copiedSlot;
                else {
                    auto& dest = data["gear"][slot];
                    if (dest.is_object()) {
                        if (kind == "runes") {
                            for (const char* field : {"rune","rune_id"}) {
                                if (copiedSlot.contains(field)) dest[field] = copiedSlot[field];
                                else dest.erase(field);
                            }
                        } else {
                            for (const char* field : {"stat_id","stat_name"}) {
                                if (copiedSlot.contains(field)) dest[field] = copiedSlot[field];
                                else dest.erase(field);
                            }
                        }
                    }
                }
            }
            if ((kind == "armor" || kind == "trinkets" || kind == "runes") &&
                ImGui::MenuItem("Copy to All Recorded Slots")) {
                const auto source = data["gear"].at(slot);
                if (source.is_object()) for (auto& dest : data["gear"]) {
                    if (!dest.is_object()) continue;
                    const std::vector<const char*> fields = kind == "runes"
                        ? std::vector<const char*>{"rune","rune_id"}
                        : std::vector<const char*>{"stat_id","stat_name"};
                    for (const char* field : fields) {
                        if (source.contains(field)) dest[field] = source[field];
                        else dest.erase(field);
                    }
                }
            }
            if (ImGui::MenuItem("Clear Slot")) {
                g_ComponentGearSelectedSlot = slot;
                g_ComponentGearRemovalPending = slot;
            }
            ImGui::EndPopup();
        }
        if (ImGui::IsItemHovered()) {
            const auto& entry = data["gear"].at(slot);
            ImGui::SetTooltip("%s", AlterEgo::Composable::Presentation::
                GearSlotSummary(kind, entry).c_str());
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##component_slot_editor", ImVec2(0, height), true);
    const std::string slot = g_ComponentGearSelectedSlot;
    ImGui::PushID(slot.c_str());
    ImGui::Text("%s", GearSlotDisplayName(slot));
    const auto& recorded = data["gear"].at(slot);
    ImGui::TextWrapped("%s", AlterEgo::Composable::Presentation::
        GearSlotSummary(kind, recorded).c_str());
    ImGui::Separator();
    if (!recorded.is_object()) {
        ImGui::TextDisabled("Unknown imported slot representation; retained as-is.");
    } else {
        auto& item = data["gear"][slot];
        const bool water=kind=="aquatic";
        const bool weapon=kind=="weapons"||(water&&AlterEgo::EquipmentChoice::IsAquaticWeaponSlot(slot));
        if (kind == "armor" || kind == "weapons" || kind == "trinkets" || water) {
            const uint32_t id = item.value("item_id", uint32_t(0));
            const auto info = id ? AlterEgo::GW2API::GetItemInfo(id) : nullptr;
            if (info && !info->icon_url.empty()) {
                AlterEgo::IconManager::RequestIcon(id, info->icon_url);
                Texture_t* tex = AlterEgo::IconManager::GetIcon(id);
                if (tex && tex->Resource) {
                    ImGui::Image(tex->Resource, ImVec2(40, 40));
                    ImGui::SameLine();
                }
            }
            ImGui::TextWrapped("%s", info ? info->name.c_str() :
                               (id ? "Item metadata pending" : "No exact item selected"));
            if (id && !info && RenderActionButton("Fetch item details##component"))
                AlterEgo::GW2API::FetchItemDetailsAsync({id});
            if (ImGui::TreeNode("##exact_item", "Exact equipment item")) {
                ComponentNativeNumber(item, "item_id", "Item ID");
                ImGui::TreePop();
            }
            ImGui::TextUnformatted("Stats");
            ComponentNativeStatChoice(item);
            if (weapon)
                ComponentNativeWeaponChoice(item, profession, slot);
            if (ImGui::TreeNode("##advanced_gear_ids", "Advanced: exact stats and weapon type")) {
                ComponentNativeNumber(item, "stat_id", "Stat ID");
                ComponentNativeText(item, "stat_name", "Stat name");
                if (weapon)
                    ComponentNativeText(item, "weapon_type", "Weapon type");
                ImGui::TreePop();
            }
        }
        if (kind == "runes" || (water&&slot=="HelmAquatic")) {
            ImGui::TextUnformatted("Rune");
            ComponentNativeCatalogChoice(item, "rune_id", "rune", "runes");
            if (ImGui::TreeNode("##advanced_rune", "Advanced rune ID and name")) {
                ComponentNativeNumber(item, "rune_id", "Rune ID");
                ComponentNativeText(item, "rune", "Rune name");
                ImGui::TreePop();
            }
        }
        if (kind == "sigils" || (water&&weapon)) {
            ImGui::TextUnformatted("Sigil 1");
            ComponentNativeCatalogChoice(item, "sigil_id", "sigil", "sigils");
            ImGui::TextUnformatted("Sigil 2 (two-handed weapons)");
            ComponentNativeCatalogChoice(item, "sigil2_id", "sigil2", "sigils");
            if (ImGui::TreeNode("##advanced_sigils", "Advanced exact sigil IDs and names")) {
                ComponentNativeNumber(item, "sigil_id", "Sigil 1 ID");
                ComponentNativeText(item, "sigil", "Sigil 1 name");
                ComponentNativeNumber(item, "sigil2_id", "Sigil 2 ID");
                ComponentNativeText(item, "sigil2", "Sigil 2 name");
                ImGui::TreePop();
            }
        }
        if (kind == "infusions" || water) {
            ComponentNativeInfusionSlots(item,slot);
            ComponentNativeText(item, "infusion", "Legacy infusion label");
        }
        if (ImGui::TreeNode("##slot_exact", "Exact stored slot fields (read only)")) {
            for (auto field = item.begin(); field != item.end(); ++field)
                ImGui::TextWrapped("%s: %s", field.key().c_str(),
                                   field.value().dump().c_str());
            ImGui::TreePop();
        }
    }
    ImGui::Separator();
    if (g_ComponentGearRemovalPending == slot) {
        ImGui::TextColored(ImVec4(1.0f, 0.67f, 0.47f, 1.0f),
                           "Remove %s from this component draft?", slot.c_str());
        if (RenderActionButton("Confirm remove##component_gear")) {
            data["gear"].erase(slot);
            g_ComponentGearSelectedSlot.clear();
            g_ComponentGearRemovalPending.clear();
        }
        ImGui::SameLine();
        if (RenderActionButton("Keep slot##component_gear"))
            g_ComponentGearRemovalPending.clear();
    } else if (RenderActionButton("Remove slot...##component_gear")) {
        g_ComponentGearRemovalPending = slot;
    }
    ImGui::PopID();
    ImGui::EndChild();
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
            if (AlterEgo::UI::BeginCombo("##skill_profession_hint", preview)) {
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
        if (!id.empty() && ComponentUIUsage(ComponentUISnapshot(), "base_build", id)) {
            ImGui::TextDisabled("Profession: %s (fixed while referenced)",
                data.value("profession", std::string()).c_str());
        } else {
            ComponentNativeText(data, "profession", "Profession");
        }
        ComponentNativeSpecializations(data, data.value("profession", std::string()));
        ComponentNativeProfessionPair(data,"pets","Ranger pets",false,profession=="Ranger");
        ComponentNativeProfessionPair(data,"legend_codes","Revenant legends",true,profession=="Revenant");
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
        const auto specContext = ComponentNativeSkillSpecs(id);
        if (specContext.second && specContext.first.empty())
            ImGui::TextDisabled("Shared skill choices are limited to specs common to referencing Loadouts.");
        ComponentNativeSkillBar(data, "skills", "Terrestrial skills",
                                profession, specContext.first, !specContext.second);
        ComponentNativeSkillBar(data, "aquatic_skills", "Aquatic skills",
                                profession, specContext.first, !specContext.second);
    } else if (kind == "relic") {
        ComponentNativeCatalogChoice(data, "relic_id", "relic_name", "relic");
        if (ImGui::TreeNode("##advanced_relic", "Advanced exact relic item ID")) {
            ComponentNativeNumber(data, "relic_id", "Relic item ID");
            ComponentNativeText(data, "relic_name", "Relic name");
            ImGui::TreePop();
        }
    } else {
        if (kind == "runes") {
            ImGui::TextUnformatted("Default rune (legacy imports)");
            ComponentNativeCatalogChoice(data, "rune_id", "rune_name", "runes");
            if (ImGui::TreeNode("##shared_rune_advanced", "Advanced default rune fields")) {
                ComponentNativeNumber(data, "rune_id", "Shared rune ID");
                ComponentNativeText(data, "rune_name", "Shared rune name");
                ImGui::TreePop();
            }
            ImGui::TextDisabled("An unspecified shared rune count is unverified in Compare.");
        }
        if (kind == "infusions") ComponentNativeInfusionRequirements(data);
        const auto oldGear = kind == "infusions" ? data.value("gear", ComponentJson::object()) :
            ComponentJson::object();
        ComponentNativeGear(data, kind, profession);
        if (kind == "infusions" && oldGear != data.value("gear",ComponentJson::object())) {
            // A slot edit supersedes cached aggregate requirements. Resolve
            // the calculated totals from the assembled loadout's gear.
            data.erase("infusion_totals");
            data.erase("infusion_summary_present");
            data.erase("infusion_ar_total");
            data.erase("infusion_stat_totals");
        }
    }
    if (ImGui::CollapsingHeader("Advanced: exact stored fields (read only)")) {
        ImGui::BeginChild("##component_native_json", ImVec2(0, 130), true);
        ImGui::TextUnformatted(data.dump(2).c_str());
        ImGui::EndChild();
    }
}
