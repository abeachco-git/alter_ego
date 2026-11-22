#pragma once
// Fashion collection UI; included by dllmain.cpp after BuildWardrobeLink() so
// the same live-equipment snapshot and shared button/theme helpers are reused.
#include "FashionLibrary.h"

static AlterEgo::Fashion::Library g_FashionLibrary;
static int g_FashionSelected = -1;
static char g_FashionSearch[160] = "";
static int g_FashionFilter = 0;
static bool g_FashionDialog = false;
static int g_FashionEditing = -1;
static AlterEgo::Fashion::Look g_FashionDraft;
static char g_FashionName[256] = "";
static char g_FashionNotes[1024] = "";
static char g_FashionCharacterNames[2048] = "";
static int g_FashionWeightIndex = 0;
static std::string g_FashionError;
static const char* kFashionWeights[] = {"Unknown", "Light", "Medium", "Heavy", "Any"};

static std::vector<uint32_t> FashionSkinIds(const AlterEgo::DecodedWardrobeLink& w) {
    return {w.helm_skin,w.shoulders_skin,w.chest_skin,w.gloves_skin,w.leggings_skin,
            w.boots_skin,w.backpack_skin,w.aquabreather_skin,w.aquatic_weapon_a,
            w.aquatic_weapon_b,w.weapon_a_main,w.weapon_a_off,w.weapon_b_main,w.weapon_b_off};
}

static std::string FashionWeightFromSkins(const AlterEgo::DecodedWardrobeLink& w) {
    bool hasArmor = false, unknown = false;
    std::string weight;
    for (const auto id : {w.helm_skin,w.shoulders_skin,w.chest_skin,w.gloves_skin,w.leggings_skin,w.boots_skin}) {
        if (!id) continue;
        hasArmor = true;
        const auto* skin = AlterEgo::GW2API::GetSkinInfo(id);
        if (!skin || skin->weight_class.empty()) { unknown = true; continue; }
        const auto& candidate = skin->weight_class;
        if (candidate == "Clothing") continue;
        if (candidate != "Light" && candidate != "Medium" && candidate != "Heavy") continue;
        if (!weight.empty() && candidate != weight) return "Unknown";
        weight = candidate;
    }
    if (!hasArmor) return "Any";
    if (unknown) return "Unknown";
    return weight.empty() ? "Any" : weight;
}

static void StartFashionEdit(const AlterEgo::Fashion::Look& look, int index = -1) {
    g_FashionDraft = look;
    g_FashionEditing = index;
    snprintf(g_FashionName, sizeof(g_FashionName), "%s", look.name.c_str());
    snprintf(g_FashionNotes, sizeof(g_FashionNotes), "%s", look.notes.c_str());
    std::string names;
    for (const auto& n : look.characters) {
        if (!names.empty()) names += ", ";
        names += n;
    }
    snprintf(g_FashionCharacterNames, sizeof(g_FashionCharacterNames), "%s", names.c_str());
    g_FashionWeightIndex = 0;
    for (int i = 0; i < IM_ARRAYSIZE(kFashionWeights); ++i)
        if (look.weight == kFashionWeights[i]) g_FashionWeightIndex = i;
    g_FashionError.clear();
    g_FashionDialog = true;
}

static bool StartFashionImport(const std::string& input, const std::string& title) {
    AlterEgo::DecodedWardrobeLink w{};
    if (!AlterEgo::Fashion::DecodeValid(input, w)) return false;
    AlterEgo::Fashion::Look look;
    look.name = title.empty() ? "Imported Wardrobe" : title;
    look.wardrobe_link = AlterEgo::ChatLink::EncodeWardrobe(w);
    look.complete = true;
    look.origin = "wardrobe_link";
    look.weight = FashionWeightFromSkins(w);
    AlterEgo::GW2API::FetchSkinDetailsAsync(FashionSkinIds(w));
    StartFashionEdit(look);
    return true;
}

static void StartLiveFashionSave(const AlterEgo::Character& ch, int tab) {
    AlterEgo::DecodedWardrobeLink w{};
    const std::string link = BuildWardrobeLink(ch, tab);
    if (!AlterEgo::Fashion::DecodeValid(link, w)) {
        if (APIDefs) APIDefs->GUI_SendAlert("Could not capture wardrobe appearance.");
        return;
    }
    bool hasSkin = false;
    for (uint32_t id : FashionSkinIds(w)) if (id) { hasSkin = true; break; }
    if (!hasSkin) {
        if (APIDefs) APIDefs->GUI_SendAlert("No loaded equipment skins for that tab. Refresh character first.");
        return;
    }
    AlterEgo::Fashion::Look look;
    auto it = ch.equipment_tab_names.find(tab);
    look.name = it != ch.equipment_tab_names.end() && !it->second.empty()
        ? it->second : "Equipment " + std::to_string(tab);
    look.wardrobe_link = link;
    look.complete = false; // equipment API does not expose Fashion Template outfit/visibility
    look.origin = "live_equipment";
    look.source_character = ch.name;
    look.source_tab = tab;
    AlterEgo::Fashion::Associate(look, ch.name);
    look.weight = FashionWeightFromSkins(w);
    // A live equipment set determines worn armor weight even if skin metadata
    // hasn't loaded. This is not a profession restriction on the saved look.
    if (look.weight == "Unknown") {
        const auto& p = ch.profession;
        if (p == "Elementalist" || p == "Mesmer" || p == "Necromancer") look.weight = "Light";
        else if (p == "Engineer" || p == "Ranger" || p == "Thief") look.weight = "Medium";
        else if (p == "Guardian" || p == "Warrior" || p == "Revenant") look.weight = "Heavy";
    }
    AlterEgo::GW2API::FetchSkinDetailsAsync(FashionSkinIds(w));
    StartFashionEdit(look);
}

static void RenderFashionDialog() {
    if (!g_FashionDialog) return;
    // Fixed initial dimensions, user-resizable thereafter. Do not combine
    // an appearing-only width with AlwaysAutoResize: that made the window
    // shrink and jump as optional character/duplicate fields changed.
    ImGui::SetNextWindowSize(ImVec2(495, 350), ImGuiCond_Appearing);
    ImGui::SetNextWindowSizeConstraints(ImVec2(475, 310), ImVec2(FLT_MAX, FLT_MAX));
    if (!ImGui::Begin("Save Fashion##ae_fashion", &g_FashionDialog,
        ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    ImGui::TextUnformatted(g_FashionEditing >= 0 ? "Edit saved look" : "Save wardrobe appearance");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##fashion_name", "Appearance name", g_FashionName, sizeof(g_FashionName));
    ImGui::TextUnformatted("Armor weight");
    ImGui::SetNextItemWidth(150);
    ImGui::Combo("##fashion_weight", &g_FashionWeightIndex, kFashionWeights, IM_ARRAYSIZE(kFashionWeights));
    ImGui::TextUnformatted("Notes");
    ImGui::InputTextMultiline("##fashion_notes", g_FashionNotes, sizeof(g_FashionNotes), ImVec2(-1, 60));
    if (ImGui::CollapsingHeader("Associated characters")) {
        ImGui::TextDisabled("Optional comma-separated list (also searchable).");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##fashion_chars", g_FashionCharacterNames, sizeof(g_FashionCharacterNames));
    }
    if (!g_FashionDraft.complete)
        ImGui::TextDisabled("Equipment capture may omit outfit and visibility settings.");
    const int duplicate = g_FashionEditing < 0
        ? g_FashionLibrary.FindExact(g_FashionDraft.wardrobe_link) : -1;
    if (duplicate >= 0) {
        ImGui::TextWrapped("Same appearance saved as '%s'.", g_FashionLibrary.looks[duplicate].name.c_str());
        if (RenderActionButton("Select Existing")) {
            if (!g_FashionDraft.source_character.empty()) {
                auto existing = g_FashionLibrary.looks[duplicate];
                AlterEgo::Fashion::Associate(existing, g_FashionDraft.source_character);
                if (!g_FashionLibrary.Update(duplicate, existing))
                    g_FashionError = "Could not save character association.";
            }
            if (g_FashionError.empty()) {
                g_FashionSelected = duplicate;
                NavigateToLibraryCollection(1);
                g_FashionDialog = false;
            }
        }
        ImGui::SameLine();
    }
    const char* saveLabel = duplicate >= 0 ? "Save Separate Copy" :
                            (g_FashionEditing >= 0 ? "Save Changes" : "Save Fashion");
    if (RenderGoldButton(saveLabel)) {
        AlterEgo::Fashion::Look next = g_FashionDraft;
        next.name = g_FashionName;
        next.notes = g_FashionNotes;
        next.weight = kFashionWeights[g_FashionWeightIndex];
        next.characters.clear();
        std::stringstream names(g_FashionCharacterNames);
        std::string n;
        while (std::getline(names, n, ',')) {
            auto first = n.find_first_not_of(" \t");
            if (first == std::string::npos) continue;
            auto last = n.find_last_not_of(" \t");
            AlterEgo::Fashion::Associate(next, n.substr(first, last-first+1));
        }
        bool ok = false;
        if (g_FashionEditing >= 0) {
            ok = g_FashionLibrary.Update(g_FashionEditing, next);
            if (ok) g_FashionSelected = g_FashionEditing;
        } else {
            int added = g_FashionLibrary.Add(std::move(next));
            ok = added >= 0;
            if (ok) g_FashionSelected = added;
        }
        if (ok) {
            NavigateToLibraryCollection(1);
            g_FashionDialog = false;
            if (APIDefs) APIDefs->GUI_SendAlert("Fashion saved.");
        } else g_FashionError = "Could not save. Check the name and library file permissions.";
    }
    ImGui::SameLine();
    if (RenderChipButton("Cancel##fashion", false)) g_FashionDialog = false;
    if (!g_FashionError.empty())
        ImGui::TextColored(ImVec4(1, 0.45f, 0.4f, 1), "%s", g_FashionError.c_str());
    ImGui::End();
}

static void RenderFashionSlot(const char* label, uint16_t skinID, const uint16_t* dyeIDs = nullptr) {
    ImGui::PushID(label);
    ImGui::TextUnformatted(label);
    ImGui::SameLine(126);
    const auto* skin = skinID ? AlterEgo::GW2API::GetSkinInfo(skinID) : nullptr;
    if (!skinID) ImGui::TextDisabled("(none)");
    else {
        if (skin && !skin->icon_url.empty()) {
            AlterEgo::IconManager::RequestIcon(skinID, skin->icon_url);
            const auto* tex = AlterEgo::IconManager::GetIcon(skinID);
            if (tex && tex->Resource) {
                ImGui::Image(tex->Resource, ImVec2(22, 22));
                ImGui::SameLine();
            }
        }
        if (skin && !skin->name.empty()) ImGui::TextUnformatted(skin->name.c_str());
        else ImGui::Text("Skin #%u", skinID);
    }
    if (dyeIDs) {
        bool displayed = false;
        ImGui::Indent(126);
        for (int i = 0; i < 4; ++i) {
            if (skin && i < (int)skin->dye_slot_materials.size() &&
                skin->dye_slot_materials[i].empty()) continue;
            const int id = dyeIDs[i];
            if (id == 0 && (!skin || skin->dye_slot_count < 0)) continue;
            if (displayed) ImGui::SameLine(0, 4);
            displayed = true;
            ImGui::PushID(i);
            const auto* dye = id > 1 ? AlterEgo::GW2API::GetDyeColor(id) : nullptr;
            int r = dye ? dye->r : 65, g = dye ? dye->g : 65, b = dye ? dye->b : 65;
            if (dye && skin && i < (int)skin->dye_slot_materials.size()) {
                const auto& mat = skin->dye_slot_materials[i];
                if (mat == "cloth") { r = dye->cloth_r; g = dye->cloth_g; b = dye->cloth_b; }
                else if (mat == "leather") { r = dye->leather_r; g = dye->leather_g; b = dye->leather_b; }
                else if (mat == "metal") { r = dye->metal_r; g = dye->metal_g; b = dye->metal_b; }
            }
            ImGui::ColorButton("##fashion_dye", ImVec4(r/255.f, g/255.f, b/255.f, 1.f),
                ImGuiColorEditFlags_NoDragDrop | ImGuiColorEditFlags_NoTooltip, ImVec2(17, 17));
            if (ImGui::IsItemHovered()) {
                if (id == 1) ImGui::SetTooltip("Channel %d: Dye Remover", i+1);
                else if (!id) ImGui::SetTooltip("Channel %d: Empty", i+1);
                else if (dye) ImGui::SetTooltip("Channel %d: %s (#%d)", i+1, dye->name.c_str(), id);
                else ImGui::SetTooltip("Channel %d: Dye #%d", i+1, id);
            }
            ImGui::PopID();
        }
        ImGui::Unindent(126);
    }
    ImGui::PopID();
}

static void RenderFashionLibrary() {
    // Collection-wide actions live in the top-level Library dropdown.
    ImGui::SetNextItemWidth(180);
    ImGui::InputTextWithHint("##fashion_search", "Search looks / characters", g_FashionSearch, sizeof(g_FashionSearch));
    ImGui::SameLine();
    const char* filters[] = {"All weights", "Light", "Medium", "Heavy", "Any", "Unknown"};
    ImGui::SetNextItemWidth(130);
    ImGui::Combo("##fashion_filter", &g_FashionFilter, filters, IM_ARRAYSIZE(filters));
    float avail = ImGui::GetContentRegionAvail().x;
    float leftWidth = ImMax(130.f, ImMin(g_LibListWidth, ImMax(130.f, avail - 220.f)));
    ImGui::BeginChild("##fashion_list", ImVec2(leftWidth, 0), true);
    if (g_FashionLibrary.looks.empty())
        ImGui::TextWrapped("No saved fashion. Capture an Equipment tab or import a Wardrobe Template.");
    for (int i = 0; i < (int)g_FashionLibrary.looks.size(); ++i) {
        const auto& look = g_FashionLibrary.looks[i];
        if (!AlterEgo::Fashion::MatchesSearch(look, g_FashionSearch)) continue;
        if (g_FashionFilter && look.weight != filters[g_FashionFilter]) continue;
        ImGui::PushID(i);
        if (ImGui::Selectable(look.name.c_str(), i == g_FashionSelected)) g_FashionSelected = i;
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##fashion_detail", ImVec2(0, 0), true);
    int toDelete = -1;
    if (g_FashionSelected < 0 || g_FashionSelected >= (int)g_FashionLibrary.looks.size()) {
        ImGui::TextDisabled("Select a saved appearance.");
    } else {
        auto& look = g_FashionLibrary.looks[g_FashionSelected];
        AlterEgo::DecodedWardrobeLink w{};
        const bool decoded = AlterEgo::Fashion::DecodeValid(look.wardrobe_link, w);
        if (decoded) {
            static std::string lastRequested;
            if (look.id != lastRequested) {
                lastRequested = look.id;
                AlterEgo::GW2API::FetchSkinDetailsAsync(FashionSkinIds(w));
            }
            if (look.weight == "Unknown") {
                const auto resolved = FashionWeightFromSkins(w);
                if (resolved != "Unknown") { look.weight = resolved; g_FashionLibrary.Save(); }
            }
        }
        ImGui::TextUnformatted(look.name.c_str());
        ImGui::TextDisabled("%s | %s", look.weight.c_str(),
            look.complete ? "Wardrobe Template" : "Equipment-derived (partial)");
        if (RenderActionButton("Copy Wardrobe Link")) {
            CopyToClipboard(look.wardrobe_link);
            if (APIDefs) APIDefs->GUI_SendAlert("Wardrobe link copied.");
        }
        if (g_PieUIPresent.load()) {
            ImGui::SameLine();
            if (RenderActionButton("Preview")) PieUI_PreviewChatLink(look.wardrobe_link);
        }
        ImGui::SameLine();
        if (RenderActionButton("Edit##fashion")) StartFashionEdit(look, g_FashionSelected);
        ImGui::SameLine();
        if (RenderActionButton("Delete##fashion")) ImGui::OpenPopup("Delete appearance##fashion");
        if (ImGui::BeginPopupModal("Delete appearance##fashion", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("Delete '%s' from the Fashion Library?", look.name.c_str());
            if (RenderGoldButton("Delete")) { toDelete = g_FashionSelected; ImGui::CloseCurrentPopup(); }
            ImGui::SameLine();
            if (RenderChipButton("Cancel##delete_fashion", false)) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        if (!look.notes.empty()) { ImGui::Spacing(); ImGui::TextWrapped("%s", look.notes.c_str()); }
        if (!look.complete)
            ImGui::TextDisabled("The equipment API may omit outfit / visibility settings.");
        const std::string assocLabel = "Associated characters (" + std::to_string(look.characters.size()) + ")";
        if (ImGui::TreeNode(assocLabel.c_str())) {
            for (const auto& ch : look.characters) ImGui::BulletText("%s", ch.c_str());
            if (look.characters.empty()) ImGui::TextDisabled("(none)");
            if (!look.source_character.empty()) ImGui::TextDisabled("Original: %s, equipment %d",
                look.source_character.c_str(), look.source_tab);
            ImGui::TreePop();
        }
        ImGui::Separator();
        if (decoded) {
            ImGui::BeginChild("##fashion_appearance", ImVec2(0, 0), false);
            ImGui::TextDisabled("Armor, back and outfit");
            RenderFashionSlot("Head", w.helm_skin, w.helm_dyes);
            RenderFashionSlot("Shoulders", w.shoulders_skin, w.shoulders_dyes);
            RenderFashionSlot("Chest", w.chest_skin, w.chest_dyes);
            RenderFashionSlot("Gloves", w.gloves_skin, w.gloves_dyes);
            RenderFashionSlot("Legs", w.leggings_skin, w.leggings_dyes);
            RenderFashionSlot("Boots", w.boots_skin, w.boots_dyes);
            RenderFashionSlot("Back", w.backpack_skin, w.backpack_dyes);
            if (w.outfit_id) {
                ImGui::Text("Outfit #%u", w.outfit_id);
                ImGui::Indent(126);
                for (int i = 0; i < 4; ++i) {
                    if (i) ImGui::SameLine(0, 4);
                    ImGui::PushID(100+i);
                    int id = w.outfit_dyes[i];
                    const auto* dye = id > 1 ? AlterEgo::GW2API::GetDyeColor(id) : nullptr;
                    ImGui::ColorButton("##outfit_dye", ImVec4(dye ? dye->cloth_r/255.f : .3f,
                        dye ? dye->cloth_g/255.f : .3f, dye ? dye->cloth_b/255.f : .3f, 1),
                        ImGuiColorEditFlags_NoTooltip, ImVec2(17,17));
                    if (ImGui::IsItemHovered()) {
                        if (id <= 1) ImGui::SetTooltip("Dye Remover / empty");
                        else if (dye) ImGui::SetTooltip("%s (#%d)", dye->name.c_str(), id);
                        else ImGui::SetTooltip("Dye #%d", id);
                    }
                    ImGui::PopID();
                }
                ImGui::Unindent(126);
            }
            ImGui::Spacing();
            ImGui::TextDisabled("Weapons");
            RenderFashionSlot("Set 1 main", w.weapon_a_main);
            RenderFashionSlot("Set 1 off", w.weapon_a_off);
            RenderFashionSlot("Set 2 main", w.weapon_b_main);
            RenderFashionSlot("Set 2 off", w.weapon_b_off);
            if (ImGui::TreeNode("Underwater / visibility")) {
                RenderFashionSlot("Breather", w.aquabreather_skin);
                RenderFashionSlot("Aquatic 1", w.aquatic_weapon_a);
                RenderFashionSlot("Aquatic 2", w.aquatic_weapon_b);
                ImGui::Text("Visibility: 0x%04X", w.visibility_flags);
                ImGui::TreePop();
            }
            ImGui::EndChild();
        }
    }
    ImGui::EndChild();
    if (toDelete >= 0 && g_FashionLibrary.Remove(toDelete))
        g_FashionSelected = g_FashionLibrary.looks.empty() ? -1 :
            ImMin(toDelete, (int)g_FashionLibrary.looks.size()-1);
}
