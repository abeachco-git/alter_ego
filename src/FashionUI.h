#pragma once
// Fashion collection UI; included by dllmain.cpp after BuildWardrobeLink() so
// the same live-equipment snapshot and shared button/theme helpers are reused.
#include "FashionLibrary.h"
#include <optional>

static AlterEgo::Fashion::Library g_FashionLibrary;
static std::string g_FashionSelectedId;
static std::optional<AlterEgo::Fashion::Look> g_FashionDeleteTarget;
static bool g_FashionDeletePending=false;
static std::map<std::string,std::string> g_FashionDeleteUsers;
static AlterEgo::UI::NameActionDraft g_FashionNameAction;
static std::optional<AlterEgo::Fashion::Look> g_FashionNameTarget;
static bool g_FashionNewPending=false;
static std::string g_FashionNewInput,g_FashionNewError;
static std::string g_FashionDeleteError;
static char g_FashionSearch[160] = "";
static int g_FashionFilter = 0;
static bool g_FashionDialog = false;
static AddonWindowFocusRequest g_FashionDialogFocus;
static bool g_FashionEditing=false;
static AlterEgo::Fashion::Look g_FashionDraft,g_FashionEditBaseline;
static char g_FashionName[256] = "";
static char g_FashionNotes[1024] = "";
static char g_FashionCharacterNames[2048] = "";
static int g_FashionWeightIndex = 0;
static std::string g_FashionError;
static nlohmann::json g_FashionFormBaseline;
static nlohmann::json FashionFormSnapshot(){
    return {{"name",std::string(g_FashionName)},{"notes",std::string(g_FashionNotes)},
        {"characters",std::string(g_FashionCharacterNames)},{"weight",g_FashionWeightIndex},
        {"character_associations",AlterEgo::CharacterIdentity::EncodeAssociations(g_FashionDraft.associations)}};
}
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
        const auto skin = AlterEgo::GW2API::GetSkinInfo(id);
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

static bool StartFashionEdit(const AlterEgo::Fashion::Look& look, int index = -1) {
    g_FashionDialogFocus.Request();
    // This window is modeless: don't silently replace an existing draft.
    if(g_FashionDialog){
        const bool same=g_FashionEditing==(index>=0)&&g_FashionDraft.id==look.id&&g_FashionDraft.wardrobe_link==look.wardrobe_link;
        if(!same)g_FashionError="Save or cancel the current Fashion draft first.";
        return same;
    }
    if(g_LibEditMode||OtherLibraryEditorOpen()){
        if(APIDefs)APIDefs->GUI_SendAlert("Finish the current Library editor first.");return false;
    }
    g_FashionEditBaseline=index>=0&&index<(int)g_FashionLibrary.looks.size()?g_FashionLibrary.looks[index]:look;
    g_FashionDraft = look;
    g_FashionEditing=index>=0;
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
    g_FashionError.clear();g_FashionFormBaseline=FashionFormSnapshot();
    g_FashionDialog = true;
    return true;
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
    return StartFashionEdit(look);
}

// Capture only on explicit Save, using the chosen equipment tab. The saved
// Fashion editor remains available for notes, associations and armor weight.
static bool CaptureLiveFashion(const AlterEgo::Character& ch,int tab,
    const std::string& name,AlterEgo::Fashion::Look& look) {
    using namespace AlterEgo;
    const std::string link=BuildWardrobeLink(ch,tab);
    const auto address=CharacterAddressFor(ch);
    if(!address||!Fashion::MakeLiveLook(link,name,ch.name,tab,look))return false;
    look.source_identity=CharacterIdentity::CaptureProvenance(*address);
    look.characters.clear(); // legacy labels are not live associations
    if(!Fashion::AssociateVerified(look,*look.source_identity))return false;
    DecodedWardrobeLink w{};
    if(!Fashion::DecodeValid(look.wardrobe_link,w))return false;
    look.weight=FashionWeightFromSkins(w);
    if(look.weight=="Unknown") {
        const auto& p=ch.profession;
        if(p=="Elementalist"||p=="Mesmer"||p=="Necromancer")look.weight="Light";
        else if(p=="Engineer"||p=="Ranger"||p=="Thief")look.weight="Medium";
        else if(p=="Guardian"||p=="Warrior"||p=="Revenant")look.weight="Heavy";
    }
    GW2API::FetchSkinDetailsAsync(FashionSkinIds(w));
    return true;
}

static bool SelectExistingFashion(int index,const AlterEgo::Fashion::Look& captured) {
    if(index<0||index>=static_cast<int>(g_FashionLibrary.looks.size()))return false;
    auto existing=g_FashionLibrary.looks[index];
    // Reuse the appearance, not its source provenance. Add explicitly
    // captured/selected identities without rewriting the original source.
    for(const auto& p:captured.associations)AlterEgo::Fashion::AssociateVerified(existing,p);
    for(const auto& label:captured.characters)AlterEgo::Fashion::Associate(existing,label);
    if(AlterEgo::Fashion::ToJson(existing)!=AlterEgo::Fashion::ToJson(g_FashionLibrary.looks[index])&&
       !g_FashionLibrary.UpdateDraft(g_FashionLibrary.looks[index],existing))return false;
    g_FashionSelectedId=existing.id;
    NavigateToLibraryCollection(1);
    return true;
}

static bool SaveNewFashion(const AlterEgo::Fashion::Look& look) {
    const int index=g_FashionLibrary.Add(look);
    if(index<0)return false;
    g_FashionSelectedId=g_FashionLibrary.looks[index].id;
    NavigateToLibraryCollection(1);
    if(APIDefs)APIDefs->GUI_SendAlert("Fashion saved.");
    return true;
}

// The same profession-aware character row and account-qualified ordering as
// Compare/Inventory. An association is an explicit selection, not a filter.
static bool SelectProfessionCharacterEntry(const char*,bool,const std::string&,const std::string&,
    const char*,ImGuiSelectableFlags,const std::string&);
static void RenderFashionAssociations(AlterEgo::Fashion::Look& draft){
    using namespace AlterEgo;
    ImGui::TextUnformatted("Verified characters");
    const auto preview=draft.associations.empty()?std::string("None"):
        std::to_string(draft.associations.size())+" associated";
    ImGui::SetNextItemWidth(-1);
    if(RenderThemedCombo("##fashion_verified_characters",preview.c_str())){
        std::map<CharacterOrder::Key,CharacterIdentity::Address> candidates;
        std::map<std::string,std::string> accountLabels;
        std::set<CharacterOrder::Key> keys;
        const auto linked=NativeAccounts::Linked();
        for(const auto& account:linked){
            accountLabels.emplace(account.id,account.label.empty()?account.name:account.label);
            for(const auto& [name,b]:account.bindings){
                CharacterIdentity::Address address{account.id,name,b,account.generation};
                const auto source=CharacterIdentity::CaptureProvenance(address);
                const auto unique=ResolveSourceIdentity(source);
                if(!unique||!CharacterIdentity::SameAddress(*unique,address))continue;
                const CharacterOrder::Key key{account.id,name};keys.insert(key);candidates.emplace(key,address);
            }
        }
        for(const auto& key:OrderedCharacterKeys(keys)){
            const auto& address=candidates.at(key);
            const auto source=CharacterIdentity::CaptureProvenance(address);
            const auto it=std::find_if(draft.associations.begin(),draft.associations.end(),
                [&](const auto& p){return p.SameEvidence(source);});
            const bool selected=it!=draft.associations.end();
            ImGui::PushID(address.account.c_str());ImGui::PushID(std::to_string(address.binding.id).c_str());
            const auto prefix=linked.size()>1?accountLabels.at(address.account)+" / ":std::string{};
            if(SelectProfessionCharacterEntry("##association",selected,address.binding.evidence.profession,address.name,
                prefix.empty()?nullptr:prefix.c_str(),ImGuiSelectableFlags_DontClosePopups,address.account)){
                if(selected)draft.associations.erase(it);
                else Fashion::AssociateVerified(draft,source);
            }
            ImGui::PopID();ImGui::PopID();
        }
        // Archived or ambiguous associations remain removable. Their labels
        // cannot create a new association through the current roster.
        for(size_t i=0;i<draft.associations.size();){
            const auto& p=draft.associations[i];
            if(ResolveSourceIdentity(p)){++i;continue;}
            ImGui::PushID(static_cast<int>(i));
            const bool remove=ImGui::Selectable((p.name+" (unavailable)###unresolved_association").c_str(),true,ImGuiSelectableFlags_DontClosePopups);
            ImGui::PopID();
            if(remove)draft.associations.erase(draft.associations.begin()+i);else ++i;
        }
        ImGui::EndCombo();
    }
}
static std::vector<std::string> CurrentFashionNames(const AlterEgo::Fashion::Look& look){
    std::vector<std::string> names;
    for(const auto& p:look.associations)if(const auto a=ResolveSourceIdentity(p))names.push_back(a->name);
    if(look.source_identity)if(const auto a=ResolveSourceIdentity(*look.source_identity))names.push_back(a->name);
    return names;
}
static void RenderFashionDialog() {
    if (!g_FashionDialog) return;
    // Fixed initial dimensions, user-resizable thereafter. Do not combine
    // an appearing-only width with AlwaysAutoResize: that made the window
    // shrink and jump as optional character/duplicate fields changed.
    ImGui::SetNextWindowSize(ImVec2(495, 350), ImGuiCond_Appearing);
    ImGui::SetNextWindowSizeConstraints(ImVec2(475, 310), ImVec2(FLT_MAX, FLT_MAX));
    g_FashionDialogFocus.ApplyBeforeBegin();
    bool windowOpen=true;
    if (!AlterEgo::UI::BeginWindow("Save Fashion##ae_fashion", &windowOpen,
        ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    // Identical form vocabulary for creating and editing a saved appearance.
    ImGui::TextUnformatted("Name");
    ImGui::SetNextItemWidth(-1);
    AlterEgo::UI::InputTextWithHint("##fashion_name", "Fashion Template Name",
                             g_FashionName, sizeof(g_FashionName));
    ImGui::TextUnformatted("Armor weight");
    ImGui::SetNextItemWidth(150);
    AlterEgo::UI::Combo("##fashion_weight", &g_FashionWeightIndex, kFashionWeights, IM_ARRAYSIZE(kFashionWeights));
    ImGui::TextUnformatted("Notes");
    AlterEgo::UI::InputTextMultiline("##fashion_notes", g_FashionNotes, sizeof(g_FashionNotes), ImVec2(-1, 60));
    if (ImGui::CollapsingHeader("Associated characters")) {
        RenderFashionAssociations(g_FashionDraft);
        ImGui::TextDisabled("Optional text labels (not verified character links).");
        ImGui::SetNextItemWidth(-1);
        AlterEgo::UI::InputText("##fashion_chars", g_FashionCharacterNames, sizeof(g_FashionCharacterNames));
    }
    if (!g_FashionDraft.complete)
        ImGui::TextDisabled("Equipment capture may omit outfit and visibility settings.");
    const int duplicate = !g_FashionEditing
        ? g_FashionLibrary.FindExact(g_FashionDraft.wardrobe_link) : -1;
    if (duplicate >= 0) {
        ImGui::TextWrapped("Same appearance saved as '%s'.", g_FashionLibrary.looks[duplicate].name.c_str());
        if (RenderActionButton("Select Existing")) {
            if(SelectExistingFashion(duplicate,g_FashionDraft))
                g_FashionDialog=false;
            else g_FashionError="Could not save character association.";
        }
        ImGui::SameLine();
    }
    // Shared standard-height action buttons, consistent with the existing
    // Save to Library dialog. Keep the duplicate action explicit.
    const char* saveLabel = duplicate >= 0 ? "Save Separate Copy" : "Save";
    const bool dirty=FashionFormSnapshot()!=g_FashionFormBaseline;
    const int current=g_FashionEditing?AlterEgo::RecordSelection::ById(g_FashionLibrary.looks,g_FashionEditBaseline.id):-1;
    const bool stale=g_FashionEditing&&(current<0||AlterEgo::Fashion::ToJson(g_FashionLibrary.looks[current])!=AlterEgo::Fashion::ToJson(g_FashionEditBaseline));
    const bool named=std::string(g_FashionName).find_first_not_of(" \t\r\n")!=std::string::npos;
    const bool ready=AlterEgo::EditorLifecycle::CanSave(named&&!stale,dirty,!g_FashionEditing);
    const auto action=AlterEgo::UI::SaveCancel("fashion_edit",ready,saveLabel,
        stale?"Saved Fashion changed. Cancel and reopen.":!named?"Enter a name.":"No changes to save.",
        false,g_FashionError.c_str());
    if(action==AlterEgo::UI::EditAction::Save) {
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
        if (g_FashionEditing) {
            ok = g_FashionLibrary.UpdateDraft(g_FashionEditBaseline,next);
            if(ok)g_FashionSelectedId=g_FashionEditBaseline.id;
        } else {
            int added = g_FashionLibrary.Add(std::move(next));
            ok = added >= 0;
            if(ok)g_FashionSelectedId=g_FashionLibrary.looks[added].id;
        }
        if (ok) {
            NavigateToLibraryCollection(1);
            g_FashionDialog = false;
            if (APIDefs) APIDefs->GUI_SendAlert("Fashion saved.");
        } else g_FashionError = g_FashionLibrary.error.empty()?"Could not save. Check the name and library file permissions.":g_FashionLibrary.error;
    }
    if(g_FashionDialog&&AlterEgo::UI::DiscardRequested("Discard Fashion changes?",
        !windowOpen||action==AlterEgo::UI::EditAction::Cancel||AlterEgo::UI::EditorEscape(),dirty))g_FashionDialog=false;
    ImGui::End();
}

static void RenderFashionSlot(const char* label, uint16_t skinID, const uint16_t* dyeIDs = nullptr) {
    ImGui::PushID(label);
    ImGui::TextUnformatted(label);
    ImGui::SameLine(126);
    const auto skin = skinID ? AlterEgo::GW2API::GetSkinInfo(skinID) : nullptr;
    if (!skinID) ImGui::TextDisabled("(none)");
    else {
        if (skin && !skin->icon_url.empty()) {
            const auto* tex = AlterEgo::IconManager::GetOrRequestIcon(AlterEgo::ArtworkRequest::SkinKey(skinID),skin->icon_url);
            if (tex && tex->Resource) {
                ImGui::Image(tex->Resource, ImVec2(22, 22));
                AlterEgo::UI::EntityContext("##fashion_skin_icon",[&]{CopyEntityMenu(skin->name,AlterEgo::LINK_SKIN,skinID);});
                ImGui::SameLine();
            }
        }
        if (skin && !skin->name.empty()) ImGui::TextUnformatted(skin->name.c_str());
        else ImGui::Text("Skin #%u", skinID);
        AlterEgo::UI::EntityContext("##fashion_skin_name",[&]{CopyEntityMenu(skin?skin->name:std::string{},AlterEgo::LINK_SKIN,skinID);});
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
            const auto dye = id > 1 ? AlterEgo::GW2API::GetDyeColor(id) : nullptr;
            int r = dye ? dye->r : 65, g = dye ? dye->g : 65, b = dye ? dye->b : 65;
            if (dye && skin && i < (int)skin->dye_slot_materials.size()) {
                const auto& mat = skin->dye_slot_materials[i];
                if (mat == "cloth") { r = dye->cloth_r; g = dye->cloth_g; b = dye->cloth_b; }
                else if (mat == "leather") { r = dye->leather_r; g = dye->leather_g; b = dye->leather_b; }
                else if (mat == "metal") { r = dye->metal_r; g = dye->metal_g; b = dye->metal_b; }
            }
            ImGui::ColorButton("##fashion_dye", ImVec4(r/255.f, g/255.f, b/255.f, 1.f),
                ImGuiColorEditFlags_NoDragDrop | ImGuiColorEditFlags_NoTooltip, ImVec2(17, 17));
            if(ImGui::IsItemHovered())AlterEgo::UI::Hint(AlterEgo::TooltipCore::DyeName(id,dye?dye->name:std::string{}).c_str());
            ImGui::PopID();
        }
        ImGui::Unindent(126);
    }
    ImGui::PopID();
}

// Shared actions for the detail toolbar and the list context menu.
static void CopyFashionWardrobeLink(const AlterEgo::Fashion::Look& look) {
    CopyToClipboard(look.wardrobe_link);
    if (APIDefs) APIDefs->GUI_SendAlert("Wardrobe link copied.");
}

static bool ReadFashionUsers(const std::string& id,std::map<std::string,std::string>& users){
    nlohmann::json snapshot;AlterEgo::Composable::Document doc;std::string error;
    if(!AlterEgo::GW2API::GetComposableLoadouts(snapshot)||!AlterEgo::Composable::FromJson(snapshot,doc,error))return false;
    users=AlterEgo::Composable::FashionUsers(doc,id);return true;
}
static void RequestFashionDelete(const AlterEgo::Fashion::Look& look){
    if(!ReadFashionUsers(look.id,g_FashionDeleteUsers)){
        if(APIDefs)APIDefs->GUI_SendAlert("Loadout references unavailable; cannot review deletion.");
        return;
    }
    g_FashionDeleteTarget=look;g_FashionDeletePending=true;g_FashionDeleteError.clear();
}
enum class FashionAction {Edit,Rename,Duplicate,Copy,Preview,Delete};
static void RunFashionAction(FashionAction action,const AlterEgo::Fashion::Look& look){
    if(action==FashionAction::Copy){CopyFashionWardrobeLink(look);return;}
    if(action==FashionAction::Preview){if(g_PieUIPresent.load())PieUI_PreviewChatLink(look.wardrobe_link);return;}
    if(LibraryDraftOpen()){if(APIDefs)APIDefs->GUI_SendAlert("Finish the current Library editor first.");return;}
    const int index=AlterEgo::RecordSelection::ById(g_FashionLibrary.looks,look.id);
    if(index<0)return;
    switch(action){
    case FashionAction::Edit:{
        auto draft=look;AlterEgo::DecodedWardrobeLink w{};
        if(draft.weight=="Unknown"&&AlterEgo::Fashion::DecodeValid(draft.wardrobe_link,w))draft.weight=FashionWeightFromSkins(w);
        StartFashionEdit(draft,index);break;
    }
    case FashionAction::Rename:
    case FashionAction::Duplicate:
        g_FashionNameTarget=g_FashionLibrary.looks[index];
        g_FashionNameAction.Start(look.name,action==FashionAction::Duplicate);break;
    case FashionAction::Delete:RequestFashionDelete(g_FashionLibrary.looks[index]);break;
    default:break;
    }
}
static void FashionCreationMenu(){
    using namespace AlterEgo::UI;
    const bool ready=!LibraryDraftOpen();
    const char* reason="Finish the current Library editor first.";
    if(MenuAction("New",MenuIcon::New,ready,reason)){
        g_FashionNewInput.clear();g_FashionNewError.clear();g_FashionNewPending=true;
    }
    if(MenuAction("Paste",MenuIcon::Paste,ready,reason)){
        const char* text=ImGui::GetClipboardText();size_t n=0;
        if(text)while(n<4096&&text[n])++n;
        if(!text||n==0||n==4096||!StartFashionImport(std::string(text,n),"")){
            if(APIDefs)APIDefs->GUI_SendAlert("Clipboard does not contain a valid Wardrobe Template link.");
        }
    }
}
static void RenderFashionCreationDialog(){
    using namespace AlterEgo::UI;
    if(g_FashionNewPending){g_FashionNewPending=false;ImGui::OpenPopup("New Fashion##wardrobe");}
    if(!ImGui::BeginPopupModal("New Fashion##wardrobe",nullptr,ImGuiWindowFlags_AlwaysAutoResize))return;
    ImGui::TextWrapped("Enter a Guild Wars 2 Wardrobe Template chat link.");
    ImGui::SetNextItemWidth(420.f);InputText("Wardrobe link",g_FashionNewInput);
    const auto action=SaveCancel("new_fashion",!LibraryDraftOpen()&&!g_FashionNewInput.empty()&&g_FashionNewInput.size()<4096,"Continue",
        "Enter a valid Wardrobe Template link.",false,g_FashionNewError.c_str());
    if(action==EditAction::Save){
        if(StartFashionImport(g_FashionNewInput,"")){g_FashionNewInput.clear();ImGui::CloseCurrentPopup();}
        else g_FashionNewError="Invalid or truncated Wardrobe Template chat link.";
    }else if(DiscardRequested("Discard Wardrobe link?",action==EditAction::Cancel||EditorEscape(),!g_FashionNewInput.empty())){
        g_FashionNewInput.clear();ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}
static void RenderFashionLibrary() {
    // Collection-wide actions also appear in the native context menus.
    // Keep the shared search/filter toolbar inside the collection sidebar.
    const char* filters[] = {"All weights", "Light", "Medium", "Heavy", "Any", "Unknown"};
    float avail = ImGui::GetContentRegionAvail().x;
    const AlterEgo::UI::SplitPanel split(g_FashionListWidth,220.f,130.f,220.f);
    AlterEgo::UI::BeginChild("##fashion_list", ImVec2(split.left, 0), true);
    g_FashionFilter = std::max(0, std::min(g_FashionFilter, IM_ARRAYSIZE(filters)-1));
    AlterEgo::UI::SearchToolbar("fashion", "Search looks / characters...", g_FashionSearch,
        [&] {
            AlterEgo::UI::FilterChips chips;
            if(g_FashionFilter) chips.push_back({"weight",filters[g_FashionFilter],[&] {g_FashionFilter=0;}});
            return chips;
        },
        [&] { AlterEgo::UI::FilterChoice("Armor weight", g_FashionFilter, filters, IM_ARRAYSIZE(filters)); },
        [&] { g_FashionFilter = 0; });
    ImGui::Separator();
    // Defer destructive actions until the details pane owns the confirmation popup.
    bool selectedVisible=false;
    if (g_FashionLibrary.looks.empty())
        ImGui::TextWrapped("No saved fashion. Capture an Equipment tab or import a Wardrobe Template.");
    const auto searchWords=AlterEgo::Search::Words(g_FashionSearch);
    for (int i = 0; i < (int)g_FashionLibrary.looks.size(); ++i) {
        const auto& look = g_FashionLibrary.looks[i];
        if(!searchWords.empty()&&!AlterEgo::Fashion::MatchesSearch(look,searchWords,CurrentFashionNames(look)))continue;
        if (g_FashionFilter && look.weight != filters[g_FashionFilter]) continue;
        ImGui::PushID(look.id.c_str());
        const auto label=look.name+"###fashion_row";
        const bool clicked=ImGui::Selectable(label.c_str(),look.id==g_FashionSelectedId);
        if((clicked||(!g_FashionDialog&&ImGui::IsItemClicked(ImGuiMouseButton_Right)))&&g_FashionSelectedId!=look.id){MarkNavigation();g_FashionSelectedId=look.id;}
        if(look.id==g_FashionSelectedId)selectedVisible=true;
        if (ImGui::BeginPopupContextItem("##fashion_actions")) {
            using namespace AlterEgo::UI;
            const bool ready=!LibraryDraftOpen();
            const char* reason="Finish the current Library editor first.";
            FashionCreationMenu();ImGui::Separator();
            if(MenuAction("Edit",MenuIcon::Edit,ready,reason))RunFashionAction(FashionAction::Edit,look);
            if(MenuAction("Rename",MenuIcon::Rename,ready,reason))RunFashionAction(FashionAction::Rename,look);
            if(MenuAction("Duplicate",MenuIcon::Duplicate,ready,reason))RunFashionAction(FashionAction::Duplicate,look);
            ImGui::Separator();CopyNameMenuItem(look.name,"Copy Name",true);
            if(MenuAction("Copy Wardrobe Link",MenuIcon::Copy))RunFashionAction(FashionAction::Copy,look);
            ImGui::Separator();
            if(MenuAction("Preview",MenuIcon::Preview,g_PieUIPresent.load(),"Requires the existing PieOrCake UI integration."))RunFashionAction(FashionAction::Preview,look);
            ImGui::Separator();
            if(MenuAction("Delete",MenuIcon::Delete,ready,reason))RunFashionAction(FashionAction::Delete,look);
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    if(ImGui::BeginPopupContextWindow("##fashion_empty_actions",ImGuiPopupFlags_MouseButtonRight|ImGuiPopupFlags_NoOpenOverItems)){
        FashionCreationMenu();ImGui::EndPopup();
    }
    ImGui::EndChild();
    if(split.Divider("##fashion_split",g_FashionListWidth))SaveSettings();
    AlterEgo::UI::BeginChild("##fashion_detail", ImVec2(0, 0), true);
    const int selectedIndex=AlterEgo::RecordSelection::ById(g_FashionLibrary.looks,g_FashionSelectedId);
    if(selectedIndex<0){g_FashionSelectedId.clear();ImGui::TextDisabled("Select a saved appearance.");}
    else{
        auto look=g_FashionLibrary.looks[selectedIndex];
        if(!selectedVisible)ImGui::TextDisabled("Selected appearance is outside current filters.");
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
                if (resolved != "Unknown") look.weight = resolved;
            }
        }
        ImGui::TextWrapped("%s",look.name.c_str());
        ImGui::TextDisabled("%s | %s", look.weight.c_str(),
            look.complete ? "Wardrobe Template" : "Equipment-derived (partial)");
        using namespace AlterEgo::UI;
        const bool ready=!LibraryDraftOpen();
        const char* reason="Finish the current Library editor first.";
        if(AvailableActionButton("Edit##fashion",ButtonRole::Standard,ready,reason))RunFashionAction(FashionAction::Edit,look);
        SameLineIfActionFits("Rename");
        if(AvailableActionButton("Rename##fashion",ButtonRole::Standard,ready,reason))RunFashionAction(FashionAction::Rename,look);
        SameLineIfActionFits("Duplicate");
        if(AvailableActionButton("Duplicate##fashion",ButtonRole::Standard,ready,reason))RunFashionAction(FashionAction::Duplicate,look);
        SameLineIfActionFits("Copy");
        if(RenderActionButton("Copy##fashion"))RunFashionAction(FashionAction::Copy,look);
        if(ImGui::IsItemHovered())ImGui::SetTooltip("Copy Wardrobe Link");
        SameLineIfActionFits("Preview");
        if(AvailableActionButton("Preview##fashion",ButtonRole::Standard,g_PieUIPresent.load(),"Requires the existing PieOrCake UI integration."))RunFashionAction(FashionAction::Preview,look);
        SameLineIfActionFits("Delete");
        if(AvailableActionButton("Delete##fashion",ButtonRole::Destructive,ready,reason))RunFashionAction(FashionAction::Delete,look);
        ImGui::Separator();
        AlterEgo::UI::BeginChild("##fashion_appearance",ImVec2(0,0),false);
        if (!look.notes.empty()) { ImGui::Spacing(); ImGui::TextWrapped("%s", look.notes.c_str()); }
        const std::string assocLabel="Associated characters ("+std::to_string(look.associations.size())+")";
        if(ImGui::TreeNode(assocLabel.c_str())){
            std::set<AlterEgo::CharacterOrder::Key> keys;
            std::vector<std::string> unavailable;
            for(const auto& p:look.associations){
                const auto address=ResolveSourceIdentity(p);
                if(address)keys.emplace(address->account,address->name);
                else unavailable.push_back(p.name);
            }
            const auto accounts=AlterEgo::NativeAccounts::Linked();
            for(const auto& key:OrderedCharacterKeys(keys)){
                std::string label=key.second;
                if(accounts.size()>1)for(const auto& a:accounts)if(a.id==key.first){label+=" ("+(a.label.empty()?a.name:a.label)+")";break;}
                ImGui::BulletText("%s",label.c_str());
                ImGui::PushID(key.first.c_str());ImGui::PushID(key.second.c_str());
                NameOptionContext(key.second);ImGui::PopID();ImGui::PopID();
            }
            for(const auto& name:unavailable)ImGui::TextDisabled("%s (unavailable)",name.c_str());
            if(look.associations.empty())ImGui::TextDisabled("(none)");
            for(const auto& name:look.characters)ImGui::TextDisabled("%s (label only)",name.c_str());
            if(!look.source_character.empty())ImGui::TextDisabled("Original: %s, equipment %d",
                look.source_character.c_str(),look.source_tab);
            ImGui::TreePop();
        }
        ImGui::Separator();
        if (decoded) {
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
                AlterEgo::UI::EntityContext("##fashion_outfit",[&]{CopyEntityChatLink(AlterEgo::LINK_OUTFIT,w.outfit_id);});
                ImGui::Indent(126);
                for (int i = 0; i < 4; ++i) {
                    if (i) ImGui::SameLine(0, 4);
                    ImGui::PushID(100+i);
                    int id = w.outfit_dyes[i];
                    const auto dye = id > 1 ? AlterEgo::GW2API::GetDyeColor(id) : nullptr;
                    ImGui::ColorButton("##outfit_dye", ImVec4(dye ? dye->cloth_r/255.f : .3f,
                        dye ? dye->cloth_g/255.f : .3f, dye ? dye->cloth_b/255.f : .3f, 1),
                        ImGuiColorEditFlags_NoTooltip, ImVec2(17,17));
                    if(ImGui::IsItemHovered())AlterEgo::UI::Hint(AlterEgo::TooltipCore::DyeName(id,dye?dye->name:std::string{}).c_str());
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
        }
        ImGui::EndChild();
    }
    ImGui::EndChild();
    RenderFashionCreationDialog();
    const int nameIndex=g_FashionNameTarget?AlterEgo::RecordSelection::ById(g_FashionLibrary.looks,g_FashionNameTarget->id):-1;
    const bool nameValid=nameIndex>=0&&AlterEgo::Fashion::ToJson(g_FashionLibrary.looks[nameIndex])==AlterEgo::Fashion::ToJson(*g_FashionNameTarget);
    AlterEgo::UI::RenderNameAction("Name Fashion##fashion",g_FashionNameAction,nameValid,
        g_FashionNameAction.duplicate?"Create an independent appearance. Existing Loadout references stay with the original.":"Only the name will change.",
        [&](const std::string& name,bool duplicate){
            if(!g_FashionNameTarget)return false;
            if(duplicate){
                const int added=g_FashionLibrary.DuplicateDraft(*g_FashionNameTarget,name);
                if(added<0)return false;
                g_FashionSelectedId=g_FashionLibrary.looks[added].id;
            }else{
                auto copy=*g_FashionNameTarget;copy.name=name;
                if(!g_FashionLibrary.UpdateDraft(*g_FashionNameTarget,copy))return false;
                g_FashionSelectedId=copy.id;
            }
            g_FashionNameTarget.reset();MarkNavigation();return true;
        });
    if(g_FashionDeletePending){g_FashionDeletePending=false;ImGui::OpenPopup("Delete appearance##fashion");}
    const char* deleteLabel=g_FashionDeleteUsers.empty()?"Delete":"Delete & Detach";
    if(AlterEgo::UI::BeginConfirmationModal("Delete appearance##fashion",deleteLabel)){
        if(g_FashionDeleteTarget){
            const auto target=*g_FashionDeleteTarget;
            const int index=AlterEgo::RecordSelection::ById(g_FashionLibrary.looks,target.id);
            const bool valid=index>=0&&AlterEgo::Fashion::ToJson(g_FashionLibrary.looks[index])==AlterEgo::Fashion::ToJson(target);
            ImGui::TextWrapped("Delete '%s' from the Fashion Library?",target.name.c_str());
            if(!g_FashionDeleteUsers.empty()){
                ImGui::TextWrapped("This appearance will be detached from %d Loadout(s). Builds and equipment will remain unchanged.",static_cast<int>(g_FashionDeleteUsers.size()));
                AlterEgo::UI::BeginChild("##fashion_delete_users",ImVec2(0,std::min(140.f,ImGui::GetTextLineHeightWithSpacing()*static_cast<float>(g_FashionDeleteUsers.size())+8.f)),true);
                for(const auto& user:g_FashionDeleteUsers)ImGui::BulletText("%s",user.second.c_str());
                ImGui::EndChild();
            }
            if(!valid)ImGui::TextWrapped("Saved Fashion changed. Cancel and review again.");
            const auto action=AlterEgo::UI::ConfirmCancel("fashion_delete",valid,deleteLabel,"Fashion changed; review again.",g_FashionDeleteError.c_str());
            if(action==AlterEgo::UI::EditAction::Save){
                if(g_FashionLibrary.RemoveDraft(target,g_FashionDeleteUsers)){
                    AlterEgo::GW2API::LoadBuildLibrary();
                    if(g_FashionSelectedId==target.id)g_FashionSelectedId.clear();
                    g_FashionDeleteTarget.reset();ImGui::CloseCurrentPopup();
                }else g_FashionDeleteError=g_FashionLibrary.error;
            }
            else if(action==AlterEgo::UI::EditAction::Cancel||AlterEgo::UI::EditorEscape()){g_FashionDeleteTarget.reset();ImGui::CloseCurrentPopup();}
        }else if(RenderActionButton("Cancel##delete_fashion"))ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}
