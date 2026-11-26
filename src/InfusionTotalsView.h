#pragma once
#include <functional>
// Included by dllmain.cpp after FindEquipment and the edit-mode helpers.
// One summary calculator and one UI renderer for Live, Saved and Edit.
static const char* InfusionAttributeLabel(const std::string& attribute);
static const char* const kInfusionStatNames[] = {
    "Power", "Precision", "ConditionDamage", "ConditionDuration", "BoonDuration",
    "Healing", "Toughness", "Vitality", "CritDamage"
};

static bool IsWeaponTwoHanded(const std::string& profession, const std::string& weaponType);
static bool CountSavedInfusionSlot(const AlterEgo::SavedBuild& b,const std::string& slot) {
    if (slot.find("Aquatic") != std::string::npos || slot == "Amulet" || slot == "Relic" ||
        slot == "Breather") return false;
    auto a = b.gear.find("WeaponA1"), a2 = b.gear.find("WeaponA2");
    const bool hasA = (a != b.gear.end() && !a->second.weapon_type.empty()) ||
                      (a2 != b.gear.end() && !a2->second.weapon_type.empty());
    if (hasA && slot.rfind("WeaponB",0) == 0) return false;
    if (slot == "WeaponA2" || slot == "WeaponB2") {
        auto main = b.gear.find(slot == "WeaponA2" ? "WeaponA1" : "WeaponB1");
        if (main != b.gear.end() && IsWeaponTwoHanded(b.profession, main->second.weapon_type)) return false;
    }
    return true;
}
static std::vector<uint32_t> GetSavedGearInfusions(const AlterEgo::SavedBuild& b) {
    std::vector<uint32_t> ids;
    for (const auto& [slot, gear] : b.gear) {
        if (!CountSavedInfusionSlot(b,slot)) continue;
        for (size_t i = 0; i < gear.infusion_ids.size(); ++i)
            if (gear.infusion_ids[i] && (i >= gear.infusion_attributes.size() ||
                gear.infusion_attributes[i].empty())) ids.push_back(gear.infusion_ids[i]);
    }
    return ids;
}
static std::vector<uint32_t> GetLiveGearInfusions(const AlterEgo::Character& ch, int tab) {
    std::vector<uint32_t> ids;
    const auto* a1 = FindEquipment(ch, "WeaponA1", tab);
    const auto* a2 = FindEquipment(ch, "WeaponA2", tab);
    const bool hasA = (a1 && a1->id) || (a2 && a2->id);
    static const char* slots[] = {
        "Helm", "Shoulders", "Coat", "Gloves", "Leggings", "Boots",
        "Backpack", "Accessory1", "Accessory2", "Ring1", "Ring2",
        "WeaponA1", "WeaponA2", "WeaponB1", "WeaponB2"
    };
    for (const char* slot : slots) {
        const std::string key(slot);
        if (hasA && key.rfind("WeaponB", 0) == 0) continue;
        if (!hasA && key.rfind("WeaponA", 0) == 0) continue;
        const auto* gear = FindEquipment(ch, key, tab);
        if (gear)
            for (uint32_t id : gear->infusions) if (id) ids.push_back(id);
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
// Ordinary gear changes must never implicitly switch the infusion mode.
static void InvalidateGearInfusionTotals(AlterEgo::SavedBuild&) {}
static AlterEgo::InfusionSummary ComputeSavedSlotInfusions(const AlterEgo::SavedBuild& b) {
    auto ids = GetSavedGearInfusions(b);
    std::map<std::string,int> flexible;
    bool recorded = false;
    for (const auto& entry : b.gear) {
        if (!CountSavedInfusionSlot(b,entry.first)) continue;
        const auto& gear = entry.second;
        recorded = recorded || gear.infusion_snapshot;
        for (const auto& attr : gear.infusion_attributes)
            if (!attr.empty()) ++flexible[attr];
    }
    // Cache by actual inputs; only retry when a missing item has arrived.
    static std::vector<uint32_t> lastIds;
    static std::map<std::string,int> lastFlexible;
    static bool lastRecorded = false, initialized = false;
    static AlterEgo::InfusionSummary cached;
    static std::vector<uint32_t> pending;
    const bool arrived = std::any_of(pending.begin(),pending.end(),[](uint32_t id) {
        const auto item = AlterEgo::GW2API::GetItemInfo(id);
        return item && item->details.is_object();
    });
    if (!initialized || ids != lastIds || flexible != lastFlexible || recorded != lastRecorded || arrived) {
        initialized = true; lastIds = ids; lastFlexible = flexible; lastRecorded = recorded;
        cached = ComputeInfusionsFromIds(ids);
        cached.present = cached.present || recorded || !flexible.empty();
        // Flexible requirements match any positive stat value; do not invent
        // +5 or AR for a requirement whose amount is not specified.
        if (!flexible.empty()) cached.complete = false;
        pending.clear();
        for (uint32_t id : ids) {
            const auto item = AlterEgo::GW2API::GetItemInfo(id);
            if (!item || !item->details.is_object()) pending.push_back(id);
        }
        if (!pending.empty()) AlterEgo::GW2API::FetchItemDetailsAsync(pending);
    }
    return cached;
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
static AlterEgo::InfusionSummary GetLiveInfusionSummary(const AlterEgo::Character& ch, int tab) {
    return ComputeInfusionsFromIds(GetLiveGearInfusions(ch, tab));
}
static void UpdateInfusionEditDraft(AlterEgo::SavedBuild& edit,
                                    const AlterEgo::InfusionSummary& summary) {
    if(AlterEgo::SetNumericInfusionTotals(edit,summary))MarkEditDirty();
}
static void RenderInfusionTotals(const AlterEgo::InfusionSummary& total,
                                 AlterEgo::SavedBuild* edit = nullptr,
                                 const std::function<void()>& headerAction = {}) {
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.70f, 0.58f, 0.20f, 1.0f), "Infusion Totals");
    if (headerAction) headerAction();
    if (!total.complete)
        ImGui::TextDisabled("Totals are partial: missing item metadata or flexible stat requirements.");
    if (edit && !edit->infusion_summary_present && edit->infusion_totals.empty()) {
        std::map<std::string,int> flexible;
        for (const auto& entry : edit->gear) if (CountSavedInfusionSlot(*edit,entry.first))
            for (const auto& attribute : entry.second.infusion_attributes)
                if (!attribute.empty()) ++flexible[attribute];
        for (const auto& entry : flexible)
            ImGui::TextDisabled("%s: %d flexible infusion slot%s",
                InfusionAttributeLabel(entry.first), entry.second,entry.second == 1 ? "" : "s");
    }
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
                UpdateInfusionEditDraft(*edit, updated);
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
                    UpdateInfusionEditDraft(*edit, updated);
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
            const auto computed=ComputeSavedSlotInfusions(build);
            if(mode==InfusionMode::Conflict||g_InfusionTotalsDraft||computed.complete){
                if(RenderActionButton("Use totals..."))ImGui::OpenPopup("##confirm_total_mode");
            }else ImGui::TextDisabled("Resolve missing metadata or flexible requirements before converting to totals.");
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
            if(RenderActionButton("Switch to totals")){
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
                }else SetNumericInfusionTotals(*edit,ComputeSavedSlotInfusions(*edit));
                MarkEditDirty();ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if(mode==InfusionMode::Slots&&g_InfusionTotalsDraft){
            const auto& old=*g_InfusionTotalsDraft;
            if(old.infusion_summary_present)
                ImGui::TextDisabled("Previous target: %d AR (restore with Use totals).",old.infusion_ar_total);
        }
    }
    if(mode!=InfusionMode::Conflict)
        RenderInfusionTotals(summary,edit&&mode==InfusionMode::Totals&&summary.complete?edit:nullptr);
}
