#pragma once
#include <functional>
#include <array>
// Included by dllmain.cpp after FindEquipment and the edit-mode helpers.
// One summary calculator and one UI renderer for Live, Saved and Edit.
static const char* InfusionAttributeLabel(const std::string& attribute);
static const char* const kInfusionStatNames[] = {
    "Power", "Precision", "ConditionDamage", "ConditionDuration", "BoonDuration",
    "Healing", "Toughness", "Vitality", "CritDamage"
};

static bool IsWeaponTwoHanded(const std::string& profession, const std::string& weaponType);
using InfusionConfig=AlterEgo::InfusionConfiguration;
static std::vector<InfusionConfig> SavedInfusionConfigurations(const AlterEgo::SavedBuild& b) {
    return AlterEgo::InfusionConfigurations([&](const char* slot){
        const auto it=b.gear.find(slot);
        return it!=b.gear.end()&&(it->second.item_id||!it->second.weapon_type.empty()||
            AlterEgo::HasSlotInfusionData(it->second));
    });
}
static std::vector<InfusionConfig> LiveInfusionConfigurations(const AlterEgo::Character& ch,int tab) {
    return AlterEgo::InfusionConfigurations([&](const char* slot){
        const auto* eq=AlterEgo::FindComparisonEquipment(ch.equipment,slot,tab);return eq&&eq->id;
    });
}
static bool CountSavedInfusionSlot(const AlterEgo::SavedBuild& b,const std::string& slot,InfusionConfig config) {
    if(!AlterEgo::InfusionSlotInConfiguration(slot,config))return false;
    if(slot=="AquaticA"&&b.gear.count("WeaponAquaticA"))return false;
    if(slot=="AquaticB"&&b.gear.count("WeaponAquaticB"))return false;
    if(slot=="Breather"&&b.gear.count("HelmAquatic"))return false;
    if (slot == "WeaponA2" || slot == "WeaponB2") {
        auto main = b.gear.find(slot == "WeaponA2" ? "WeaponA1" : "WeaponB1");
        if (main != b.gear.end() && IsWeaponTwoHanded(b.profession, main->second.weapon_type)) return false;
    }
    return true;
}
static std::vector<uint32_t> GetSavedGearInfusions(const AlterEgo::SavedBuild& b,InfusionConfig config) {
    std::vector<uint32_t> ids;
    for (const auto& [slot, gear] : b.gear) {
        if (!CountSavedInfusionSlot(b,slot,config)) continue;
        for (size_t i = 0; i < gear.infusion_ids.size(); ++i)
            if (gear.infusion_ids[i] && (i >= gear.infusion_attributes.size() ||
                gear.infusion_attributes[i].empty())) ids.push_back(gear.infusion_ids[i]);
    }
    return ids;
}
static std::vector<uint32_t> GetLiveGearInfusions(const AlterEgo::Character& ch, int tab,InfusionConfig config) {
    std::vector<uint32_t> ids;
    static const char* slots[] = {
        "Helm", "Shoulders", "Coat", "Gloves", "Leggings", "Boots",
        "Backpack", "Accessory1", "Accessory2", "Ring1", "Ring2",
        "WeaponA1", "WeaponA2", "WeaponB1", "WeaponB2",
        "HelmAquatic","WeaponAquaticA","WeaponAquaticB"
    };
    for (const char* slot : slots) {
        if(!AlterEgo::InfusionSlotInConfiguration(slot,config))continue;
        const auto* gear=AlterEgo::FindComparisonEquipment(ch.equipment,slot,tab);
        if (gear) for (uint32_t id : gear->infusions) if (id) ids.push_back(id);
    }
    return ids;
}
static AlterEgo::InfusionSummary ComputeInfusionSummary(
    const std::vector<std::pair<uint32_t, int>>& items) {
    AlterEgo::InfusionSummary total;
    for (const auto& [id, count] : items) {
        if (!id || count <= 0) continue;
        total.present = true;
        const auto item = AlterEgo::GW2API::GetItemInfo(id);
        if (!item || !item->details.is_object()) {
            total.complete = false;
            continue;
        }
        int agonyAttr = 0;
        int agonyBuff = 0;
        auto infix = item->details.find("infix_upgrade");
        if (infix != item->details.end() && infix->is_object()) {
            auto attrs = infix->find("attributes");
            if (attrs != infix->end() && attrs->is_array()) {
                for (const auto& a : *attrs) {
                    if (!a.is_object()) continue;
                    const std::string attr = a.value("attribute", std::string());
                    auto mod = a.find("modifier");
                    if (attr.empty() || mod == a.end() || !mod->is_number()) continue;
                    const int value = static_cast<int>(mod->get<double>());
                    if (value <= 0) continue;
                    if (attr == "AgonyResistance") agonyAttr += value;
                    else total.stats[attr == "HealingPower" ? "Healing" : attr] += count * value;
                }
            }
            auto buff = infix->find("buff");
            if (buff != infix->end() && buff->is_object()) {
                auto desc = buff->find("description");
                if (desc != buff->end() && desc->is_string())
                    agonyBuff = AlterEgo::ParseAgonyResistance(desc->get<std::string>());
            }
        }
        // AR appears in buff text on many infusions, not in attributes.
        if (!agonyAttr && !agonyBuff)
            agonyBuff = AlterEgo::ParseAgonyResistance(item->description);
        if (!agonyAttr && !agonyBuff)
            agonyBuff = AlterEgo::ParseAgonyResistance(item->name);
        total.agonyResistance += count * (agonyAttr ? agonyAttr : agonyBuff);
    }
    return total;
}
static AlterEgo::InfusionSummary ComputeInfusionsFromIds(const std::vector<uint32_t>& ids) {
    std::vector<std::pair<uint32_t, int>> counted;
    counted.reserve(ids.size());
    for (uint32_t id : ids) if (id) counted.push_back({id, 1});
    return ComputeInfusionSummary(counted);
}
static AlterEgo::InfusionSummary ComputeSavedSlotInfusions(const AlterEgo::SavedBuild& b,InfusionConfig config) {
    auto ids=GetSavedGearInfusions(b,config);
    std::map<std::string,int> flexible;
    bool recorded=false,unknown=false;
    for(const auto& entry:b.gear){
        if(!CountSavedInfusionSlot(b,entry.first,config))continue;
        const auto& gear=entry.second;
        recorded=recorded||gear.infusion_snapshot;
        if(!gear.infusion.empty()&&gear.infusion_ids.empty()&&gear.infusion_attributes.empty())unknown=true;
        for(const auto& attr:gear.infusion_attributes)if(!attr.empty())++flexible[attr];
    }
    // Independent cache per simultaneous equipment configuration. Metadata
    // arrives through the existing request handler; local display issues no
    // new requests unless the set of input IDs changes or a missing row arrives.
    struct Cache {
        std::vector<uint32_t> ids,pending;
        std::map<std::string,int> flexible;
        bool recorded=false,unknown=false,initialized=false;
        AlterEgo::InfusionSummary summary;
    };
    static std::array<Cache,4> caches;
    auto& cache=caches[static_cast<size_t>(config)];
    const bool arrived=std::any_of(cache.pending.begin(),cache.pending.end(),[](uint32_t id){
        const auto item=AlterEgo::GW2API::GetItemInfo(id);return item&&item->details.is_object();
    });
    if(!cache.initialized||ids!=cache.ids||flexible!=cache.flexible||recorded!=cache.recorded||unknown!=cache.unknown||arrived){
        cache.initialized=true;cache.ids=ids;cache.flexible=flexible;cache.recorded=recorded;cache.unknown=unknown;
        cache.summary=ComputeInfusionsFromIds(ids);
        cache.summary.present=cache.summary.present||recorded||unknown||!flexible.empty();
        if(unknown||!flexible.empty())cache.summary.complete=false;
        cache.pending.clear();
        for(auto id:ids){
            const auto item=AlterEgo::GW2API::GetItemInfo(id);
            if(!item||!item->details.is_object())cache.pending.push_back(id);
        }
        if(!cache.pending.empty())AlterEgo::GW2API::FetchItemDetailsAsync(cache.pending);
    }
    return cache.summary;
}
static AlterEgo::InfusionSummary ComputeSavedSlotInfusions(const AlterEgo::SavedBuild& b) {
    return ComputeSavedSlotInfusions(b,SavedInfusionConfigurations(b).front());
}
static AlterEgo::InfusionSummary GetSavedInfusionSummary(const AlterEgo::SavedBuild& b) {
    if (AlterEgo::GetInfusionMode(b)==AlterEgo::InfusionMode::Conflict)
        return {true,false,0,{}};
    if (b.infusion_summary_present)
        return {true, true, b.infusion_ar_total, b.infusion_stat_totals};
    if (!b.infusion_totals.empty()) {
        std::vector<std::pair<uint32_t, int>> items;
        bool complete=true;
        for (const auto& i : b.infusion_totals) {
            if(!i.attribute.empty()||!i.item_id){if(i.count>0)complete=false;continue;}
            items.push_back({i.item_id, i.count});
        }
        auto result=ComputeInfusionSummary(items);
        result.present=true;result.complete=result.complete&&complete;return result;
    }
    return ComputeSavedSlotInfusions(b);
}
static AlterEgo::InfusionSummary GetLiveInfusionSummary(const AlterEgo::Character& ch,int tab,InfusionConfig config) {
    return ComputeInfusionsFromIds(GetLiveGearInfusions(ch,tab,config));
}
static AlterEgo::InfusionSummary GetLiveInfusionSummary(const AlterEgo::Character& ch,int tab) {
    return GetLiveInfusionSummary(ch,tab,LiveInfusionConfigurations(ch,tab).front());
}
static void UpdateInfusionEditDraft(AlterEgo::SavedBuild& edit,
                                    const AlterEgo::InfusionSummary& summary) {
    if(AlterEgo::SetNumericInfusionTotals(edit,summary))MarkEditDirty();
}
static void RenderInfusionTotals(const AlterEgo::InfusionSummary& total,
                                 const std::function<void(const AlterEgo::InfusionSummary&)>& update = {},
                                 const std::function<void()>& headerAction = {},
                                 const char* title="Infusion Totals") {
    const bool edit=static_cast<bool>(update);
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.70f, 0.58f, 0.20f, 1.0f), "%s",title);
    if (headerAction) headerAction();
    if (!total.complete)
        ImGui::TextDisabled("Totals are partial: missing item metadata or flexible stat requirements.");
    if (!total.present && !edit)
        ImGui::TextDisabled("No infusion totals specified");
    if (ImGui::BeginTable("##infusion_totals", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        if (edit) {
            int ar = total.agonyResistance;
            ImGui::SetNextItemWidth(75.0f);
            if (ImGui::InputInt("AR##saved_ar", &ar, 0, 0)) {
                auto updated = total;
                updated.agonyResistance = std::max(0, ar);
                update(updated);
            }
        } else if (total.present) {
            ImGui::Text("Agony Resistance: %d", total.agonyResistance);
        }
        for (size_t n = 0; n < sizeof(kInfusionStatNames)/sizeof(kInfusionStatNames[0]); ++n) {
            const std::string stat = kInfusionStatNames[n];
            const auto it = total.stats.find(stat);
            const int current = it == total.stats.end() ? 0 : it->second;
            if (!edit && current <= 0) continue;
            ImGui::TableNextColumn();
            ImGui::PushID(static_cast<int>(n));
            if (edit) {
                int value = current;
                ImGui::SetNextItemWidth(75.0f);
                if (ImGui::InputInt(InfusionAttributeLabel(stat), &value, 0, 0)) {
                    auto updated = total;
                    updated.stats[stat] = std::max(0, value);
                    update(updated);
                }
            } else {
                ImGui::Text("%s: +%d", InfusionAttributeLabel(stat), current);
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

}

 // Existing aggregate counts remain a supported import representation. Editing
 // numerical totals explicitly converts them, never retains both as targets.
static void RenderSlotInfusionSummaries(const AlterEgo::SavedBuild& b) {
    for(auto config:SavedInfusionConfigurations(b)){
        ImGui::PushID(static_cast<int>(config));
        RenderInfusionTotals(ComputeSavedSlotInfusions(b,config),{}, {},AlterEgo::InfusionConfigurationLabel(config));
        ImGui::PopID();
    }
}
static void RenderLiveInfusionTotals(const AlterEgo::Character& ch,int tab,const std::function<void()>& headerAction={}) {
    bool first=true;
    for(auto config:LiveInfusionConfigurations(ch,tab)){
        ImGui::PushID(static_cast<int>(config));
        RenderInfusionTotals(GetLiveInfusionSummary(ch,tab,config),{},first?headerAction:std::function<void()>{},
            AlterEgo::InfusionConfigurationLabel(config));
        ImGui::PopID();first=false;
    }
}
static void RenderSavedInfusionMode(const AlterEgo::SavedBuild& build,AlterEgo::SavedBuild* edit) {
    using namespace AlterEgo;
    const auto mode=GetInfusionMode(build);
    ImGui::Spacing();
    ImGui::TextUnformatted(mode==InfusionMode::Slots?"Infusions: By Equipment Slot":
        mode==InfusionMode::Totals?"Infusions: By Totals":"Infusions: Mixed legacy requirements");
    if(mode==InfusionMode::Conflict)
        ImGui::TextWrapped("Both slot and aggregate requirements are stored. Neither is authoritative until you select a mode. Existing data is preserved.");
    const auto summary=GetSavedInfusionSummary(build);
    if(edit){
        if(mode!=InfusionMode::Slots&&RenderActionButton("Use equipment slots..."))
            ImGui::OpenPopup("##confirm_slot_mode");
        if(mode!=InfusionMode::Totals){
            if(mode==InfusionMode::Conflict)ImGui::SameLine();
            if(RenderActionButton("Use totals..."))ImGui::OpenPopup("##confirm_total_mode");
        }
        if(ImGui::BeginPopup("##confirm_slot_mode")){
            ImGui::TextWrapped("Aggregate targets will be removed on Save. Configure individual equipment slots; no automatic distribution.");
            if(RenderActionButton("Switch to slots")){
                g_InfusionTotalsDraft=*edit;
                if(g_InfusionSlotDraft)RestoreSlotInfusions(*edit,*g_InfusionSlotDraft);
                else ForgetAggregateInfusions(*edit);
                MarkEditDirty();ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if(ImGui::BeginPopup("##confirm_total_mode")){
            ImGui::TextWrapped("Individual infusion identities and placement will be removed on Save. You can switch back before saving.");
            auto configs=SavedInfusionConfigurations(build);
            configs.erase(std::remove_if(configs.begin(),configs.end(),[](auto c){
                return c==InfusionConfig::WaterA||c==InfusionConfig::WaterB;
            }),configs.end());
            static int chosen=-1;
            if(ImGui::IsWindowAppearing())chosen=configs.size()==1?static_cast<int>(configs.front()):-1;
            const bool reuse=mode==InfusionMode::Conflict||g_InfusionTotalsDraft.has_value();
            if(!reuse){
                ImGui::TextUnformatted("Aggregate targets apply to terrestrial equipment. Choose a reference set:");
                for(auto config:configs)
                    if(ImGui::RadioButton(InfusionConfigurationLabel(config),chosen==static_cast<int>(config)))chosen=static_cast<int>(config);
            }
            const bool selected=std::any_of(configs.begin(),configs.end(),[&](auto c){return static_cast<int>(c)==chosen;});
            const auto calculated=selected?ComputeSavedSlotInfusions(build,static_cast<InfusionConfig>(chosen)):InfusionSummary{false,false,0,{}};
            if(!reuse&&selected&&!calculated.complete)ImGui::TextDisabled("Metadata or amounts are incomplete; automatic calculation is unavailable.");
            const bool calculatedSwitch=(reuse||(selected&&calculated.complete))&&RenderActionButton("Switch to totals");
            const bool blankSwitch=!reuse&&RenderActionButton("Start blank manual totals");
            if(calculatedSwitch||blankSwitch){
                g_InfusionSlotDraft=*edit;
                if(mode==InfusionMode::Conflict){
                    ForgetSlotInfusions(*edit);
                }else if(g_InfusionTotalsDraft){
                    ForgetSlotInfusions(*edit);
                    const auto& old=*g_InfusionTotalsDraft;
                    edit->infusion_totals=old.infusion_totals;
                    edit->infusion_summary_present=old.infusion_summary_present;
                    edit->infusion_ar_total=old.infusion_ar_total;
                    edit->infusion_stat_totals=old.infusion_stat_totals;
                }else SetNumericInfusionTotals(*edit,blankSwitch?InfusionSummary{true,true,0,{}}:calculated);
                MarkEditDirty();ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if(mode==InfusionMode::Slots&&g_InfusionTotalsDraft&&
            ImGui::CollapsingHeader("Previous aggregate targets (unsaved reference)")){
            auto prior=*g_InfusionTotalsDraft;ForgetSlotInfusions(prior);
            ImGui::PushID("previous_targets");RenderInfusionTotals(GetSavedInfusionSummary(prior));ImGui::PopID();
        }
    }
    if(mode==InfusionMode::Slots)RenderSlotInfusionSummaries(build);
    if(mode==InfusionMode::Totals){
        std::function<void(const InfusionSummary&)> update;
        if(edit&&summary.complete)update=[&](const InfusionSummary& next){UpdateInfusionEditDraft(*edit,next);};
        if(!build.infusion_totals.empty())ImGui::TextDisabled("Imported aggregate recommendations; editing converts to numeric targets.");
        if(edit&&!summary.complete&&RenderActionButton("Replace with blank manual totals..."))
            ImGui::OpenPopup("##manual_targets");
        if(ImGui::BeginPopup("##manual_targets")){
            ImGui::TextWrapped("Imported amounts cannot be verified. Starting blank replaces these recommendations on Save.");
            if(RenderActionButton("Start manual totals")){
                UpdateInfusionEditDraft(*edit,{true,true,0,{}});ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        RenderInfusionTotals(summary,update);
    }
}
