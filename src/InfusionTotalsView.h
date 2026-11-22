#pragma once
// Included by dllmain.cpp after FindEquipment and the edit-mode helpers.
// One summary calculator and one UI renderer for Live, Saved and Edit.
static const char* InfusionAttributeLabel(const std::string& attribute);
static const char* const kInfusionStatNames[] = {
    "Power", "Precision", "ConditionDamage", "ConditionDuration", "BoonDuration",
    "Healing", "Toughness", "Vitality", "CritDamage"
};

static std::vector<uint32_t> GetSavedGearInfusions(const AlterEgo::SavedBuild& b) {
    std::vector<uint32_t> ids;
    bool hasA = b.gear.count("WeaponA1") || b.gear.count("WeaponA2");
    for (const auto& [slot, gear] : b.gear) {
        if (slot.find("Aquatic") != std::string::npos) continue;
        if (slot.rfind("WeaponB", 0) == 0 && hasA) continue;
        if (slot.rfind("WeaponA", 0) == 0 && !hasA) continue;
        for (uint32_t id : gear.infusion_ids) if (id) ids.push_back(id);
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
        const auto* item = AlterEgo::GW2API::GetItemInfo(id);
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
static AlterEgo::InfusionSummary GetSavedInfusionSummary(const AlterEgo::SavedBuild& b) {
    if (b.infusion_summary_present)
        return {true, true, b.infusion_ar_total, b.infusion_stat_totals};
    if (!b.infusion_totals.empty()) {
        std::vector<std::pair<uint32_t, int>> items;
        for (const auto& i : b.infusion_totals) items.push_back({i.item_id, i.count});
        return ComputeInfusionSummary(items);
    }
    return ComputeInfusionsFromIds(GetSavedGearInfusions(b));
}
static AlterEgo::InfusionSummary GetLiveInfusionSummary(const AlterEgo::Character& ch, int tab) {
    return ComputeInfusionsFromIds(GetLiveGearInfusions(ch, tab));
}
static void UpdateInfusionEditDraft(AlterEgo::SavedBuild& edit,
                                    const AlterEgo::InfusionSummary& summary) {
    edit.infusion_summary_present = true;
    edit.infusion_ar_total = summary.agonyResistance;
    edit.infusion_stat_totals = summary.stats;
    MarkEditDirty();
}
static void RenderInfusionTotals(const AlterEgo::InfusionSummary& total,
                                 AlterEgo::SavedBuild* edit = nullptr) {
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.70f, 0.58f, 0.20f, 1.0f), "Infusion Totals");
    if (!total.complete)
        ImGui::TextDisabled("Some infusion metadata is unavailable; totals are unverified.");
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
    if (edit && !GetSavedGearInfusions(*edit).empty()) {
        if (ImGui::SmallButton("Calculate##infusion_totals")) {
            const auto ids = GetSavedGearInfusions(*edit);
            const auto computed = ComputeInfusionsFromIds(ids);
            if (computed.complete)
                UpdateInfusionEditDraft(*edit, computed);
            else {
                std::vector<uint32_t> missing;
                for (uint32_t id : ids)
                    if (!AlterEgo::GW2API::GetItemInfo(id)) missing.push_back(id);
                if (!missing.empty()) AlterEgo::GW2API::FetchItemDetailsAsync(missing);
            }
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Calculate stat bonuses and Agony Resistance from infusions in gear. Replaces manual totals when metadata is available.");
    }
}
