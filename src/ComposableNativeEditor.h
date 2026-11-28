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
    if(AlterEgo::Composable::IsCompanionKind(kind))return AlterEgo::Composable::CompanionProfession(kind);
    if(kind!="skills")return {};
    const auto stored=AlterEgo::Composable::StoredSkillsProfession(data);
    if(!stored.empty())return stored;
    const auto& doc=ComponentUISnapshot();
    if(g_ComponentEditorMode=="copy_for_loadout"){
        const auto* owner=ComponentUIFindLoadout(doc,g_ComponentEditorPrivateLoadoutId);
        if(owner){
            const auto* base=ComponentUIFind(doc,"base_build",owner->at("refs").value("base_build",std::string()));
            if(base)return base->at("data").value("profession",std::string());
        }
    }
    return ComponentUIProfession(kind,data,id);
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
    if(g_ComponentEditorMode!="edit"&&g_ComponentEditorMode!="copy_for_loadout")return {common,found};
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

static void ComponentNativeSkillBar(ComponentJson& data, const char* key,
                                    const char* title,
                                    const std::string& profession,
                                    const std::vector<uint32_t>& specs,
                                    bool allowAnySpec) {
    namespace SE=AlterEgo::SkillEdit;
    const bool aquatic=std::string(key)=="aquatic_skills";
    ImGui::PushID(key);
    if(ImGui::TreeNodeEx("##bar",ImGuiTreeNodeFlags_DefaultOpen,"%s",title)){
        const auto evidence=SE::ReadPaletteEvidence(data);
        const bool missing=!data.contains(key);
        if(missing&&!SE::HasPaletteBarEvidence(evidence,aquatic)){
            if(RenderActionButton((std::string("Add ")+title+"##bar").c_str()))
                data[key]={{"heal",0},{"utilities",ComponentJson::array({0,0,0})},{"elite",0}};
        }else if(!missing&&!data[key].is_object()){
            ImGui::TextDisabled("Unsupported skill format; existing value preserved.");
        }else{
            const auto slots=SE::JsonPaletteDisplayBar(data.value(key,ComponentJson::object()),evidence,aquatic);
            RenderSkillEditorBar(slots,ICON_SIZE,profession,specs,allowAnySpec,aquatic,true,[&](int slot,uint32_t next){
                SE::SetJsonSlotWithPaletteEvidence(data,aquatic,slot,next);
            });
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

static void ComponentNativeSpecializations(ComponentJson& data,const std::string& profession){
    namespace SE=AlterEgo::SkillEdit;
    const auto lines=SE::JsonSpecializationLines<AlterEgo::SpecLine>(data);
    AlterEgo::GW2API::FetchSpecDetailsAsync({lines[0].spec_id,lines[1].spec_id,lines[2].spec_id});
    if(!profession.empty())AlterEgo::GW2API::FetchProfessionInfoAsync(profession);
    if(data.contains("specializations")&&!data.at("specializations").is_array()){
        ImGui::TextDisabled("Unsupported specialization data; preserved.");return;
    }
    RenderSpecializationGrid(lines.data(),profession,true,[&](int slot,uint32_t next){
        SE::SetJsonSpecialization(data,slot,next,[](uint32_t id){
            const auto info=AlterEgo::GW2API::GetSpecInfo(id);return info&&info->elite;
        });
    },[&](int slot,int tier,int value){SE::SetJsonTrait(data,slot,tier,value);});
}

static void ComponentNativeProfessionPair(ComponentJson& data,const char* key,
                                           const char* label,bool legend,bool eligible,
                                           const std::vector<AlterEgo::SkillEdit::EliteContext>& contexts) {
    if(!eligible&&!data.contains(key))return;
    if(!ImGui::TreeNodeEx(key,ImGuiTreeNodeFlags_DefaultOpen,"%s",label))return;
    if(data.contains(key)&&!data[key].is_array()){
        ImGui::TextDisabled("Unrecognized array format; preserved.");ImGui::TreePop();return;
    }
    ComponentJson values=data.contains(key)?data[key]:ComponentJson::array();
    auto set=[&](size_t i,uint32_t value){
        AlterEgo::SkillEdit::SetCompanionPayloadSlot(values,i,value);data[key]=values;
    };
    if(eligible){
        auto slots=AlterEgo::SkillEdit::JsonCompanionSlots(values,legend?255u:UINT32_MAX);
        if(const auto changed=RenderProfessionPairSection(legend,slots,true,contexts))
            for(size_t i=0;i<slots.values.size();++i)if(changed&(1u<<i))set(i,slots.values[i]);
    }else ImGui::TextDisabled("Not applicable to this profession; exact fields preserved.");
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

static AlterEgo::GearEdit::SocketCapacity ComponentInfusionCapacity(const ComponentJson& part,const std::string& slot,const std::string& profession){
    const auto exact=part.find("item_id");
    const uint32_t id=exact!=part.end()&&exact->is_number_integer()&&*exact>0&&*exact<=UINT32_MAX?exact->get<uint32_t>():0;
    const auto type=part.find("weapon_type");
    return ResolveGearInfusionCapacity(slot,profession,type!=part.end()&&type->is_string()?type->get<std::string>():std::string{},id);
}
static void ComponentNativeInfusionSlots(ComponentJson& part,const std::string& slot,const std::string& profession) {
    using namespace AlterEgo;
    using namespace AlterEgo::GearEdit;
    BuildGearSlot value;
    if(!ReadComponentField(part,CopyField::Infusions,false,value,false)){
        ImGui::TextDisabled("Unsupported infusion fields; preserved.");return;
    }
    bool changed=false;
    static std::string query;
    static uint32_t input=0;
    for(size_t i=0;i<InfusionPositionCount(value);++i){
        ImGui::PushID(static_cast<int>(i));
        const auto req=InfusionPositionAt(value,i);
        const auto label="Slot "+std::to_string(i+1)+": "+InfusionPositionLabel(req);
        if(RenderInfusionPositionSelect(label,false)){
            input=req.exactId;query.clear();ImGui::OpenPopup("##component_infusion_picker");
        }
        bool remove=false;
        if(ImGui::BeginPopupContextItem("##component_infusion_context")){
            const auto item=req.exactId?GW2API::GetItemInfo(req.exactId):nullptr;
            CopyEntityMenu(item?item->name:std::string{},AlterEgo::LINK_ITEM,req.exactId);
            if(req.exactId)ImGui::Separator();
            if(AlterEgo::UI::MenuAction("Clear",AlterEgo::UI::MenuIcon::Delete))remove=true;
            ImGui::EndPopup();
        }
        remove=RenderInfusionRemoveButton()||remove;
        ImGui::SetNextWindowSize(ImVec2(340,0),ImGuiCond_Appearing);
        if(ImGui::BeginPopup("##component_infusion_picker")){
            AlterEgo::UI::SearchField("component_infusion_search","Search...",query);
            RenderInfusionPositionPicker(InfusionPositionAt(value,i),input,query.c_str(),180,
                [&](const auto& next){changed=SetInfusionPosition(value,i,next)||changed;});
            ImGui::EndPopup();
        }
        if(remove){
            changed=RemoveInfusion(value,i)||changed;
            ImGui::PopID();break;
        }
        ImGui::PopID();
    }
    const auto socketCapacity=ComponentInfusionCapacity(part,slot,profession);
    const int capacity=socketCapacity.limit;
    if(capacity>0&&!socketCapacity.verified)ImGui::TextDisabled("Socket capacity unverified (maximum %d).",capacity);
    if(InfusionPositionCount(value)>static_cast<size_t>(capacity))
        ImGui::TextDisabled("Imported requirements exceed known capacity; preserved.");
    if(InfusionPositionCount(value)<static_cast<size_t>(capacity)&&RenderActionButton("+ Add infusion##component"))
        changed=AddInfusionPosition(value,capacity)||changed;
    if(!value.infusion.empty())ImGui::TextDisabled("Legacy infusion: %s",value.infusion.c_str());
    if(changed)WriteComponentField(part,CopyField::Infusions,false,value);
}

// Reuse the same database-backed upgrade catalogs as the traditional
// customization panel. The text/ID pair is updated together; unfamiliar
// imported items remain editable via the exact-ID fallback fields.
static std::string g_ComponentCatalogSearch;
static std::string g_ComponentStatSearch;
// Same current-field card as Gear Customize. The standalone popup owns
// its catalog and retains native JSON-only mutation and clipboard actions.
static bool BeginNativeGearField(const char* popup,const std::string& label,uint32_t icon=0){
    const auto pos=ImGui::GetCursorScreenPos();
    const float width=std::max(1.f,ImGui::GetContentRegionAvail().x);
    const float iconSize=icon?24.f:0.f;
    const float wrap=std::max(1.f,width-12.f-(iconSize?iconSize+6.f:0.f));
    const float height=std::max(30.f,std::max(iconSize,ImGui::CalcTextSize(label.c_str(),nullptr,false,wrap).y)+8.f);
    const bool selected=ImGui::IsPopupOpen(popup);
    if(RenderGearFieldCard("##native_field",pos,width,height,selected))ImGui::OpenPopup(popup);
    auto* draw=ImGui::GetWindowDrawList();
    if(icon){
        const auto info=AlterEgo::GW2API::GetItemInfo(icon);
        auto* texture=info?AlterEgo::IconManager::GetOrRequestIcon(icon,info->icon_url):nullptr;
        const ImVec2 at(pos.x+4.f,pos.y+(height-iconSize)*.5f);
        if(texture&&texture->Resource)draw->AddImage(texture->Resource,at,ImVec2(at.x+iconSize,at.y+iconSize));
        else draw->AddRectFilled(at,ImVec2(at.x+iconSize,at.y+iconSize),IM_COL32(55,55,65,220));
    }
    draw->AddText(ImGui::GetFont(),ImGui::GetFontSize(),ImVec2(pos.x+6.f+(iconSize?iconSize+6.f:0.f),pos.y+4.f),
        IM_COL32(220,200,170,255),label.c_str(),nullptr,wrap);
    if(ImGui::IsPopupOpen(popup)){
        const auto display=ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowSize(ImVec2(std::min(360.f,std::max(1.f,display.x-24.f)),std::min(380.f,std::max(1.f,display.y-24.f))),ImGuiCond_Appearing);
    }
    return ImGui::BeginPopup(popup);
}
static void ComponentNativeCatalogChoice(ComponentJson& item,
                                          const char* idField,
                                          const char* nameField,
                                          const char* family) {
    ImGui::PushID(idField);
    const std::string selected = item.value(nameField, std::string());
    const auto storedId = item.find(idField);
    const uint32_t selectedId = storedId != item.end() && storedId->is_number_integer()
        ? storedId->get<uint32_t>() : 0;
    const auto info=selectedId?AlterEgo::GW2API::GetItemInfo(selectedId):nullptr;
    const std::string shown=info&&!info->name.empty()?info->name:!selected.empty()?selected:selectedId?"Item metadata pending":"Choose item";
    if(BeginNativeGearField("##catalog",shown,selectedId)){
        AlterEgo::UI::SearchField("catalog", "Search items...", g_ComponentCatalogSearch);
        if (ImGui::Selectable("(none)")) {
            item.erase(idField);
            item.erase(nameField);ImGui::CloseCurrentPopup();
        }
        const char* kind=std::strcmp(family,"runes")==0?"Rune":std::strcmp(family,"sigils")==0?"Sigil":"Relic";
        RenderGearCatalogChoices(kind,g_ComponentCatalogSearch,selectedId,selected,true,
            [&](uint32_t id,const std::string& name){if(id!=selectedId||!selectedId){item[idField]=id;item[nameField]=name;}g_ComponentCatalogSearch.clear();ImGui::CloseCurrentPopup();});
        ImGui::EndPopup();
    }else if (ImGui::BeginPopupContextItem("##native_upgrade_context")) {
        using AlterEgo::UI::MenuAction;
        using AlterEgo::UI::MenuIcon;
        const bool rune=std::strcmp(family,"runes")==0,sigil=std::strcmp(family,"sigils")==0;
        const auto entity=ItemMenuEntity(rune?"Rune":sigil?"Sigil":"Relic",selectedId,selected);
        const bool entities=RenderGearEntityMenu(entity,{}, {},{}, {},true);
        {
        AlterEgo::UI::MenuSection config("configuration",entities?"Configuration":nullptr,entities);
        const auto field=rune||sigil?GearCopyField::Upgrade:GearCopyField::Relic;
        const bool recorded=(item.contains(idField)||item.contains(nameField))&&
            (!item.contains(idField)||(item[idField].is_number_integer()&&item[idField]>=0&&item[idField]<=UINT32_MAX))&&
            (!item.contains(nameField)||item[nameField].is_string());
        if(MenuAction("Copy",MenuIcon::Copy,recorded)){
            GearClipboard clip;clip.valid=true;clip.field=field;
            clip.slot=rune?"Helm":sigil?"WeaponA1":"Relic";
            if(rune){clip.data.rune_id=selectedId;clip.data.rune=selected;}
            else if(sigil){clip.data.sigil_id=selectedId;clip.data.sigil=selected;}
            else{clip.relicId=selectedId;clip.relicName=selected;}
            g_GearClipboard=std::move(clip);
        }
        const auto& clip=g_GearClipboard;
        const bool compatible=clip.valid&&clip.field==field&&
            (rune?IsArmorSlot(clip.slot):sigil?IsWeaponSlot(clip.slot):clip.slot=="Relic");
        if(MenuAction("Paste",MenuIcon::Paste,compatible)){
            const uint32_t nextId=rune?clip.data.rune_id:sigil?clip.data.sigil_id:clip.relicId;
            const auto& name=rune?clip.data.rune:sigil?clip.data.sigil:clip.relicName;
            if(nextId)item[idField]=nextId;else item.erase(idField);
            if(!name.empty())item[nameField]=name;else item.erase(nameField);
        }
        if (MenuAction("Clear",MenuIcon::Delete,recorded)) { item.erase(idField); item.erase(nameField); }
        } // configuration ID scope
        ImGui::EndPopup();
    }
    ImGui::PopID();
}

static void ComponentNativeStatChoice(ComponentJson& item) {
    ImGui::PushID("##native_stats");
    const uint32_t id = AlterEgo::TooltipCore::FieldId(item,"stat_id");
    const auto info = AlterEgo::GW2API::GetItemStatInfo(id);
    const std::string storedName = item.value("stat_name", std::string());
    const std::string shown = !storedName.empty() ? storedName :
        info ? info->name : id ? "#" + std::to_string(id) : "(choose stats)";
    if(BeginNativeGearField("##stats",shown)){
        AlterEgo::UI::SearchField("stats", "Search stats...", g_ComponentStatSearch);
        if (ImGui::Selectable("(none)")) {
            item.erase("stat_id");
            item.erase("stat_name");
        }
        RenderGearStatChoices(g_ComponentStatSearch.c_str(),id,true,[&](uint32_t next,const std::string& name){
            if(next!=id||!id){item["stat_id"]=next;item["stat_name"]=name;}g_ComponentStatSearch.clear();ImGui::CloseCurrentPopup();
        });
        ImGui::EndPopup();
    }else if(ImGui::BeginPopupContextItem("##native_stat_context")){
        using AlterEgo::UI::MenuAction;
        using AlterEgo::UI::MenuIcon;
        const MenuEntity entity{"Stats",info&&!info->name.empty()?info->name:storedName,{}};
        const bool entities=RenderGearEntityMenu(entity,{}, {},{}, {},true);
        {
        AlterEgo::UI::MenuSection config("configuration",entities?"Configuration":nullptr,entities);
        AlterEgo::BuildGearSlot checked;
        using namespace AlterEgo::GearEdit;
        const bool recorded=ReadComponentField(item,CopyField::Stats,true,checked);
        if(MenuAction("Copy",MenuIcon::Copy,recorded)){
            GearClipboard clip;clip.valid=true;clip.field=GearCopyField::Stats;clip.slot="Helm";clip.data=checked;
            g_GearClipboard=std::move(clip);
        }
        AlterEgo::BuildGearSlot target;
        const bool compatible=g_GearClipboard.valid&&g_GearClipboard.field==GearCopyField::Stats&&
            ReadComponentField(item,CopyField::Stats,true,target,false);
        if(MenuAction("Paste",MenuIcon::Paste,compatible))
            ApplyComponentField(item,CopyField::Stats,true,g_GearClipboard.data,1);
        if(MenuAction("Clear",MenuIcon::Delete,recorded)){item.erase("stat_id");item.erase("stat_name");}
        } // configuration ID scope
        ImGui::EndPopup();
    }
    ImGui::PopID();
}

static void ComponentNativeWeaponChoice(ComponentJson& item,ComponentJson& gear,
                                        const std::string& profession,
                                        const std::string& slot) {
    if(!profession.empty())AlterEgo::GW2API::FetchProfessionPaletteAsync(profession);
    const auto weapons=AlterEgo::GW2API::GetProfessionWeapons(profession);
    if(!profession.empty()&&(!weapons||weapons->empty())){
        ImGui::TextDisabled("Weapon definitions not available.");
        if (RenderActionButton("Fetch weapon data##component"))
            AlterEgo::GW2API::FetchProfessionInfoAsync(profession,true);
        return;
    }
    const std::string current = item.value("weapon_type", std::string());
    auto apply=[&](const std::string& next,bool twoHanded){
        AlterEgo::GearEdit::ApplyComponentWeaponChange(gear,slot,next,twoHanded);
    };
    ImGui::TextUnformatted("Weapon type");
    if(profession.empty())ImGui::TextDisabled("Profession eligibility not verified");
    if(BeginNativeGearField("##native_weapon",current.empty()?"Choose weapon type":WeaponDisplayName(current))){
        static std::string filter;
        if(ImGui::IsWindowAppearing())filter.clear();
        AlterEgo::UI::SearchField("component_weapon_search","Search...",filter);
        if(ImGui::Selectable("(none)",current.empty())&&!current.empty())apply("",false);
        RenderWeaponTypeChoices(profession,slot,current,filter,true,[&](const auto& name,const auto& wd){
            apply(name,wd.two_handed);if(!profession.empty())FetchChosenWeaponMetadata(wd);ImGui::CloseCurrentPopup();
        });
        ImGui::EndPopup();
    }else if(!current.empty())NameOptionContext(WeaponDisplayName(current));
}

// Field-level clipboard adapters use the same typed clipboard and mutation
// functions as the parent Loadout. Exact raw slot copy remains a separate
// same-component/same-position action so extensions cannot cross slot types.
static bool ComponentGearCanPaste(const ComponentJson& part,const std::string& kind,
    const std::string& slot,const GearClipboard& clip,const std::string& profession){
    using namespace AlterEgo::GearEdit;
    if(!clip.valid||!ComponentOwnsField(kind,slot,clip.field))return false;
    if(clip.field==CopyField::Infusions&&ComponentInfusionCapacity(part,slot,profession).limit<=0)return false;
    if(clip.field==GearCopyField::Upgrade&&
       ((IsArmorSlot(slot)!=IsArmorSlot(clip.slot))||(!IsArmorSlot(slot)&&!IsWeaponSlot(slot))))return false;
    AlterEgo::BuildGearSlot checked;
    return ReadComponentField(part,clip.field,IsArmorSlot(slot),checked,false);
}
static GearClipboard ComponentGearCapture(const ComponentJson& part,const std::string& kind,
    const std::string& slot,GearCopyField field){
    using namespace AlterEgo::GearEdit;
    GearClipboard clip;clip.field=field;clip.slot=slot;
    clip.valid=ComponentOwnsField(kind,slot,field)&&ReadComponentField(part,field,IsArmorSlot(slot),clip.data);
    return clip;
}
static bool ComponentGearApply(ComponentJson& part,const std::string& kind,const std::string& slot,
    const std::string& profession,const GearClipboard& clip){
    using namespace AlterEgo::GearEdit;
    if(!ComponentGearCanPaste(part,kind,slot,clip,profession))return false;
    const std::string type=part.contains("weapon_type")&&part["weapon_type"].is_string()?part["weapon_type"].get<std::string>():std::string{};
    const bool weapon=IsWeaponSlot(slot);
    const bool knownTwo=weapon&&!type.empty()&&IsWeaponTwoHanded(profession,type);
    const bool knownWeapon=weapon&&!type.empty();
    int capacity=1;
    bool capacityVerified=false;
    if(clip.field==GearCopyField::Upgrade&&weapon){
        const bool second=clip.data.sigil2_id||!clip.data.sigil2.empty();
        capacity=SigilCapacity(slot,knownWeapon?knownTwo:second);
        if(!capacity)return false;
    }
    if(clip.field==GearCopyField::Infusions){
        const auto sockets=ComponentInfusionCapacity(part,slot,profession);
        capacity=sockets.limit;capacityVerified=sockets.verified;
    }
    return ApplyComponentField(part,clip.field,IsArmorSlot(slot),clip.data,capacity,capacityVerified);
}

// Same canonical slot order and readable details as Components > Inventory.
// Graphical choice controls below reuse the original Loadout's profession,
// stat, item, rune/sigil and relic metadata; all writes target only the draft.
// Read-only presentation action, shared by the editor and component viewer.
// Resolve exact stored names without mutating imported data or inventing IDs.
static bool ComponentStoredNameActions(const ComponentJson& item,bool configuration){
    if(!item.is_object())return false;
    const auto text=[&](const char* field){
        auto it=item.find(field);
        return it!=item.end()&&it->is_string()?it->get<std::string>():std::string{};
    };
    const auto id=[&](const char* field){return AlterEgo::TooltipCore::FieldId(item,field);};
    std::vector<MenuEntity> upgrades;
    AddMenuUpgrade(upgrades,"Rune",id("rune_id"),text("rune"));
    AddMenuUpgrade(upgrades,"Sigil",id("sigil_id"),text("sigil"));
    AddMenuUpgrade(upgrades,"Second sigil",id("sigil2_id"),text("sigil2"));
    const auto infusions=item.find("infusion_ids");
    if(infusions!=item.end()&&infusions->is_array()){
        for(const auto& value:*infusions)if(value.is_number_integer()&&value>0&&value<=UINT32_MAX)
            AddMenuUpgrade(upgrades,"Infusion",value.get<uint32_t>());
    }
    return RenderGearEntityMenu(ItemMenuEntity("Item",id("item_id"),text("item_name")),{},upgrades,{}, {},configuration);
}
static void ComponentNativeGear(ComponentJson& data, const std::string& kind,
                                const std::string& profession) {
    if(data.contains("gear")&&!data.at("gear").is_object()){
        ImGui::TextDisabled("Unrecognized gear data; preserved without alteration.");return;
    }
    static ComponentJson copiedSlot;
    static std::string copiedKind,copiedFrom;
    auto choose=[&](const std::string& slot){
        if(!ComponentNativeSlotAllowed(kind,slot)&&!(data.contains("gear")&&data.at("gear").contains(slot)))return;
        g_ComponentGearSelectedSlot=slot;g_ComponentGearRemovalPending.clear();
        g_ComponentCatalogSearch.clear();g_ComponentStatSearch.clear();
    };
    auto menu=[&](const std::string& slot){
            const bool entities=ComponentStoredNameActions(data["gear"].at(slot),true);
            {
            AlterEgo::UI::MenuSection config("configuration",entities?"Configuration":nullptr,entities);
            auto capture=[&](GearCopyField field){return ComponentGearCapture(data["gear"].at(slot),kind,slot,field);};
            if(AlterEgo::UI::BeginActionMenu("Copy",AlterEgo::UI::MenuIcon::Copy)){
                if(ImGui::MenuItem("Exact slot")){
                    copiedSlot=data["gear"].at(slot);copiedKind=kind;copiedFrom=slot;
                }
                for(const auto& choice:std::array<std::pair<GearCopyField,const char*>,3>{{
                    {GearCopyField::Stats,"Stats"},{GearCopyField::Upgrade,"Rune / Sigils"},{GearCopyField::Infusions,"Infusions"}}}){
                    auto clip=capture(choice.first);
                    if(ImGui::MenuItem(choice.second,nullptr,false,clip.valid))g_GearClipboard=clip;
                }
                ImGui::EndMenu();
            }
            if(AlterEgo::UI::MenuAction("Paste",AlterEgo::UI::MenuIcon::Paste,ComponentGearCanPaste(data["gear"].at(slot),kind,slot,g_GearClipboard,profession)))
                ComponentGearApply(data["gear"][slot],kind,slot,profession,g_GearClipboard);
            if(AlterEgo::UI::MenuAction("Paste Exact Slot",AlterEgo::UI::MenuIcon::Paste,copiedKind==kind&&copiedFrom==slot&&copiedSlot.is_object()))
                data["gear"][slot]=copiedSlot;
            if(AlterEgo::UI::BeginActionMenu("Copy to All Recorded Slots",AlterEgo::UI::MenuIcon::Copy)){
                for(const auto& choice:std::array<std::pair<GearCopyField,const char*>,3>{{
                    {GearCopyField::Stats,"Stats"},{GearCopyField::Upgrade,"Rune / Sigils"},{GearCopyField::Infusions,"Infusions"}}}){
                    auto clip=capture(choice.first);
                    if(ImGui::MenuItem(choice.second,nullptr,false,clip.valid)){
                        for(auto entry=data["gear"].begin();entry!=data["gear"].end();++entry)
                            if(entry.key()!=slot)ComponentGearApply(entry.value(),kind,entry.key(),profession,clip);
                    }
                }
                ImGui::EndMenu();
            }
            if (AlterEgo::UI::MenuAction("Clear Slot",AlterEgo::UI::MenuIcon::Delete)) {
                g_ComponentGearSelectedSlot = slot;
                g_ComponentGearRemovalPending = slot;
            }
            } // configuration ID scope

    };
    ImGui::TextDisabled("Select a slot to customize; right-click for actions.");
    const float width=ImGui::GetContentRegionAvail().x;
    const bool sideBySide=width>=620.f;
    if(sideBySide)AlterEgo::UI::BeginChild("##component_card_list",ImVec2(width*.48f,0),true);
    RenderComponentGearCards(kind,data,profession,choose,menu,g_ComponentGearSelectedSlot);
    if(data.contains("gear")&&data.at("gear").is_object())
        for(const auto& slot:AlterEgo::Composable::Presentation::SortedGearSlots(data.at("gear"))){
            if(std::find(std::begin(kNativeGearSlots),std::end(kNativeGearSlots),slot)!=std::end(kNativeGearSlots))continue;
            ImGui::PushID(slot.c_str());
            if(ImGui::Selectable(slot.c_str(),g_ComponentGearSelectedSlot==slot))choose(slot);
            ImGui::PopID();
        }
    if(sideBySide){ImGui::EndChild();ImGui::SameLine();AlterEgo::UI::BeginChild("##component_slot_editor",ImVec2(0,0),true);}
    else ImGui::Separator();
    if(g_ComponentGearSelectedSlot.empty()){
        ImGui::TextDisabled("Select an equipment slot.");
        if(sideBySide)ImGui::EndChild();return;
    }
    if(!data.contains("gear")||!data.at("gear").contains(g_ComponentGearSelectedSlot)){
        ImGui::Text("%s",GearSlotDisplayName(g_ComponentGearSelectedSlot));
        if(RenderActionButton("Configure slot"))data["gear"][g_ComponentGearSelectedSlot]=ComponentJson::object();
        if(sideBySide)ImGui::EndChild();return;
    }
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
            const uint32_t id = AlterEgo::TooltipCore::FieldId(item,"item_id");
            const auto info = id ? AlterEgo::GW2API::GetItemInfo(id) : nullptr;
            if (info && !info->icon_url.empty()) {
                Texture_t* tex = AlterEgo::IconManager::GetOrRequestIcon(id,info->icon_url);
                if (tex && tex->Resource) {
                    ImGui::Image(tex->Resource, ImVec2(40, 40));
                    ImGui::SameLine();
                }
            }
            if(id)ImGui::TextWrapped("%s",info&&!info->name.empty()?info->name.c_str():"Item metadata pending");
            else ImGui::TextDisabled("No exact item selected");
            if(info&&ImGui::IsItemHovered())RenderSavedItemTooltip(id,AlterEgo::TooltipCore::FieldId(item,"stat_id"));
            if(id)AlterEgo::UI::EntityContext("##native_exact_item", [&]{CopyEntityMenu(info?info->name:std::string{},AlterEgo::LINK_ITEM,id);});
            if (ImGui::TreeNode("##exact_item", "Exact equipment item")) {
                uint32_t exact=id;ImGui::SetNextItemWidth(150);
                if(ImGui::InputScalar("Item ID",ImGuiDataType_U32,&exact)){
                    if(exact!=id){item["item_id"]=exact;item.erase("item_name");}
                }
                ImGui::TreePop();
            }
            ImGui::TextUnformatted("Stats");
            ComponentNativeStatChoice(item);
            RenderSelectedItemStatDetails(id,AlterEgo::TooltipCore::FieldId(item,"stat_id"),false);
            if (weapon)
                ComponentNativeWeaponChoice(item,data["gear"],profession,slot);
            if (ImGui::TreeNode("##advanced_gear_ids", "Advanced: exact stats and weapon type")) {
                ComponentNativeNumber(item, "stat_id", "Stat ID");
                ComponentNativeText(item, "stat_name", "Stat name");
                if(weapon){
                    // Advanced input is a deliberate Enter-confirmed transition,
                    // not a raw mutation that leaves a stale exact item identity.
                    const auto existing=item.find("weapon_type");
                    if(existing!=item.end()&&!existing->is_string())ImGui::TextDisabled("Unknown weapon type representation; retained.");
                    else{
                        auto entered=existing==item.end()?std::string{}:existing->get<std::string>();
                        ImGui::SetNextItemWidth(180);
                        if(AlterEgo::UI::InputText("Weapon type (Enter to apply)",entered,ImGuiInputTextFlags_EnterReturnsTrue)){
                            bool valid=AlterEgo::EquipmentChoice::WeaponEnvironmentAllowed(slot,entered);
                            bool two=IsWeaponTwoHanded(profession,entered);
                            const auto defs=AlterEgo::GW2API::GetProfessionWeapons(profession);
                            if(defs&&!entered.empty()){
                                const auto it=defs->find(entered);
                                valid=valid&&it!=defs->end()&&AlterEgo::EquipmentChoice::WeaponTypeEligible(slot,entered,it->second.mainhand,it->second.offhand,it->second.two_handed);
                                if(it!=defs->end())two=it->second.two_handed;
                            }
                            if(valid)AlterEgo::GearEdit::ApplyComponentWeaponChange(data["gear"],slot,entered,two);
                        }
                        ImGui::TextDisabled("Known profession/hand restrictions apply; unknown definitions are unverified.");
                    }
                }
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
            const auto type=item.value("weapon_type",std::string{});
            const int capacity=AlterEgo::GearEdit::SigilCapacity(slot,type.empty()||IsWeaponTwoHanded(profession,type));
            if(capacity>=2){
                ImGui::TextUnformatted("Sigil 2 (two-handed weapons)");
                ComponentNativeCatalogChoice(item,"sigil2_id","sigil2","sigils");
            }else if(item.contains("sigil2_id")||item.contains("sigil2")){
                ImGui::TextDisabled("Recorded second sigil exceeds known capacity; retained.");
                if(RenderActionButton("Clear second sigil")){item.erase("sigil2_id");item.erase("sigil2");}
            }
            if (ImGui::TreeNode("##advanced_sigils", "Advanced exact sigil IDs and names")) {
                ComponentNativeNumber(item, "sigil_id", "Sigil 1 ID");
                ComponentNativeText(item, "sigil", "Sigil 1 name");
                if(capacity>=2){
                    ComponentNativeNumber(item,"sigil2_id","Sigil 2 ID");
                    ComponentNativeText(item,"sigil2","Sigil 2 name");
                }
                ImGui::TreePop();
            }
        }
        if (kind == "infusions" || water) {
            ComponentNativeInfusionSlots(item,slot,profession);
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
    if(sideBySide)ImGui::EndChild();
}

static bool ComponentInfusionProjection(const ComponentJson& data,AlterEgo::SavedBuild& b) {
    try{
        b.infusion_summary_present=data.value("infusion_summary_present",false);
        b.infusion_ar_total=data.value("infusion_ar_total",0);
        if(data.contains("infusion_stat_totals"))b.infusion_stat_totals=data.at("infusion_stat_totals").get<std::map<std::string,int>>();
        if(data.contains("infusion_totals")){
            if(!data.at("infusion_totals").is_array())return false;
            for(const auto& entry:data.at("infusion_totals"))
                b.infusion_totals.push_back({entry.value("item_id",0u),entry.value("count",0),entry.value("attribute",std::string())});
        }
        if(data.contains("gear")){
            if(!data.at("gear").is_object())return false;
            for(auto it=data.at("gear").begin();it!=data.at("gear").end();++it){
                AlterEgo::BuildGearSlot slot;
                if(!AlterEgo::GearEdit::ReadComponentField(it.value(),AlterEgo::GearEdit::CopyField::Infusions,false,slot,false))return false;
                b.gear.emplace(it.key(),std::move(slot));
            }
        }
        return true;
    }catch(...){return false;}
}
static bool ComponentNativeInfusionRequirements(ComponentJson& data) {
    using namespace AlterEgo;
    SavedBuild projection;
    if(!ComponentInfusionProjection(data,projection)){
        ImGui::TextDisabled("Unsupported imported infusion fields; preserved. Inspect exact stored fields.");
        return false;
    }
    const auto mode=GetInfusionMode(projection);
    ImGui::TextUnformatted(mode==InfusionMode::Slots?"Infusions: By Equipment Slot":
        mode==InfusionMode::Totals?"Infusions: By Totals":"Infusions: Mixed legacy requirements");
    if(mode==InfusionMode::Conflict)
        ImGui::TextWrapped("Choose which existing requirements are authoritative; neither is discarded until Save.");
    if(mode!=InfusionMode::Slots&&RenderActionButton("Use equipment slots...##component"))
        ImGui::OpenPopup("##component_slot_mode");
    if(mode!=InfusionMode::Totals){
        if(mode==InfusionMode::Conflict)ImGui::SameLine();
        if(RenderActionButton("Use totals...##component"))ImGui::OpenPopup("##component_total_mode");
    }
    if(ImGui::BeginPopup("##component_slot_mode")){
        ImGui::TextWrapped("Aggregate targets will be removed on Save. No automatic slot assignment.");
        if(RenderActionButton("Switch to slots")){
            g_ComponentInfusionTotalsDraft=data;
            GearEdit::ClearComponentInfusionTotals(data);
            if(g_ComponentInfusionSlotDraft&&g_ComponentInfusionSlotDraft->contains("gear"))
                data["gear"]=g_ComponentInfusionSlotDraft->at("gear");
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if(ImGui::BeginPopup("##component_total_mode")){
        ImGui::TextWrapped("Individual terrestrial infusion requirements will be removed on Save. A linked Aquatic component is not modified here.");
        auto configs=SavedInfusionConfigurations(projection);
        configs.erase(std::remove_if(configs.begin(),configs.end(),[](auto c){
            return c==InfusionConfig::WaterA||c==InfusionConfig::WaterB;
        }),configs.end());
        static int selected=-1;
        if(ImGui::IsWindowAppearing())selected=configs.size()==1?static_cast<int>(configs.front()):-1;
        const bool restore=mode==InfusionMode::Conflict||g_ComponentInfusionTotalsDraft.has_value();
        if(!restore){
            for(auto config:configs)
                if(ImGui::RadioButton(InfusionConfigurationLabel(config),selected==static_cast<int>(config)))selected=static_cast<int>(config);
        }
        const bool picked=std::any_of(configs.begin(),configs.end(),[&](auto c){return static_cast<int>(c)==selected;});
        auto total=picked?ComputeSavedSlotInfusions(projection,static_cast<InfusionConfig>(selected)):InfusionSummary{false,false,0,{}};
        if(!restore&&picked&&!total.complete)ImGui::TextDisabled("Exact amounts or metadata are incomplete.");
        const bool calculatedSwitch=(restore||(picked&&total.complete))&&RenderActionButton("Switch to totals");
        const bool blankSwitch=!restore&&RenderActionButton("Start blank manual totals");
        if(calculatedSwitch||blankSwitch){
            g_ComponentInfusionSlotDraft=data;
            GearEdit::ClearComponentInfusionSlots(data);
            if(mode!=InfusionMode::Conflict){
                GearEdit::ClearComponentInfusionTotals(data);
                if(g_ComponentInfusionTotalsDraft){
                    for(const char* key:{"infusion_totals","infusion_summary_present","infusion_ar_total","infusion_stat_totals"})
                        if(g_ComponentInfusionTotalsDraft->contains(key))data[key]=g_ComponentInfusionTotalsDraft->at(key);
                }else{
                    if(blankSwitch)total={true,true,0,{}};
                    data["infusion_summary_present"]=true;data["infusion_ar_total"]=total.agonyResistance;
                    data["infusion_stat_totals"]=total.stats;
                }
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if(mode==InfusionMode::Slots){
        ImGui::TextDisabled("Calculated from this terrestrial component only; aquatic upgrades belong to Aquatic.");
        RenderSlotInfusionSummaries(projection);
        if(g_ComponentInfusionTotalsDraft&&ImGui::CollapsingHeader("Previous aggregate targets (unsaved reference)")){
            SavedBuild previous;
            if(ComponentInfusionProjection(*g_ComponentInfusionTotalsDraft,previous)){
                ForgetSlotInfusions(previous);
                ImGui::PushID("previous_targets");RenderInfusionTotals(GetSavedInfusionSummary(previous));ImGui::PopID();
            }
        }
    }else if(mode==InfusionMode::Totals){
        const auto total=GetSavedInfusionSummary(projection);
        std::function<void(const InfusionSummary&)> update;
        if(total.complete)update=[&](const InfusionSummary& next){
            GearEdit::ClearComponentInfusionTotals(data);
            data["infusion_summary_present"]=true;data["infusion_ar_total"]=next.agonyResistance;
            data["infusion_stat_totals"]=next.stats;
        };
        if(!total.complete&&RenderActionButton("Replace with blank manual totals..."))
            ImGui::OpenPopup("##component_manual_targets");
        if(ImGui::BeginPopup("##component_manual_targets")){
            ImGui::TextWrapped("Imported amounts cannot be verified. Starting blank replaces them on Save.");
            if(RenderActionButton("Start manual totals")){
                GearEdit::ClearComponentInfusionTotals(data);data["infusion_summary_present"]=true;
                data["infusion_ar_total"]=0;data["infusion_stat_totals"]=ComponentJson::object();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        RenderInfusionTotals(total,update);
        if(!projection.infusion_totals.empty())ImGui::TextDisabled("Imported aggregate recommendations; editing converts to numeric targets.");
    }
    return mode==InfusionMode::Slots;
}

static void RenderComponentNativeEditor(const std::string& kind,
                                        const std::string& id,
                                        ComponentJson& data) {
    if (!data.is_object()) data = ComponentJson::object();
    DemandComponentItemMetadata(kind,data);
    std::string profession = ComponentNativeProfession(kind, id, data);
    if (kind == "skills") {
        using namespace AlterEgo;
        const auto owner=g_ComponentSkillOwners.find(id);
        const bool conflictingOwner=g_ComponentEditorMode=="edit"&&owner!=g_ComponentSkillOwners.end()&&
            (owner->second.conflict||owner->second.incomplete);
        const auto fixed=ComponentNativeProfession(kind,id,g_ComponentEditorBaseline);
        const auto inferred=Composable::InferSkillsProfession(data,GW2API::CachedSkillProfessions);
        if(!conflictingOwner&&!data.contains("profession")){
            const auto resolved=!fixed.empty()?fixed:inferred.profession;
            if(!resolved.empty()){profession=resolved;data["profession"]=resolved;}
        }
        const bool editable=fixed.empty()&&(inferred.profession.empty()||inferred.profession!=profession);
        if(conflictingOwner)ImGui::TextWrapped("Conflicting or unknown Loadout ownership. Review the affected Loadouts first.");
        if(editable)RenderComponentProfessionChoice(profession,[&](const std::string& choice){profession=choice;data["profession"]=choice;});
        else RenderComponentProfessionLabel(profession);
        if(Composable::InferSkillsProfession(data,GW2API::CachedSkillProfessions,profession).conflict)
            ImGui::TextWrapped("Recorded skills disagree with the selected profession. Review the skill selections.");
        if(!profession.empty())AlterEgo::GW2API::FetchProfessionPaletteAsync(profession);
        if (!profession.empty() &&
            AlterEgo::GW2API::GetProfessionSkillIds(profession).empty()) {
            ImGui::SameLine();
            if (RenderActionButton("Fetch skill options##component"))
                AlterEgo::GW2API::FetchProfessionInfoAsync(profession,true);
        }
    }
    if (kind == "base_build") {
        const auto fixed=ComponentNativeProfession(kind,id,g_ComponentEditorBaseline);
        const bool referenced=(g_ComponentEditorMode=="edit"||g_ComponentEditorMode=="copy_for_loadout")&&
            !id.empty()&&ComponentUIUsage(ComponentUISnapshot(),kind,id)>0;
        if(fixed.empty()&&!referenced&&(!data.contains("specializations")||data.at("specializations").empty())){
            RenderComponentProfessionChoice(profession,[&](const std::string& choice){profession=choice;data["profession"]=choice;});
        }else RenderComponentProfessionLabel(profession);
        if(profession.empty())ImGui::TextDisabled("Profession unresolved; recorded fields retained.");
        ComponentNativeSpecializations(data,profession);
    } else if (AlterEgo::Composable::IsCompanionKind(kind)) {
        ComponentNativeProfessionPair(data,AlterEgo::Composable::CompanionField(kind),kind=="pets"?"Pets":"Legends",kind=="legends",true,ComponentUIEliteContexts(kind,id));
    } else if (kind == "skills") {
        if (profession.empty())
            ImGui::TextDisabled("Choose the profession to enable its skill choices.");
        const auto specContext = ComponentNativeSkillSpecs(id);
        if (specContext.second && specContext.first.empty())
            ImGui::TextDisabled("Shared skill choices are limited to specs common to referencing Loadouts.");
        ComponentNativeSkillBar(data, "skills", "Terrestrial skills",
                                profession, specContext.first, !specContext.second);
        ComponentNativeSkillBar(data, "aquatic_skills", "Aquatic skills",
                                profession, specContext.first, !specContext.second);
        if(const auto inactive=AlterEgo::SkillEdit::ReadInactivePalettes(data))
            RenderInactiveRevenantOrder(*inactive,AlterEgo::SkillEdit::ReadInactiveLegendContext(data),ICON_SIZE);
        else if(data.contains("inactive_rev_skills"))ImGui::TextDisabled("Unrecognized inactive utility order; retained");
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
        if(kind!="infusions"||ComponentNativeInfusionRequirements(data))
            ComponentNativeGear(data,kind,profession);
    }
}
