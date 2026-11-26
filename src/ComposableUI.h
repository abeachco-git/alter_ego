#pragma once
// UI for independently saved Loadout components.
//
// Include from dllmain.cpp after the existing UI globals/helper controls.
// All mutations are queued and applied after the visible panes finish
// rendering: SavedBuild references remain stable for the whole frame.
#include "ComposableSemantics.h"
#include "UntrustedInput.h"
#include "NativeLibraryEditing.h"
#include "ComposableGearPresentation.h"
#include "ComponentBrowseCore.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

using ComponentJson = nlohmann::json;

static const struct { const char* kind; const char* label; } kComponentKinds[] = {
    {"base_build", "Base Build"},
    {"skills", "Skills"},
    {"weapons", "Weapons"},
    {"sigils", "Sigils"},
    {"armor", "Armor"},
    {"runes", "Runes"},
    {"trinkets", "Trinkets"},
    {"relic", "Relics"},
    {"infusions", "Infusions"}
};

static const char* ComponentLabel(const std::string& kind) {
    for (const auto& entry : kComponentKinds)
        if (kind == entry.kind) return entry.label;
    return "Unknown";
}

struct ComponentUICommand {
    enum Type { None, Assign, CopyEditedForLoadout, SetFashion, Create, Duplicate,
                Rename, EditShared, RemoveUnused, ImportPortableLoadout, Consolidate, ReuseEdited, ApplySet, RenameSet, RemoveSet, DetachSet, SaveSet, DuplicateSet, AssignSetPart, SaveReferences, SaveSetDraft, CreateSet } type = None;
    std::string loadout_id, kind, component_id, name;
    ComponentJson data = ComponentJson::object();
    uint64_t expected_revision = 0;
    std::string candidate_id;
};
static ComponentUICommand g_ComponentCommand;
using ReferenceMap=std::map<std::string,std::string>;
struct ReferenceEditorDraft {
    bool open=false, isSet=false, queued=false, confirmed=false, parentDraft=false, newSet=false;
    uint64_t revision=0;
    std::string id,name,setId,fashionId,error;
    ReferenceMap refs,originalRefs;
    std::string originalName,originalSet,originalFashion;
};
static ReferenceEditorDraft g_ReferenceEditor;
static std::string g_ComponentSearch;
static bool g_ComponentUnusedOnly = false;
static int g_ComponentSort = 0; // Name, Usage
static bool g_ComponentSortAscending=true;
static std::set<std::string> g_ComponentCollapsedKinds;
static std::string g_ComponentSelectedId;
static std::string g_ComponentSelectedKind;
static std::string g_ComponentEditorId;
static std::string g_ComponentEditorKind;
static std::string g_ComponentEditorName;
static bool g_ComponentEditorAutoName = false;
static ComponentJson g_ComponentNameRequestedDraft;
static bool g_ComponentNameRequested=false;
static std::string g_ComponentSkillProfessionHint; // UI-only skill picker filter
static ComponentJson g_ComponentEditorDraft = ComponentJson::object();
static std::string g_ComponentEditorError;
static bool g_ComponentEditorOpen = false;
static bool g_ComponentEditorNew = false;
static std::string g_ComponentEditorMode; // create, duplicate, rename, edit, copy_for_loadout
static bool g_ComponentEditorSaveQueued = false;
static std::string g_ComponentEditorPrivateLoadoutId;
static std::string g_ComponentStatus;
static uint64_t g_ComponentEditorRevision = 0;
static std::map<std::pair<std::string,std::string>,int> g_ComponentUsageIndex;
static std::map<std::pair<std::string,std::string>,int> g_ComponentEffectiveLoadouts;


static const ComponentJson& ComponentUISnapshot() {
    static ComponentJson cached = ComponentJson::object();
    static uint64_t previousRevision = 0;
    const uint64_t revision = AlterEgo::GW2API::GetBuildLibraryRevision();
    if (revision != previousRevision || !cached.contains("loadouts")) {
        ComponentJson latest;
        if (AlterEgo::GW2API::GetComposableLoadouts(latest))
            cached = std::move(latest);
        else cached = ComponentJson::object();
        g_ComponentUsageIndex.clear();g_ComponentEffectiveLoadouts.clear();
        AlterEgo::Composable::Document graph;std::string error;
        if(AlterEgo::Composable::FromJson(cached,graph,error))
            for(const auto& l:graph.loadouts)for(const auto& [kind,id]:AlterEgo::Composable::EffectiveRefs(graph,l))
                ++g_ComponentEffectiveLoadouts[{kind,id}];
        if (cached.contains("loadouts")) for (const auto& l : cached.at("loadouts"))
            if (l.contains("refs") && l.at("refs").is_object())
                for (auto ref=l.at("refs").begin(); ref!=l.at("refs").end(); ++ref)
                    if(ref.value().is_string()) ++g_ComponentUsageIndex[{ref.key(),ref.value().get<std::string>()}];
        if(cached.contains("equipment_sets"))for(const auto& set:cached.at("equipment_sets"))
            for(auto ref=set.at("refs").begin();ref!=set.at("refs").end();++ref)
                ++g_ComponentUsageIndex[{ref.key(),ref.value().get<std::string>()}];
        previousRevision = revision;
    }
    return cached;
}

static const ComponentJson* ComponentUIFindLoadout(const ComponentJson& doc,
                                                    const std::string& id) {
    if (!doc.contains("loadouts") || !doc["loadouts"].is_array()) return nullptr;
    for (const auto& item : doc["loadouts"])
        if (item.is_object() && item.value("id", std::string()) == id) return &item;
    return nullptr;
}
static const ComponentJson* ComponentUIFind(const ComponentJson& doc,
                                            const std::string& kind,
                                            const std::string& id) {
    if (!doc.contains("components") || !doc["components"].is_object()) return nullptr;
    auto collection = doc["components"].find(kind);
    if (collection == doc["components"].end() || !collection->is_array())
        return nullptr;
    for (const auto& item : *collection)
        if (item.is_object() && item.value("id", std::string()) == id) return &item;
    return nullptr;
}
static int ComponentUIUsage(const ComponentJson& doc, const std::string& kind,
                             const std::string& id) {
    (void)doc;
    const auto it=g_ComponentUsageIndex.find({kind,id});
    return it==g_ComponentUsageIndex.end()?0:it->second;
}
static int ComponentUIEffectiveLoadoutCount(const std::string& kind,const std::string& id) {
    const auto it=g_ComponentEffectiveLoadouts.find({kind,id});
    return it==g_ComponentEffectiveLoadouts.end()?0:it->second;
}
static std::string ComponentUIShortDescription(const std::string& kind,
                                                const ComponentJson& data) {
    if (!data.is_object()) return "Invalid data";
    if (kind == "relic")
        return data.value("relic_name", std::string("Relic")) +
               (data.contains("relic_id") ? " (#" + data["relic_id"].dump() + ")" : "");
    if (kind == "base_build") {
        const std::string profession = data.value("profession", std::string());
        return profession.empty() ? "Specializations and traits" : profession;
    }
    if (kind == "skills") return "Heal, utility and elite skills";
    if (kind == "infusions") {
        auto summary = AlterEgo::Composable::Semantics::Infusions(data);
        if (summary.known) {
            int count = 0;
            for (const auto& entry : summary.counts) count += entry.second;
            return std::to_string(count) + " recorded infusions";
        }
        if (data.contains("infusion_summary_present"))
            return "Aggregate AR / stat requirements";
        return "No verified infusion totals";
    }
    if (kind == "runes") {
        auto summary = AlterEgo::Composable::Semantics::Runes(data);
        if (summary.known) {
            int count = 0;
            for (const auto& entry : summary.counts) count += entry.second;
            return std::to_string(count) + " recorded runes";
        }
        return data.contains("rune_name") ? data.value("rune_name", std::string("Rune"))
                                           : "Rune requirements unknown";
    }
    if (kind == "sigils") {
        auto summary = AlterEgo::Composable::Semantics::Sigils(data);
        if (summary.known) {
            int count = 0;
            for (const auto& entry : summary.counts) count += entry.second;
            return std::to_string(count) + " sigils in weapon sets";
        }
        return "No recorded sigils";
    }
    if (data.contains("gear") && data["gear"].is_object())
        return std::to_string(data["gear"].size()) + " equipment slots";
    return "No saved fields";
}
// Human-readable inventory before the advanced JSON representation. This
// remains deliberately lossless: it reads the same component payload, without
// interpreting missing item names as empty slots.
static void ComponentStoredNameActions(const ComponentJson& item);
static void RenderComponentInventory(const std::string& kind, const ComponentJson& data) {
    if (!data.is_object()) return;
    if (kind == "runes" || kind == "sigils" || kind == "infusions") {
        AlterEgo::Composable::Semantics::Summary summary;
        if (kind == "runes") summary = AlterEgo::Composable::Semantics::Runes(data);
        else if (kind == "sigils") summary = AlterEgo::Composable::Semantics::Sigils(data);
        else summary = AlterEgo::Composable::Semantics::Infusions(data);
        ImGui::TextDisabled("%s", summary.known
            ? "Functional totals (slot independent where applicable)"
            : "Totals cannot be verified from saved fields");
        for (const auto& entry : summary.counts)
            ImGui::BulletText("%d x %s", entry.second, entry.first.c_str());
        if (kind == "infusions" && data.contains("infusion_ar_total") &&
            data["infusion_ar_total"].is_number_integer())
            ImGui::Text("Saved agony resistance: %d",
                data["infusion_ar_total"].get<int>());
        if (kind == "infusions" && data.contains("infusion_stat_totals") &&
            data["infusion_stat_totals"].is_object()) {
            for (auto it = data["infusion_stat_totals"].begin();
                 it != data["infusion_stat_totals"].end(); ++it)
                if (it.value().is_number_integer())
                    ImGui::BulletText("%s: %lld", it.key().c_str(),
                        static_cast<long long>(it.value().get<int64_t>()));
        }
    }
    if (kind == "relic") {
        if (data.contains("relic_name") && data["relic_name"].is_string()){
            const auto name=data["relic_name"].get<std::string>();
            ImGui::Text("Relic: %s",name.c_str());
            if(ImGui::BeginPopupContextItem("##component_relic_name")){
                CopyNameMenuItem(name);ImGui::EndPopup();
            }
        }
        if (data.contains("relic_id") && data["relic_id"].is_number_integer())
            ImGui::Text("Item ID: %lld",
                static_cast<long long>(data["relic_id"].get<int64_t>()));
    }
    if (kind == "base_build") {
        ImGui::TextDisabled("Profession, specialization and trait definition");
        if (data.contains("profession") && data["profession"].is_string())
            ImGui::Text("Profession: %s", data["profession"].get<std::string>().c_str());
        if (data.contains("specializations") && data["specializations"].is_array())
            ImGui::Text("Specialization entries: %d", (int)data["specializations"].size());
    }
    if (kind == "skills") {
        for (const auto& bar : {"skills", "aquatic_skills"}) {
            if (!data.contains(bar) || !data[bar].is_object()) continue;
            ImGui::TextDisabled("%s", std::string(bar) == "skills"
                ? "Terrestrial skills" : "Aquatic skills");
            const auto& skills = data[bar];
            for (const auto& field : {"heal", "elite", "utilities"}) {
                if (!skills.contains(field)) continue;
                const std::string value = skills[field].is_array()
                    ? skills[field].dump() : skills[field].dump();
                ImGui::TextWrapped("%s: %s", field, value.c_str());
            }
        }
    }
    if (data.contains("gear") && data["gear"].is_object() &&
        !data["gear"].empty()) {
        ImGui::Separator();
        ImGui::TextDisabled("Recorded physical equipment slots");
        if (ImGui::BeginTable("##component_items", 2,
              ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
            ImGui::TableSetupColumn("Slot", ImGuiTableColumnFlags_WidthFixed, 105.0f);
            ImGui::TableSetupColumn("Stored details", ImGuiTableColumnFlags_WidthStretch);
            for (const auto& slotName : AlterEgo::Composable::Presentation::SortedGearSlots(data["gear"])) {
                const auto& item = data["gear"].at(slotName);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(GearSlotDisplayName(slotName));
                ImGui::TableSetColumnIndex(1);
                ImGui::PushID(slotName.c_str());
                const bool expanded=ImGui::TreeNodeEx("##component_slot",ImGuiTreeNodeFlags_NoTreePushOnOpen|ImGuiTreeNodeFlags_SpanAvailWidth,"%s",AlterEgo::Composable::Presentation::GearSlotSummary(kind,item).c_str());
                if(ImGui::BeginPopupContextItem("##component_item_names")){
                    ComponentStoredNameActions(item);ImGui::EndPopup();
                }
                if (expanded) {
                    ImGui::Indent();
                    if (item.is_object()) {
                        for (auto field = item.begin(); field != item.end(); ++field)
                            ImGui::TextWrapped("%s: %s", field.key().c_str(),
                                               field.value().dump().c_str());
                    } else {
                        ImGui::TextWrapped("%s", item.dump(2).c_str());
                    }
                    ImGui::Unindent();
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }
}

static bool ComponentUISearchMatch(std::string text, std::string needle) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    std::transform(needle.begin(), needle.end(), needle.begin(),
                   [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    return text.find(needle) != std::string::npos;
}
#include "ComposableNativeEditor.h"

static void ComponentUIStartEditor(const std::string& mode,
                                   const std::string& kind,
                                   const std::string& id,
                                   const ComponentJson* component) {
    g_ComponentEditorMode = mode;
    g_ComponentEditorRevision = AlterEgo::GW2API::GetBuildLibraryRevision();
    g_ComponentEditorAutoName = (mode == "create");
    g_ComponentNameRequested=false;
    g_ComponentEditorKind = kind;
    g_ComponentEditorId = id;
    g_ComponentEditorPrivateLoadoutId.clear();
    g_ComponentSkillProfessionHint.clear();
    g_ComponentCatalogSearch.clear();
    g_ComponentStatSearch.clear();
    g_ComponentGearSelectedSlot.clear();
    g_ComponentGearRemovalPending.clear();
    g_ComponentEditorSaveQueued = false;
    g_ComponentEditorName = component
        ? component->value("name", std::string()) : std::string("New ") + ComponentLabel(kind);
    if (mode == "duplicate") g_ComponentEditorName += " (Copy)";
    if (mode == "copy_for_loadout") g_ComponentEditorName += " (Private)";
    g_ComponentEditorDraft = component && component->contains("data") &&
        component->at("data").is_object()
        ? component->at("data") : ComponentJson::object();
    g_ComponentEditorError.clear();
    g_ComponentEditorOpen = true;
}
static void ComponentUIStartPrivateEditor(const std::string& loadoutId,
                                           const std::string& kind,
                                           const std::string& componentId,
                                           const ComponentJson* component) {
    ComponentUIStartEditor("copy_for_loadout", kind, componentId, component);
    g_ComponentEditorPrivateLoadoutId = loadoutId;
}

static void ComponentUIOpenLibrary(const std::string& kind, const std::string& id);
static bool ComponentUIEditorCanCommit() {
    if(g_ComponentEditorMode!="create" && g_ComponentEditorRevision!=AlterEgo::GW2API::GetBuildLibraryRevision()) {
        g_ComponentEditorError="Library changed. Cancel and reopen this editor before saving.";return false;
    }
    if (AlterEgo::Composable::ComponentNameKey(g_ComponentEditorName).empty()) {
        g_ComponentEditorError = "Give the component a name.";
        return false;
    }
    if (!g_ComponentEditorDraft.is_object()) {
        g_ComponentEditorError = "Component data must be an object.";
        return false;
    }
    return true;
}
static void RenderComponentUIEditor() {
    if (g_ComponentEditorOpen && !ImGui::IsPopupOpen("Component editor##ae_components"))
        ImGui::OpenPopup("Component editor##ae_components");
    ImGui::SetNextWindowSize(ImVec2(750.0f, 660.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Component editor##ae_components",
                                &g_ComponentEditorOpen)) return;

    ImGui::Text("%s - %s", ComponentLabel(g_ComponentEditorKind),
                g_ComponentEditorMode == "edit" ? "Edit shared data" :
                g_ComponentEditorMode == "duplicate" ? "Duplicate & edit" :
                g_ComponentEditorMode == "copy_for_loadout" ? "Edit private copy" :
                g_ComponentEditorMode == "create" ? "Create" : "Rename");
    const int uses = ComponentUIUsage(ComponentUISnapshot(), g_ComponentEditorKind,
                                      g_ComponentEditorId);
    if (g_ComponentEditorMode == "edit") {
        ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.40f, 1.0f),
            "SHARED EDIT: %d direct owner(s), %d effective Loadout(s). Copy first for a private edit.",
            uses,ComponentUIEffectiveLoadoutCount(g_ComponentEditorKind,g_ComponentEditorId));
        if (uses > 0 && ImGui::TreeNode("Affected Loadouts / equipment sets##component_shared_users")) {
            const auto& snapshot = ComponentUISnapshot();
            if (snapshot.contains("loadouts") && snapshot["loadouts"].is_array())
                for (const auto& loadout : snapshot["loadouts"]) {
                    if (!loadout.is_object() || !loadout.contains("refs") ||
                        !loadout["refs"].is_object()) continue;
                    const auto& refs = loadout["refs"];
                    if (refs.value(g_ComponentEditorKind, std::string()) !=
                        g_ComponentEditorId) continue;
                    const auto meta = loadout.value("metadata", ComponentJson::object());
                    const std::string label = meta.is_object()
                        ? meta.value("name", loadout.value("id", std::string()))
                        : loadout.value("id", std::string());
                    ImGui::BulletText("%s", label.c_str());
                }
            if(snapshot.contains("equipment_sets"))for(const auto& set:snapshot.at("equipment_sets"))
                if(set.at("refs").value(g_ComponentEditorKind,std::string())==g_ComponentEditorId) {
                    ImGui::BulletText("%s (Equipment Set)",set.value("name",std::string()).c_str());
                    for(const auto& l:snapshot.at("loadouts"))if(l.value("equipment_set_id",std::string())==set.value("id",std::string()))
                        ImGui::TextDisabled("  %s (via set)",l.at("metadata").value("name",l.value("id",std::string())).c_str());
                }
            ImGui::TreePop();
        }
    } else if (g_ComponentEditorMode == "copy_for_loadout") {
        ImGui::TextColored(ImVec4(.73f,.92f,.76f,1.0f),
            "PRIVATE COPY: Save creates a detached component for one Loadout.");
        ImGui::TextDisabled("Cancel keeps the original shared reference unchanged.");
    } else if (g_ComponentEditorMode == "duplicate") {
        ImGui::TextDisabled("Creates an independent component; the source remains unchanged.");
    } else {
        ImGui::TextDisabled("Components may be reused by multiple Loadouts.");
    }
    if(g_ComponentEditorAutoName){
        const bool changed=!g_ComponentNameRequested||g_ComponentNameRequestedDraft!=g_ComponentEditorDraft;
        g_ComponentEditorName=AlterEgo::GW2API::SuggestLibraryComponentName(g_ComponentEditorKind,g_ComponentEditorDraft,changed);
        if(changed){g_ComponentNameRequestedDraft=g_ComponentEditorDraft;g_ComponentNameRequested=true;}
    }
    ImGui::SetNextItemWidth(-1);
    if (AlterEgo::UI::InputText("Name##component_name", g_ComponentEditorName))
        g_ComponentEditorAutoName = false;
    if (RenderActionButton("Regenerate Name##component_editor")) {
        g_ComponentEditorAutoName = true;
        g_ComponentNameRequested=false;
        g_ComponentEditorName = AlterEgo::GW2API::SuggestLibraryComponentName(
            g_ComponentEditorKind, g_ComponentEditorDraft);
    }
    // Cache candidate results by draft and committed revision. All match
    // checks use native exact equality and verified profession context.
    if (g_ComponentEditorMode != "rename") {
        using namespace AlterEgo;
        static ComponentJson matchedDraft;
        static std::string matchedKind, matchedId, matchedMode, matchedLoadout, similarName;
        static uint64_t matchedRevision = 0;
        static std::vector<NativeLibrary::Match> matches;
        const auto revision = GW2API::GetBuildLibraryRevision();
        if (revision != matchedRevision || matchedKind != g_ComponentEditorKind ||
            matchedId != g_ComponentEditorId || matchedMode != g_ComponentEditorMode ||
            matchedLoadout != g_ComponentEditorPrivateLoadoutId ||
            !NativeLibrary::ExactContent(matchedDraft, g_ComponentEditorDraft)) {
            matchedRevision = revision;
            matchedKind = g_ComponentEditorKind;
            matchedId = g_ComponentEditorId;
            matchedMode = g_ComponentEditorMode;
            matchedLoadout = g_ComponentEditorPrivateLoadoutId;
            matchedDraft = g_ComponentEditorDraft;
            matches.clear();
            similarName.clear();
            Composable::Document document;
            std::string error;
            if (!matchedDraft.empty() &&
                Composable::FromJson(ComponentUISnapshot(), document, error)) {
                std::string profession;
                if (matchedMode == "copy_for_loadout") {
                    if (const auto* l = Composable::FindLoadout(document, matchedLoadout))
                        profession = Composable::ProfessionOf(document, *l);
                } else if (matchedKind == "skills") {
                    profession = NativeLibrary::VerifiedSkillsProfession(document, matchedId);
                }
                matches = NativeLibrary::EditIndex(document).Matches(matchedKind, matchedDraft, profession);
                matches.erase(std::remove_if(matches.begin(), matches.end(),
                    [&](const auto& m){ return m.id == matchedId; }), matches.end());
                if (matches.empty() && matchedMode == "create") {
                    auto group = document.components.find(matchedKind);
                    if (group != document.components.end())
                        for (const auto& item : group->second)
                            if (!NativeLibrary::ExactContent(item.data, matchedDraft) &&
                                Composable::Semantics::SimilarRequirements(matchedKind, item.data, matchedDraft)) {
                                similarName = item.name; break;
                            }
                }
            }
        }
        if (!matches.empty()) {
            ImGui::TextDisabled("Exact existing component%s", matches.size() == 1 ? "" : "s");
            if (ImGui::BeginChild("##component_matches", ImVec2(0, std::min(96.0f,
                    ImGui::GetFrameHeightWithSpacing() * static_cast<float>(matches.size()) + 4.0f)), false)) {
                for (const auto& match : matches) {
                    ImGui::PushID(match.id.c_str());
                    ImGui::Text("%s (%lld uses)", match.name.c_str(), static_cast<long long>(match.users));
                    if (g_ComponentEditorMode == "create" || g_ComponentEditorMode == "copy_for_loadout") {
                        ImGui::SameLine();
                        if (RenderActionButton("Use Existing##component_editor") && ComponentUIEditorCanCommit()) {
                            if (g_ComponentEditorMode == "create") {
                                ComponentUIOpenLibrary(g_ComponentEditorKind, match.id);
                            } else {
                                ComponentUICommand cmd;
                                cmd.type = ComponentUICommand::ReuseEdited;
                                cmd.kind = g_ComponentEditorKind;
                                cmd.component_id = g_ComponentEditorId;
                                cmd.loadout_id = g_ComponentEditorPrivateLoadoutId;
                                cmd.candidate_id = match.id;
                                cmd.data = g_ComponentEditorDraft;
                                cmd.expected_revision = g_ComponentEditorRevision;
                                g_ComponentCommand = std::move(cmd);
                                g_ComponentEditorSaveQueued = true;
                            }
                            g_ComponentEditorOpen = false;
                            ImGui::PopID();
                            ImGui::EndChild();
                            ImGui::CloseCurrentPopup();
                            ImGui::EndPopup();
                            return;
                        }
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndChild();
            if (g_ComponentEditorMode == "edit")
                ImGui::TextDisabled("Save keeps this component independent. Consolidate explicitly after saving.");
            else if (g_ComponentEditorMode == "duplicate" || g_ComponentEditorMode == "copy_for_loadout")
                ImGui::TextDisabled("Save creates a separate component instead of reusing a match.");
        } else if (!similarName.empty()) {
            ImGui::TextDisabled("Similar functional totals: %s", similarName.c_str());
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Exact saved fields differ. Both records remain independent.");
        }
    }
    if (g_ComponentEditorMode != "rename") {
        ImGui::Separator();
        ImGui::BeginChild("##component_native_editor", ImVec2(0, -110.0f), false);
        RenderComponentNativeEditor(g_ComponentEditorKind, g_ComponentEditorId,
                                    g_ComponentEditorDraft);
        ImGui::EndChild();
    }
    if (!g_ComponentEditorError.empty())
        ImGui::TextWrapped("%s", g_ComponentEditorError.c_str());
    const bool editorStale=g_ComponentEditorMode!="create"&&g_ComponentEditorRevision!=AlterEgo::GW2API::GetBuildLibraryRevision();
    if(editorStale)ImGui::TextWrapped("Library changed. Cancel and reopen this editor before saving.");
    const auto editAction=AlterEgo::UI::SaveCancel("component_editor",!editorStale&&!AlterEgo::Composable::ComponentNameKey(g_ComponentEditorName).empty()&&g_ComponentEditorDraft.is_object());
    if (editAction==AlterEgo::UI::EditAction::Save) {
        if (ComponentUIEditorCanCommit()) {
            ComponentUICommand cmd;
            cmd.kind = g_ComponentEditorKind;
            cmd.expected_revision = g_ComponentEditorMode=="create" ? 0 : g_ComponentEditorRevision;
            cmd.component_id = g_ComponentEditorId;
            cmd.name = g_ComponentEditorName;
            if (g_ComponentEditorMode == "copy_for_loadout") {
                cmd.type = ComponentUICommand::CopyEditedForLoadout;
                cmd.loadout_id = g_ComponentEditorPrivateLoadoutId;
                cmd.data = g_ComponentEditorDraft;
            } else if (g_ComponentEditorMode == "create" ||
                g_ComponentEditorMode == "duplicate") {
                cmd.type = ComponentUICommand::Create;
                cmd.data = g_ComponentEditorDraft;
            } else if (g_ComponentEditorMode == "rename") {
                cmd.type = ComponentUICommand::Rename;
            } else {
                cmd.type = ComponentUICommand::EditShared;
                cmd.data = g_ComponentEditorDraft;
            }
            g_ComponentCommand = std::move(cmd);
            g_ComponentEditorSaveQueued = true;
            g_ComponentEditorOpen = false;
            ImGui::CloseCurrentPopup();
        }
    }
    if (editAction==AlterEgo::UI::EditAction::Cancel) {
        g_ComponentEditorOpen = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}
static bool ComponentUICompatible(const ComponentJson& doc,
                                  const ComponentJson& target,
                                  const std::string& kind,
                                  const ComponentJson& candidate) {
    // Restrict cross-profession Build selection. Skill components do not have
    // an encoded profession, so infer it from existing Loadouts that use them.
    const auto& refs = target["refs"];
    auto current = refs.find("base_build");
    if (current == refs.end() || !current->is_string()) return true;
    const auto* base = ComponentUIFind(doc, "base_build", current->get<std::string>());
    if (!base || !base->contains("data")) return true;
    const std::string profession =
        (*base)["data"].value("profession", std::string());
    if (profession.empty()) return true;
    if (kind == "base_build")
        return candidate.contains("data") &&
               candidate["data"].value("profession", std::string()) == profession;
    if (kind != "skills") return true;
    for (const auto& other : doc["loadouts"]) {
        if (!other.contains("refs") || !other["refs"].is_object()) continue;
        const auto& otherRefs = other["refs"];
        if (!otherRefs.contains("skills") ||
            otherRefs["skills"] != candidate["id"] ||
            !otherRefs.contains("base_build")) continue;
        const auto* otherBuild = ComponentUIFind(doc, "base_build",
            otherRefs["base_build"].get<std::string>());
        if (otherBuild && (*otherBuild)["data"].value("profession", std::string()) != profession)
            return false;
    }
    return true;
}

static void ComponentUIOpenLibrary(const std::string& kind, const std::string& id) {
    MarkNavigation();
    g_ComponentKindFilters.clear();g_ComponentCollapsedKinds.erase(kind);
    g_ComponentPendingKind=-1;
    g_ComponentSelectedKind = kind;
    g_ComponentSelectedId = id;
    g_ComponentSearch.clear();g_ComponentUnusedOnly=false;
    g_LibraryCollection = 2;
}

static void PastePortableLoadout() {
    {
        const char* clipboard = ImGui::GetClipboardText();
        if (!clipboard || !*clipboard) {
            if (APIDefs) APIDefs->GUI_SendAlert("Clipboard is empty.");
        } else {
            try {
                const ComponentJson incoming = AlterEgo::Untrusted::ParseClipboard(clipboard);
                if (!incoming.is_object() ||
                    incoming.value("format", std::string()) !=
                        AlterEgo::Composable::kPortableLoadoutFormat) {
                    if (APIDefs) APIDefs->GUI_SendAlert(
                        "Clipboard is not an Alter Ego portable Loadout.");
                } else {
                    g_ComponentCommand = {ComponentUICommand::ImportPortableLoadout,
                        "", "", "", "", incoming};
                }
            } catch (...) {
                if (APIDefs) APIDefs->GUI_SendAlert("Invalid clipboard JSON.");
            }
        }
    }

}

static void CopyPortableLoadout(const std::string& loadoutId) {
    const auto& doc = ComponentUISnapshot();
        AlterEgo::Composable::Document current;
        std::string error;
        ComponentJson packet;
        if (AlterEgo::Composable::FromJson(doc, current, error) &&
            AlterEgo::Composable::ExportPortableLoadout(current, loadoutId,
                                                       packet, error)) {
            const std::string text = packet.dump(2);
            CopyToClipboard(text);
            if (APIDefs) APIDefs->GUI_SendAlert(
                "Portable Loadout copied with all referenced components.");
        } else if (APIDefs) APIDefs->GUI_SendAlert(
            "Could not export portable Loadout.");
}

static const ComponentJson* ComponentUIFindSet(const ComponentJson& doc,const std::string& id) {
    if(!id.empty()&&doc.contains("equipment_sets"))
        for(const auto& s:doc.at("equipment_sets"))if(s.value("id",std::string())==id)return &s;
    return nullptr;
}
static ReferenceMap ComponentUIRefs(const ComponentJson& owner) {
    return owner.at("refs").get<ReferenceMap>();
}
static std::string ComponentUIRef(const ReferenceMap& refs,const std::string& kind) {
    const auto it=refs.find(kind);return it==refs.end()?std::string():it->second;
}
static std::string ComponentUIRefName(const ComponentJson& doc,const std::string& kind,const std::string& id,const char* empty="Not set") {
    if(id.empty())return empty;
    const auto* part=ComponentUIFind(doc,kind,id);
    return part?part->value("name",id):"(missing)";
}
static void BeginReferenceEditor(const ComponentJson& owner,bool isSet) {
    auto& e=g_ReferenceEditor;e={};
    e.open=true;e.isSet=isSet;e.revision=AlterEgo::GW2API::GetBuildLibraryRevision();
    e.id=owner.value("id",std::string());e.refs=e.originalRefs=ComponentUIRefs(owner);
    if(isSet)e.name=e.originalName=owner.value("name",e.id);
    else {
        e.name=owner.at("metadata").value("name",e.id);
        e.setId=e.originalSet=owner.value("equipment_set_id",std::string());
        e.fashionId=e.originalFashion=owner.value("fashion_id",std::string());
        if(g_LibEditMode&&g_EditRefsReady&&e.id==g_EditDraftId){
            e.parentDraft=true;e.revision=g_EditDraftRevision;
            e.refs=e.originalRefs=g_EditRefs;
            e.setId=e.originalSet=g_EditSetId;
            e.fashionId=e.originalFashion=g_EditFashionId;
        }
    }
}
static void BeginNewEquipmentSet(){
    auto& e=g_ReferenceEditor;e={};
    e.open=true;e.isSet=true;e.newSet=true;
    e.revision=AlterEgo::GW2API::GetBuildLibraryRevision();
}
static void SelectDraftEquipmentSet(ReferenceEditorDraft& e,const ComponentJson& doc,const std::string& id) {
    if(e.setId==id)return;
    if(const auto* old=ComponentUIFindSet(doc,e.setId))
        for(const auto& [kind,ref]:ComponentUIRefs(*old))e.refs[kind]=ref;
    if(const auto* next=ComponentUIFindSet(doc,id))
        for(const auto& [kind,ref]:ComponentUIRefs(*next))e.refs.erase(kind);
    e.setId=id;e.confirmed=false;
}
static void DraftSelectEquipmentSet(const ComponentJson& doc,const std::string& id){SelectDraftEquipmentSet(g_ReferenceEditor,doc,id);}
static void RenderReferenceGroupLabel(const char* label) {
    ImGui::Spacing();ImGui::TextUnformatted(label);ImGui::Separator();
}
static void ComponentRowMenu(const ComponentJson& doc,const std::string& kind,const ComponentJson& item,bool reference=false,bool mutableActions=true);
static bool ReferenceHitRow(const std::string& name,bool inherited,const char* hint){
    const float width=std::max(1.f,ImGui::GetContentRegionAvail().x);
    const float reserve=inherited?18.f:0.f;
    const float wrap=std::max(1.f,width-reserve);
    const auto start=ImGui::GetCursorScreenPos();
    const float height=std::max(ImGui::GetTextLineHeight(),ImGui::CalcTextSize(name.c_str(),nullptr,false,wrap).y);
    const bool clicked=ImGui::Selectable("##reference",false,ImGuiSelectableFlags_SpanAllColumns,ImVec2(width,height));
    auto* draw=ImGui::GetWindowDrawList();
    draw->AddText(ImGui::GetFont(),ImGui::GetFontSize(),start,ImGui::GetColorU32(ImGuiCol_Text),name.c_str(),nullptr,wrap);
    if(inherited){
        const float x=start.x+width-11.f,y=start.y+ImGui::GetTextLineHeight()*0.5f;
        const auto color=ImGui::GetColorU32(ImGuiCol_TextDisabled);
        draw->AddCircle(ImVec2(x-3.f,y+2.f),3.f,color,12,1.2f);
        draw->AddCircle(ImVec2(x+3.f,y-2.f),3.f,color,12,1.2f);
        draw->AddLine(ImVec2(x-1.f,y+1.f),ImVec2(x+1.f,y-1.f),color,1.2f);
    }
    if(ImGui::IsItemHovered())ImGui::SetTooltip("%s\n%s",name.c_str(),hint);
    return clicked;
}
static void ReferenceComponentActions(const ComponentJson& doc,const std::string& kind,const std::string& id,bool mutableActions){
    if(const auto* part=ComponentUIFind(doc,kind,id))
        if(ImGui::BeginPopupContextItem("##ref_actions")){
            ComponentRowMenu(doc,kind,*part,true,mutableActions);
            ImGui::EndPopup();
        }
}
static void RenderReferenceValue(const ComponentJson& doc,const std::string& kind,const std::string& id,bool inherited,bool mutableActions,const char* empty="Not set"){
    const auto name=ComponentUIRefName(doc,kind,id,empty);
    if(!ComponentUIFind(doc,kind,id)){ImGui::TextDisabled("%s",name.c_str());return;}
    const char* hint=inherited?"From Equipment Set. Right-click for actions.":"Right-click for actions.";
    if(ReferenceHitRow(name,inherited,hint)&&mutableActions)ComponentUIOpenLibrary(kind,id);
    ReferenceComponentActions(doc,kind,id,mutableActions);
}
static bool RenderReferencePicker(const ComponentJson& doc,const ComponentJson* loadout,
    const std::string& kind,ReferenceMap& refs,bool allowOmit) {
    const auto ref=ComponentUIRef(refs,kind);
    const auto label=ComponentUIRefName(doc,kind,ref,allowOmit?"Not included":"Not set");
    bool changed=false;
    ImGui::SetNextItemWidth(-1);
    if(AlterEgo::UI::BeginCombo("##ref",label.c_str())){
        if(allowOmit&&AlterEgo::UI::SelectableOption("Not included",ref.empty())&&!ref.empty()){
            refs.erase(kind);changed=true;
        }
        const auto group=doc.at("components").find(kind);
        if(group!=doc.at("components").end())for(const auto& part:*group){
            if(loadout&&!ComponentUICompatible(doc,*loadout,kind,part))continue;
            const auto id=part.value("id",std::string());
            if(AlterEgo::UI::SelectableOption((part.value("name",id)+"##"+id).c_str(),id==ref)&&id!=ref){
                refs[kind]=id;changed=true;
            }
        }
        ImGui::EndCombo();
    }else ReferenceComponentActions(doc,kind,ref,false);
    return changed;
}
static std::string ComponentUIFashionName(const std::string& id){
    if(id.empty())return "None";
    for(const auto& look:g_FashionLibrary.looks)if(look.id==id)return look.name;
    return "(missing saved appearance)";
}
static constexpr int kEquipmentReferenceOrder[]={2,4,6,5,3,8,7};
static constexpr int kBuildReferenceOrder[]={0,1};
static void ReferenceTableColumns(){
    ImGui::TableSetupColumn("Category",ImGuiTableColumnFlags_WidthFixed,116.f);
    ImGui::TableSetupColumn("Selection",ImGuiTableColumnFlags_WidthStretch);
}
static void ReferenceLabel(const char* label){
    ImGui::TableNextRow();ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();ImGui::TextUnformatted(label);
    ImGui::TableSetColumnIndex(1);
}
static void RenderEditableReferenceRows(const ComponentJson& doc,const ComponentJson* owner,ReferenceEditorDraft& e,const int* kinds,int count){
    const auto* linked=e.isSet?nullptr:ComponentUIFindSet(doc,e.setId);
    ComponentJson candidate=owner?*owner:ComponentJson::object();
    for(int j=0;j<count;++j){
        const std::string kind=kComponentKinds[kinds[j]].kind;
        ImGui::PushID(kind.c_str());ReferenceLabel(ComponentLabel(kind));
        if(linked&&linked->at("refs").contains(kind)){
            RenderReferenceValue(doc,kind,linked->at("refs").value(kind,std::string()),true,false);
        }else{
            candidate["refs"]=e.refs;
            if(RenderReferencePicker(doc,e.isSet?nullptr:&candidate,kind,e.refs,e.isSet))e.confirmed=false;
        }
        ImGui::PopID();
    }
}
static void RenderSetReferencePicker(const ComponentJson& doc,ReferenceEditorDraft& e){
    const auto* linked=ComponentUIFindSet(doc,e.setId);
    const auto label=linked?linked->value("name",e.setId):std::string("Independent components");
    ImGui::SetNextItemWidth(-1);
    if(AlterEgo::UI::BeginCombo("##draft_set",label.c_str())){
        if(AlterEgo::UI::SelectableOption("Independent components",e.setId.empty()))SelectDraftEquipmentSet(e,doc,"");
        if(doc.contains("equipment_sets"))for(const auto& set:doc.at("equipment_sets")){
            const auto id=set.value("id",std::string());
            if(AlterEgo::UI::SelectableOption((set.value("name",id)+"##"+id).c_str(),id==e.setId))SelectDraftEquipmentSet(e,doc,id);
        }
        ImGui::EndCombo();
    }else if(linked&&ImGui::BeginPopupContextItem("##set_ref_copy")){
        CopyNameMenuItem(label);ImGui::EndPopup();
    }
}
static void RenderFashionReferencePicker(ReferenceEditorDraft& e){
    ImGui::SetNextItemWidth(-1);
    if(AlterEgo::UI::BeginCombo("##draft_fashion",ComponentUIFashionName(e.fashionId).c_str())){
        if(AlterEgo::UI::SelectableOption("None",e.fashionId.empty()))e.fashionId.clear();
        for(const auto& look:g_FashionLibrary.looks)
            if(AlterEgo::UI::SelectableOption((look.name+"##"+look.id).c_str(),look.id==e.fashionId))e.fashionId=look.id;
        ImGui::EndCombo();
    }else if(!e.fashionId.empty()&&ImGui::BeginPopupContextItem("##fashion_ref_copy")){
        CopyNameMenuItem(ComponentUIFashionName(e.fashionId));ImGui::EndPopup();
    }
}
static void RenderEditableReferences(const ComponentJson& doc,const ComponentJson* owner,ReferenceEditorDraft& e){
    if(!e.isSet){
        RenderReferenceGroupLabel("Build");
        if(ImGui::BeginTable("##draft_build",2,ImGuiTableFlags_SizingStretchProp)){
            ReferenceTableColumns();RenderEditableReferenceRows(doc,owner,e,kBuildReferenceOrder,IM_ARRAYSIZE(kBuildReferenceOrder));ImGui::EndTable();
        }
    }
    RenderReferenceGroupLabel("Equipment");
    if(ImGui::BeginTable("##draft_equipment",2,ImGuiTableFlags_SizingStretchProp)){
        ReferenceTableColumns();
        if(!e.isSet){ReferenceLabel("Equipment Set");RenderSetReferencePicker(doc,e);}
        RenderEditableReferenceRows(doc,owner,e,kEquipmentReferenceOrder,IM_ARRAYSIZE(kEquipmentReferenceOrder));
        if(!e.isSet){ReferenceLabel("Fashion");RenderFashionReferencePicker(e);}
        ImGui::EndTable();
    }
}
static bool ApplyParentReferenceDraft(ReferenceEditorDraft& e){
    if(!g_LibEditMode||!g_EditRefsReady||e.id!=g_EditDraftId||g_GearDialogOpen){
        e.error="Parent Loadout draft is unavailable. Close gear editing first.";return false;
    }
    if(!AlterEgo::GW2API::RebaseLoadoutDraft(g_EditDraft,g_EditSetId,g_EditFashionId,g_EditRefs,e.setId,e.fashionId,e.refs,e.revision,e.error))return false;
    g_EditRefs=e.refs;g_EditSetId=e.setId;g_EditFashionId=e.fashionId;
    g_EditSharedKinds.clear();g_LibDetailsFetched=false;MarkEditDirty();return true;
}
static void RenderReferenceEditor(){
    using namespace AlterEgo;
    auto& e=g_ReferenceEditor;
    if(!e.open)return;
    const char* popup=e.newSet?"New Equipment Set###ae_reference_editor":e.isSet?"Edit Equipment Set###ae_reference_editor":"Edit Loadout Components###ae_reference_editor";
    if(!ImGui::IsPopupOpen(popup))ImGui::OpenPopup(popup);
    const auto display=ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowSize(ImVec2(std::min(580.f,display.x-24.f),std::min(620.f,display.y-24.f)),ImGuiCond_Appearing);
    if(!ImGui::BeginPopupModal(popup,&e.open))return;
    const auto& doc=ComponentUISnapshot();
    const auto* owner=e.isSet?ComponentUIFindSet(doc,e.id):ComponentUIFindLoadout(doc,e.id);
    const bool stale=e.revision!=GW2API::GetBuildLibraryRevision()||(!e.newSet&&!owner);
    if(!e.isSet)ImGui::TextWrapped("%s",e.name.c_str());
    if(stale)ImGui::TextWrapped("Library changed. Cancel and reopen to avoid overwriting newer changes.");
    ImGui::BeginChild("##reference_editor_fields",ImVec2(0,-110),false);
    if(e.isSet){ImGui::SetNextItemWidth(-1);UI::InputText("Name",e.name);}
    if((owner||e.newSet)&&doc.contains("components"))RenderEditableReferences(doc,owner,e);
    int users=0;bool adding=false;
    if(e.isSet&&owner){
        for(const auto& l:doc.at("loadouts"))if(l.value("equipment_set_id",std::string())==e.id)++users;
        for(const auto& [kind,id]:e.refs)if(!e.originalRefs.count(kind))adding=true;
        ImGui::Spacing();ImGui::TextWrapped("Save changes the shared set for %d linked Loadout(s). Removing a category preserves its previous selection on each Loadout.",users);
        if(adding&&users)ImGui::Checkbox("Replace independent selections in linked Loadouts",&e.confirmed);
    }
    ImGui::EndChild();
    const bool dirty=e.refs!=e.originalRefs||(e.isSet?e.name!=e.originalName:e.setId!=e.originalSet||e.fashionId!=e.originalFashion);
    const bool valid=!stale&&dirty&&(!e.isSet||(!e.refs.empty()&&!Composable::ComponentNameKey(e.name).empty()&&(!adding||!users||e.confirmed)));
    if(e.isSet&&e.refs.empty())ImGui::TextDisabled("Include at least one equipment category.");
    if(!e.error.empty())ImGui::TextWrapped("%s",e.error.c_str());
    if(e.parentDraft)ImGui::TextWrapped("Save here stages selections in the Loadout draft. The top Save commits all changes; Cancel discards them.");
    const auto action=UI::SaveCancel("reference_editor",valid);
    if(action==UI::EditAction::Save){
        if(e.parentDraft){
            if(ApplyParentReferenceDraft(e)){e.open=false;ImGui::CloseCurrentPopup();}
        }else{
            g_ComponentCommand={e.newSet?ComponentUICommand::CreateSet:e.isSet?ComponentUICommand::SaveSetDraft:ComponentUICommand::SaveReferences,e.id,"",e.setId,e.name,
                ComponentJson{{"refs",e.refs},{"fashion",e.fashionId}},e.revision};
            e.queued=true;e.open=false;ImGui::CloseCurrentPopup();
        }
    }else if(action==UI::EditAction::Cancel){e={};ImGui::CloseCurrentPopup();}
    ImGui::EndPopup();
}
// Read-only references share the same menu/action dispatcher as the Components collection.
static void RenderReferencePreviewRow(const ComponentJson& doc,const std::string& kind,const std::string& ref,
    const std::string&,bool inherited,bool actions,const char* empty="Not set"){
    ImGui::PushID(kind.c_str());ReferenceLabel(ComponentLabel(kind));
    RenderReferenceValue(doc,kind,ref,inherited,actions,empty);
    ImGui::PopID();
}
static void RenderLoadoutComponentSelectors(const std::string& loadoutId) {
    using namespace AlterEgo;
    const auto& doc=ComponentUISnapshot();
    const auto* l=ComponentUIFindLoadout(doc,loadoutId);if(!l)return;
    static bool expanded=false,wasEditing=false;
    static std::string lastId,inlineError;
    const bool editing=g_LibEditMode&&g_EditRefsReady&&loadoutId==g_EditDraftId;
    if(editing&&(!wasEditing||lastId!=loadoutId)){expanded=true;inlineError.clear();}
    if(lastId!=loadoutId)inlineError.clear();
    lastId=loadoutId;wasEditing=editing;
    ImGui::PushID(loadoutId.c_str());
    if(UI::Button(expanded?"Loadout Components -##loadout_selector":"Loadout Components +##loadout_selector",ImVec2(0,0),UI::ButtonRole::Compact))expanded=!expanded;
    if(!expanded){ImGui::PopID();return;}
    if(editing&&!g_GearDialogOpen){
        ReferenceEditorDraft e;e.id=loadoutId;e.revision=g_EditDraftRevision;e.refs=g_EditRefs;e.setId=g_EditSetId;e.fashionId=g_EditFashionId;
        RenderEditableReferences(doc,l,e);
        if(e.refs!=g_EditRefs||e.setId!=g_EditSetId||e.fashionId!=g_EditFashionId){
            if(ApplyParentReferenceDraft(e))inlineError.clear();
            else inlineError=e.error.empty()?"Cannot apply reference change; review the selected component.":e.error;
        }
        if(!inlineError.empty())ImGui::TextWrapped("%s",inlineError.c_str());
        ImGui::PopID();return;
    }
    const auto direct=editing?g_EditRefs:ComponentUIRefs(*l);
    const auto linkedId=editing?g_EditSetId:l->value("equipment_set_id",std::string());
    const auto* linked=ComponentUIFindSet(doc,linkedId);
    const auto inherited=linked?ComponentUIRefs(*linked):ReferenceMap{};
    const bool actions=!g_LibEditMode;
    if(g_LibEditMode)ImGui::TextDisabled("Close gear editing or reopen the Loadout draft to change references.");
    RenderReferenceGroupLabel("Build");
    if(ImGui::BeginTable("##loadout_build_refs",2,ImGuiTableFlags_SizingStretchProp)){
        ReferenceTableColumns();
        for(int i:kBuildReferenceOrder){
            const std::string kind=kComponentKinds[i].kind;
            RenderReferencePreviewRow(doc,kind,ComponentUIRef(direct,kind),loadoutId,false,actions);
        }
        ImGui::EndTable();
    }
    RenderReferenceGroupLabel("Equipment");
    static std::string setName;
    static std::set<std::string> setKinds;
    static uint64_t setDraftRevision=0;
    bool nameSet=false;
    if(ImGui::BeginTable("##loadout_equipment_refs",2,ImGuiTableFlags_SizingStretchProp)){
        ReferenceTableColumns();
        ImGui::PushID("equipment_set");ReferenceLabel("Equipment Set");
        const std::string setLabel=linked?linked->value("name",linkedId):"Independent components";
        if(ReferenceHitRow(setLabel,false,"Right-click for Equipment Set actions.")&&actions&&linked){
            g_EquipmentSetSelectedId=linkedId;NavigateToLibraryCollection(3);
        }
        if(ImGui::BeginPopupContextItem("##set_actions")){
            if(linked){
                if(actions&&ImGui::MenuItem("Open in Equipment Sets")){g_EquipmentSetSelectedId=linkedId;NavigateToLibraryCollection(3);}
                if(actions&&ImGui::MenuItem("Edit Shared Set"))BeginReferenceEditor(*linked,true);
                if(actions&&ImGui::MenuItem("Detach Set")){BeginReferenceEditor(*l,false);DraftSelectEquipmentSet(doc,"");}
                CopyNameMenuItem(setLabel);
            }
            if(actions&&ImGui::MenuItem(linked?"Copy Set":"Save as Equipment Set")){
                setName=(linked?setLabel:l->at("metadata").value("name",loadoutId))+(linked?" - Copy":" - Equipment");
                setKinds.clear();
                for(const auto& [kind,id]:(linked?inherited:direct))if(Composable::IsEquipmentKind(kind))setKinds.insert(kind);
                setDraftRevision=GW2API::GetBuildLibraryRevision();nameSet=true;
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
        for(int i:kEquipmentReferenceOrder){
            const std::string kind=kComponentKinds[i].kind;
            const bool shared=inherited.count(kind)!=0;
            RenderReferencePreviewRow(doc,kind,ComponentUIRef(shared?inherited:direct,kind),loadoutId,shared,actions);
        }
        ImGui::PushID("fashion_ref");ReferenceLabel("Fashion");
        const auto fashionId=editing?g_EditFashionId:l->value("fashion_id",std::string());
        int index=-1;
        for(int i=0;i<(int)g_FashionLibrary.looks.size();++i)if(g_FashionLibrary.looks[i].id==fashionId){index=i;break;}
        if(index<0)ImGui::TextDisabled("%s",ComponentUIFashionName(fashionId).c_str());
        else{
            const auto& look=g_FashionLibrary.looks[index];
            if(ReferenceHitRow(look.name,false,"Right-click for Fashion actions.")&&actions){g_FashionSelectedId=look.id;g_FashionSearch[0]=0;NavigateToLibraryCollection(1);}
            if(ImGui::BeginPopupContextItem("##fashion_actions")){
                if(actions&&ImGui::MenuItem("Open in Fashion")){g_FashionSelectedId=look.id;g_FashionSearch[0]=0;NavigateToLibraryCollection(1);}
                if(actions&&ImGui::MenuItem("Edit Fashion"))StartFashionEdit(look,index);
                CopyNameMenuItem(look.name);ImGui::EndPopup();
            }
        }
        ImGui::PopID();ImGui::EndTable();
    }
    if(nameSet)ImGui::OpenPopup("Name equipment set##loadout");
    if(ImGui::BeginPopupModal("Name equipment set##loadout",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
        UI::InputText("Name",setName);
        for(const auto& entry:kComponentKinds)if(Composable::IsEquipmentKind(entry.kind)){
            bool included=setKinds.count(entry.kind)!=0;
            if(ImGui::Checkbox(entry.label,&included)){if(included)setKinds.insert(entry.kind);else setKinds.erase(entry.kind);}
        }
        ImGui::TextWrapped("Save creates and links a separate named set. Existing component payloads remain shared.");
        const auto action=UI::SaveCancel("new_set",actions&&!setKinds.empty()&&!Composable::ComponentNameKey(setName).empty()&&setDraftRevision==GW2API::GetBuildLibraryRevision());
        if(action==UI::EditAction::Save){
            g_ComponentCommand={ComponentUICommand::SaveSet,loadoutId,"","",setName,ComponentJson(setKinds),setDraftRevision};
            ImGui::CloseCurrentPopup();
        }else if(action==UI::EditAction::Cancel)ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::PopID();
}

static void RenderLoadoutSaveReview(){
    using namespace AlterEgo;
    static ComponentJson review=ComponentJson::array();
    static std::string error;
    if(g_EditReviewPending){
        g_EditReviewPending=false;
        error.clear();
        g_EditSharedKinds.clear(); // no implicit consent from an earlier attempt
        if(!g_LibEditMode||!g_EditRefsReady||g_GearDialogOpen)return;
        if(!GW2API::ReviewUnifiedLoadout(g_EditDraft,g_EditSetId,g_EditFashionId,g_EditRefs,g_EditDraftRevision,review,error)){
            if(error.empty())error="Could not review the Loadout draft.";
            ImGui::OpenPopup("Review Loadout changes##ae_unified_review");
        }else if(review.empty())g_EditSavePending=true;
        else ImGui::OpenPopup("Review Loadout changes##ae_unified_review");
    }
    if(!ImGui::BeginPopupModal("Review Loadout changes##ae_unified_review",nullptr,ImGuiWindowFlags_AlwaysAutoResize))return;
    const bool valid=g_LibEditMode&&g_EditRefsReady&&!g_GearDialogOpen&&
        g_EditDraftRevision==GW2API::GetBuildLibraryRevision()&&error.empty();
    if(!error.empty())ImGui::TextWrapped("%s",error.c_str());
    else if(!valid)ImGui::TextWrapped("Library changed. Cancel this review and reopen the Loadout editor.");
    else{
        ImGui::TextWrapped("Private changes affect only this Loadout. Choose shared editing only if every listed owner should change.");
        for(const auto& row:review){
            const auto kind=row.value("kind",std::string()),name=row.value("name",std::string()),reuse=row.value("reuse",std::string());
            const bool shared=row.value("shared",false);
            ImGui::PushID(kind.c_str());
            RenderSectionHeader(ComponentLabel(kind),UI::SectionGold);
            if(!name.empty())ImGui::TextWrapped("%s",name.c_str());
            const bool useShared=g_EditSharedKinds.count(kind)!=0;
            const std::string privateLabel=reuse.empty()?"Only this Loadout":"Reuse "+reuse;
            if(shared){
                if(ImGui::RadioButton((privateLabel+"##private").c_str(),!useShared))g_EditSharedKinds.erase(kind);
                const auto label="Edit shared ("+std::to_string(row.value("loadouts",0))+" Loadouts, "+std::to_string(row.value("owners",0))+" direct owners)";
                if(ImGui::RadioButton((label+"##shared").c_str(),useShared))g_EditSharedKinds.insert(kind);
                if(row.value("set_users",0)>1&&!useShared)
                    ImGui::TextDisabled("Private changes may copy the linked Equipment Set.");
            }else if(!reuse.empty())ImGui::TextWrapped("Reuse identical component: %s",reuse.c_str());
            ImGui::PopID();
        }
    }
    const auto action=UI::SaveCancel("unified_review",valid);
    if(action==UI::EditAction::Save){g_EditSavePending=true;ImGui::CloseCurrentPopup();}
    else if(action==UI::EditAction::Cancel){g_EditSharedKinds.clear();ImGui::CloseCurrentPopup();}
    ImGui::EndPopup();
}

// Both surfaces invoke one action dispatcher; destructive confirmation is
// owned by the page rather than a row popup or a transient item pointer.
enum class ComponentAction { Edit, Rename, Duplicate, Copy, Delete };
struct ComponentDeleteDraft {std::string kind,id,name;uint64_t revision=0;bool pending=false;};
static ComponentDeleteDraft g_ComponentDelete;
static void RunComponentAction(ComponentAction action,const std::string& kind,const ComponentJson& item){
    const auto id=item.value("id",std::string()),name=item.value("name",id);
    switch(action){
    case ComponentAction::Edit:ComponentUIStartEditor("edit",kind,id,&item);break;
    case ComponentAction::Rename:ComponentUIStartEditor("rename",kind,id,&item);break;
    case ComponentAction::Duplicate:ComponentUIStartEditor("duplicate",kind,id,&item);break;
    case ComponentAction::Copy:{
        AlterEgo::Composable::Component exported;
        exported.name=name;exported.data=item.at("data");
        const auto portable=AlterEgo::Composable::ExportComponent(kind,exported).dump(2);
        CopyToClipboard(portable);break;
    }
    case ComponentAction::Delete:
        if(!ComponentUIUsage(ComponentUISnapshot(),kind,id))
            g_ComponentDelete={kind,id,name,AlterEgo::GW2API::GetBuildLibraryRevision(),true};
        break;
    }
}
static void ImportComponentClipboard(){
    const char* clipboard=ImGui::GetClipboardText();
    g_ComponentStatus.clear();
    if(!clipboard||!*clipboard){g_ComponentStatus="Clipboard is empty.";return;}
    try{
        const auto packet=AlterEgo::Untrusted::ParseClipboard(clipboard);
        std::string kind,error;AlterEgo::Composable::Component imported;
        if(!AlterEgo::Composable::ImportComponent(packet,kind,imported,error))g_ComponentStatus=error;
        else g_ComponentCommand={ComponentUICommand::Create,"",kind,"",imported.name,imported.data};
    }catch(const std::exception& e){g_ComponentStatus="Invalid or oversized component clipboard.";}
}
static constexpr int kComponentGroupOrder[]={0,1,4,2,6,5,3,8,7};
static void ComponentCreationMenu(const std::string& category={}){
    if(category.empty()){
        if(ImGui::BeginMenu("New")){
            for(int i:kComponentGroupOrder)if(ImGui::MenuItem(kComponentKinds[i].label))
                ComponentUIStartEditor("create",kComponentKinds[i].kind,"",nullptr);
            ImGui::EndMenu();
        }
    }else if(ImGui::MenuItem("New"))ComponentUIStartEditor("create",category,"",nullptr);
    if(ImGui::MenuItem("Paste"))ImportComponentClipboard();
}
static void ComponentRowMenu(const ComponentJson& doc,const std::string& kind,const ComponentJson& item,bool reference,bool mutableActions){
    const auto id=item.value("id",std::string()),name=item.value("name",id);
    if(mutableActions){
        if(reference&&ImGui::MenuItem("Open in Components"))ComponentUIOpenLibrary(kind,id);
        if(ImGui::MenuItem(reference?"Edit Shared":"Edit"))RunComponentAction(ComponentAction::Edit,kind,item);
        if(ImGui::MenuItem("Rename"))RunComponentAction(ComponentAction::Rename,kind,item);
        if(ImGui::MenuItem("Regenerate Name")){RunComponentAction(ComponentAction::Rename,kind,item);g_ComponentEditorAutoName=true;g_ComponentNameRequested=false;}
        if(ImGui::MenuItem("Duplicate"))RunComponentAction(ComponentAction::Duplicate,kind,item);
        ImGui::Separator();
    }
    CopyNameMenuItem(name);
    if(ImGui::MenuItem("Copy Component"))RunComponentAction(ComponentAction::Copy,kind,item);
    if(!reference&&mutableActions){
        ImGui::Separator();ComponentCreationMenu(kind);ImGui::Separator();
        if(AlterEgo::UI::DestructiveMenuItem("Delete",ComponentUIUsage(doc,kind,id)==0))RunComponentAction(ComponentAction::Delete,kind,item);
    }
}
static void RenderComponentDeleteConfirm(const ComponentJson& doc){
    auto& d=g_ComponentDelete;
    if(d.pending){d.pending=false;ImGui::OpenPopup("Delete unused component##ae");}
    if(!ImGui::BeginPopupModal("Delete unused component##ae",nullptr,ImGuiWindowFlags_AlwaysAutoResize))return;
    const bool valid=d.revision==AlterEgo::GW2API::GetBuildLibraryRevision()&&
        ComponentUIFind(doc,d.kind,d.id)&&ComponentUIUsage(doc,d.kind,d.id)==0;
    ImGui::TextWrapped("Delete %s?",d.name.c_str());
    if(!valid)ImGui::TextWrapped("Library changed or the component is referenced. Cancel and review again.");
    else if(AlterEgo::UI::Button("Delete##component_confirm",ImVec2(0,0),AlterEgo::UI::ButtonRole::Destructive)){
        g_ComponentCommand={ComponentUICommand::RemoveUnused,"",d.kind,d.id,"",ComponentJson::object(),d.revision};
        ImGui::CloseCurrentPopup();
    }
    if(valid)ImGui::SameLine();
    if(RenderActionButton("Cancel##component_confirm"))ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}
static void RenderComponentLibrary() {
    using namespace AlterEgo;
    const auto& doc=ComponentUISnapshot();
    if(!doc.contains("components")||!doc["components"].is_object()){
        ImGui::TextDisabled("Component library is not available.");RenderComponentUIEditor();return;
    }
    if(g_ComponentPendingKind>=0&&g_ComponentPendingKind<IM_ARRAYSIZE(kComponentKinds)){
        const std::string kind=kComponentKinds[g_ComponentPendingKind].kind;
        g_ComponentPendingKind=-1;
        auto id=g_ComponentSelectIdPending;
        if(id.empty()&&!g_ComponentSelectNamePending.empty()){
            const auto group=doc["components"].find(kind);
            if(group!=doc["components"].end())for(auto it=group->rbegin();it!=group->rend();++it)
                if(it->value("name",std::string())==g_ComponentSelectNamePending){id=it->value("id",std::string());break;}
        }
        if(!id.empty())ComponentUIOpenLibrary(kind,id);
        g_ComponentSelectIdPending.clear();g_ComponentSelectNamePending.clear();
    }
    const UI::SplitPanel split(g_ComponentListWidth,270.f,160.f,220.f);
    ImGui::BeginChild("##component_list",ImVec2(split.left,0),true);
    UI::SearchToolbar("components","Search components...",g_ComponentSearch,
        [&]{
            UI::FilterChips chips;
            for(const auto& kind:g_ComponentKindFilters)
                chips.push_back({"kind/"+kind,ComponentLabel(kind),[kind]{g_ComponentKindFilters.erase(kind);}});
            if(g_ComponentUnusedOnly)chips.push_back({"usage","Unused only",[]{g_ComponentUnusedOnly=false;}});
            return chips;
        },
        [&]{
            std::vector<std::pair<std::string,std::string>> kinds;
            for(int n:kComponentGroupOrder)kinds.emplace_back(kComponentKinds[n].kind,kComponentKinds[n].label);
            UI::MultiFilterChoice("Component type",g_ComponentKindFilters,kinds);
        },
        [&]{g_ComponentKindFilters.clear();g_ComponentUnusedOnly=false;});
    const char* sorts[]={"Name","Usage"};
    const float gap=ImGui::GetStyle().ItemSpacing.x;
    const float checkboxWidth=ImGui::GetFrameHeight()+ImGui::CalcTextSize("Unused only").x+ImGui::GetStyle().ItemInnerSpacing.x;
    const float available=ImGui::GetContentRegionAvail().x;
    const bool oneRow=available>=checkboxWidth+24.f+2*gap+75.f;
    ImGui::SetNextItemWidth(std::max(50.f,std::min(120.f,available-24.f-gap-(oneRow?checkboxWidth+gap:0.f))));
    if(UI::Combo("##component_sort",&g_ComponentSort,sorts,IM_ARRAYSIZE(sorts)))g_ComponentSortAscending=true;
    ImGui::SameLine();UI::SortDirectionButton("##component_sortdir",g_ComponentSortAscending);
    if(oneRow)ImGui::SameLine();
    ImGui::Checkbox("Unused only",&g_ComponentUnusedOnly);
    if(!g_ComponentStatus.empty())ImGui::TextWrapped("%s",g_ComponentStatus.c_str());
    ImGui::Separator();
    struct Entry {const ComponentJson* item;ComponentBrowse::SortKey key;};
    struct Group {std::string kind;std::vector<Entry> items;};
    std::vector<Group> groups;size_t count=0;
    const bool searching=!g_ComponentSearch.empty();
    const bool filtered=searching||g_ComponentUnusedOnly||!g_ComponentKindFilters.empty();
    for(int n:kComponentGroupOrder){
        const std::string kind=kComponentKinds[n].kind;
        if(!ComponentBrowse::MatchesKind(g_ComponentKindFilters,kind))continue;
        Group g{kind,{}};
        const auto group=doc["components"].find(kind);
        if(group!=doc["components"].end()&&group->is_array())for(const auto& item:*group){
            if(!item.is_object()||!item.contains("data")||!item["data"].is_object())continue;
            const auto id=item.value("id",std::string()),name=item.value("name",id);
            const int references=ComponentUIUsage(doc,kind,id);
            if(g_ComponentUnusedOnly&&references>0)continue;
            if(!ComponentUISearchMatch(name,g_ComponentSearch)&&!ComponentUISearchMatch(id,g_ComponentSearch)&&
               !ComponentUISearchMatch(ComponentUIShortDescription(kind,item["data"]),g_ComponentSearch))continue;
            g.items.push_back({&item,ComponentBrowse::Key(name,id,references)});
        }
        if(filtered&&g.items.empty())continue;
        std::stable_sort(g.items.begin(),g.items.end(),[&](const Entry& a,const Entry& b){
            return ComponentBrowse::Less(a.key,b.key,g_ComponentSort==1,g_ComponentSortAscending);
        });
        count+=g.items.size();groups.push_back(std::move(g));
    }
    bool selectedMatches=false;
    for(const auto& group:groups)if(group.kind==g_ComponentSelectedKind)for(const auto& entry:group.items)
        if(entry.item->value("id",std::string())==g_ComponentSelectedId)selectedMatches=true;
    ImGui::TextDisabled("%zu matching component%s",count,count==1?"":"s");
    bool first=true;
    for(const auto& g:groups){
        const auto& kind=g.kind;ImGui::PushID(kind.c_str());
        if(!first)ImGui::Spacing();
        first=false;
        const bool expanded=searching||!g_ComponentCollapsedKinds.count(kind);
        const auto summary=std::to_string(g.items.size());
        if(CompletionTheme::CenteredSectionToggle(expanded,ComponentLabel(kind))&&!searching){
            if(expanded)g_ComponentCollapsedKinds.insert(kind);else g_ComponentCollapsedKinds.erase(kind);
        }
        CompletionTheme::SectionHeading(ComponentLabel(kind),nullptr,summary.c_str(),CompletionTheme::kAvailableText);
        // The canonical heading is drawn with a dummy; add a transparent
        // hit target only over that heading, leaving the +/- button alone.
        const auto lo=ImGui::GetItemRectMin(),hi=ImGui::GetItemRectMax(),after=ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(lo);ImGui::InvisibleButton("##category_actions",ImVec2(hi.x-lo.x,hi.y-lo.y));
        if(ImGui::BeginPopupContextItem("##category_menu")){ComponentCreationMenu(kind);ImGui::EndPopup();}
        ImGui::SetCursorScreenPos(after);
        if(expanded)for(const auto& entry:g.items){
            const auto* item=entry.item;
            const auto id=item->value("id",std::string()),name=item->value("name",id);
            const int users=entry.key.references;
            ImGui::PushID(id.c_str());
            const std::string shown=name+"  ["+std::to_string(users)+"]###row";
            const bool selected=g_ComponentSelectedKind==kind&&g_ComponentSelectedId==id;
            const bool clicked=ImGui::Selectable(shown.c_str(),selected);
            if((clicked||ImGui::IsItemClicked(ImGuiMouseButton_Right))&&!selected){
                MarkNavigation();g_ComponentSelectedKind=kind;g_ComponentSelectedId=id;selectedMatches=true;
            }
            if(ImGui::IsItemHovered())ImGui::SetTooltip("%d direct reference%s; %d affected Loadout(s)",users,users==1?"":"s",ComponentUIEffectiveLoadoutCount(kind,id));
            if(ImGui::BeginPopupContextItem("##component_actions")){ComponentRowMenu(doc,kind,*item);ImGui::EndPopup();}
            ImGui::PopID();
        }
        ImGui::PopID();
    }
    if(ImGui::BeginPopupContextWindow("##component_empty_actions",ImGuiPopupFlags_MouseButtonRight|ImGuiPopupFlags_NoOpenOverItems)){
        ComponentCreationMenu();ImGui::EndPopup();
    }
    ImGui::EndChild();
    if(split.Divider("##component_split",g_ComponentListWidth))SaveSettings();
    ImGui::BeginChild("##component_detail",ImVec2(0,0),true);
    const std::string kind=g_ComponentSelectedKind;
    const auto* item=ComponentUIFind(doc,kind,g_ComponentSelectedId);
    const auto group=doc["components"].find(kind);
    if(!item){g_ComponentSelectedKind.clear();g_ComponentSelectedId.clear();ImGui::TextDisabled("Choose a saved component to view or edit.");}
    else{
        const auto id=item->value("id",std::string()),name=item->value("name",id);
        const int usage=ComponentUIUsage(doc,kind,id);
        if(!selectedMatches)ImGui::TextDisabled("Selected component is outside current filters.");
        ImGui::TextWrapped("%s",name.c_str());
        ImGui::TextDisabled("%s | %d reference%s | %d affected Loadout(s)",ComponentLabel(kind),usage,usage==1?"":"s",ComponentUIEffectiveLoadoutCount(kind,id));
        ImGui::TextWrapped("%s",ComponentUIShortDescription(kind,item->at("data")).c_str());
        ImGui::Separator();
        const auto action=[&](const char* label,ComponentAction command,bool sameLine){
            if(sameLine)UI::SameLineIfActionFits(label);
            if(UI::Button(label,ImVec2(0,0),UI::ButtonRole::Compact))RunComponentAction(command,kind,*item);
        };
        action("Edit",ComponentAction::Edit,false);
        action("Rename",ComponentAction::Rename,true);
        action("Duplicate",ComponentAction::Duplicate,true);
        action("Copy Component",ComponentAction::Copy,true);
        if(ImGui::IsItemHovered())ImGui::SetTooltip("Copy portable component including preserved imported fields.");
        if(usage==0){
            if(UI::Button("Delete Unused",ImVec2(0,0),UI::ButtonRole::CompactDestructive))
                RunComponentAction(ComponentAction::Delete,kind,*item);
        }else ImGui::TextDisabled("Referenced components cannot be deleted.");
        ImGui::Separator();
        ImGui::BeginChild("##component_detail_scroll",ImVec2(0,0),false);
        if (ImGui::CollapsingHeader("Exact duplicates##component_duplicates")) {
            static uint64_t duplicateRevision=0;
            static std::string duplicateKind,duplicateId;
            static std::vector<std::string> duplicates;
            static std::string pendingSource,pendingSurvivor,pendingKind;
            static uint64_t pendingRevision=0;
            const auto revision=AlterEgo::GW2API::GetBuildLibraryRevision();
            if(duplicateRevision!=revision||duplicateKind!=kind||duplicateId!=id) {
                duplicates.clear();
                if(group!=doc["components"].end()&&group->is_array())
                    for(const auto& candidate:*group) {
                        const auto other=candidate.value("id",std::string());
                        if(other!=id&&candidate.contains("data")&&
                            AlterEgo::NativeLibrary::ExactContent(candidate.at("data"),item->at("data")))
                            duplicates.push_back(other);
                    }
                duplicateRevision=revision;duplicateKind=kind;duplicateId=id;
            }
            ImGui::TextWrapped("Keep the selected component; explicitly replace references to an exact duplicate and remove that duplicate.");
            if(kind=="skills") ImGui::TextDisabled("Skills require matching, verified professions on both components.");
            if(duplicates.empty()) ImGui::TextDisabled("No exact duplicates.");
            for(const auto& other:duplicates) {
                const auto* candidate=ComponentUIFind(doc,kind,other);
                if(!candidate)continue;
                ImGui::PushID(other.c_str());
                ImGui::TextWrapped("%s (%d references)",candidate->value("name",other).c_str(),ComponentUIUsage(doc,kind,other));
                if(AlterEgo::UI::Button("Consolidate into selected",ImVec2(0,0),AlterEgo::UI::ButtonRole::Destructive)) {
                    pendingSource=other;pendingSurvivor=id;pendingKind=kind;pendingRevision=revision;
                    ImGui::OpenPopup("Confirm consolidation");
                }
                if(ImGui::BeginPopupModal("Confirm consolidation",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::TextWrapped("Remove %s and keep %s?",candidate->value("name",other).c_str(),name.c_str());
                    ImGui::TextDisabled("Affected Loadouts / equipment sets:");
                    for(const auto& l:doc.at("loadouts")) {
                        if(!l.contains("refs")||l.at("refs").value(kind,std::string())!=other)continue;
                        ImGui::BulletText("%s",l.value("metadata",ComponentJson::object()).value("name",l.value("id",std::string())).c_str());
                    }
                    if(doc.contains("equipment_sets"))for(const auto& set:doc.at("equipment_sets"))
                        if(set.at("refs").value(kind,std::string())==other)
                            ImGui::BulletText("%s (Equipment Set)",set.value("name",std::string()).c_str());
                    if(pendingRevision!=revision) ImGui::TextDisabled("Library changed; cancel and review again.");
                    else if(AlterEgo::UI::Button("Confirm",ImVec2(0,0),AlterEgo::UI::ButtonRole::Destructive)) {
                        g_ComponentCommand={ComponentUICommand::Consolidate,pendingSurvivor,pendingKind,pendingSource,"",ComponentJson::object(),pendingRevision};
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if(RenderActionButton("Cancel"))ImGui::CloseCurrentPopup();
                    ImGui::EndPopup();
                }
                ImGui::PopID();
            }
        }
        ImGui::Spacing();
        RenderComponentInventory(kind, item->at("data"));
        ImGui::Spacing();
        ImGui::TextDisabled("Used by (select to open):");
        bool anyUsage = false;
        if (doc.contains("loadouts") && doc["loadouts"].is_array()) {
            for (const auto& loadout : doc["loadouts"]) {
                if (!loadout.is_object() || !loadout.contains("refs") ||
                    !loadout["refs"].is_object()) continue;
                const auto& refs = loadout["refs"];
                if (!refs.contains(kind) || !refs[kind].is_string() ||
                    refs[kind].get<std::string>() != id) continue;
                anyUsage = true;
                const auto& meta = loadout.value("metadata", ComponentJson::object());
                const std::string loadoutName = meta.is_object()
                    ? meta.value("name", loadout.value("id", std::string()))
                    : loadout.value("id", std::string());
                ImGui::PushID(loadout.value("id", std::string()).c_str());
                if (ImGui::Selectable(loadoutName.c_str(), false)) {
                    const auto& builds = AlterEgo::GW2API::GetSavedBuilds();
                    for (int index = 0; index < static_cast<int>(builds.size()); ++index)
                        if (builds[index].id == loadout.value("id", std::string())) {
                            g_LibSelectedId=builds[index].id;
                            g_LibraryCollection = 0;MarkNavigation();
                            g_LibDetailsFetched = false;
                            break;
                        }
                }
                ImGui::PopID();
            }
        }
        if(doc.contains("equipment_sets"))for(const auto& set:doc.at("equipment_sets")) {
            if(set.at("refs").value(kind,std::string())!=id)continue;
            anyUsage=true;
            const auto setId=set.value("id",std::string());
            if(AlterEgo::UI::SelectableOption((set.value("name",setId)+" (Equipment Set)##"+setId).c_str())) {
                g_EquipmentSetSelectedId=setId;g_LibraryCollection=3;MarkNavigation();
            }
            for(const auto& l:doc.at("loadouts"))if(l.value("equipment_set_id",std::string())==setId)
                ImGui::TextDisabled("  %s (via set)",l.at("metadata").value("name",l.value("id",std::string())).c_str());
        }
        if (!anyUsage) ImGui::TextDisabled("No Loadouts or equipment sets reference this component.");
        ImGui::Separator();
        if (AlterEgo::UI::BeginCombo("Assign to Loadout##component_attach",
                              "Select Loadout...")) {
            bool offered = false;
            if (doc.contains("loadouts") && doc["loadouts"].is_array())
                for (const auto& loadout : doc["loadouts"]) {
                    if (!loadout.is_object() || !loadout.contains("refs") ||
                        !loadout["refs"].is_object()) continue;
                    bool inherited=false;
                    if(doc.contains("equipment_sets"))for(const auto& set:doc.at("equipment_sets"))
                        if(set.value("id",std::string())==loadout.value("equipment_set_id",std::string())&&set.at("refs").contains(kind))inherited=true;
                    if(inherited)continue;
                    if (!ComponentUICompatible(doc, loadout, kind, *item)) continue;
                    const std::string targetId = loadout.value("id", std::string());
                    if (targetId.empty() ||
                        loadout["refs"].value(kind, std::string()) == id) continue;
                    const auto& meta = loadout.value("metadata", ComponentJson::object());
                    const std::string targetName = meta.is_object()
                        ? meta.value("name", targetId) : targetId;
                    offered = true;
                    if (ImGui::Selectable((targetName + "##attach" + targetId).c_str())){
                        BeginReferenceEditor(loadout,false);g_ReferenceEditor.refs[kind]=id;
                    }
                }
            if (!offered) ImGui::TextDisabled("Already assigned or no compatible Loadouts.");
            ImGui::EndCombo();
        }
        if (usage > 0 &&
            AlterEgo::UI::BeginCombo("Copy privately for##component_detach", "Select Loadout...")) {
            for (const auto& loadout : doc["loadouts"]) {
                if (!loadout.is_object() || !loadout.contains("refs") ||
                    !loadout["refs"].is_object() ||
                    loadout["refs"].value(kind, std::string()) != id) continue;
                const std::string targetId = loadout.value("id", std::string());
                const auto& meta = loadout.value("metadata", ComponentJson::object());
                const std::string targetName = meta.is_object()
                    ? meta.value("name", targetId) : targetId;
                if (ImGui::Selectable((targetName + "##detach" + targetId).c_str())) {
                    ComponentUIStartPrivateEditor(targetId, kind, id, item);
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Create a detached copy for one Loadout and edit it immediately.");
        ImGui::EndChild();
    }
    ImGui::EndChild();
    RenderComponentDeleteConfirm(doc);
    RenderComponentUIEditor();
}

static std::string g_EquipmentSetSearch;
static void RenderEquipmentSetLibrary() {
    using namespace AlterEgo;
    const auto& doc=ComponentUISnapshot();
    auto& search=g_EquipmentSetSearch;
    if(!doc.contains("equipment_sets")){ImGui::TextDisabled("Equipment set library unavailable.");return;}
    const ComponentJson* selected=nullptr;
    const auto& sets=doc.at("equipment_sets");
    const UI::SplitPanel split(g_EquipmentSetListWidth,220.f,130.f,220.f);
    ImGui::BeginChild("##equipment_set_list",ImVec2(split.left,0),true);
    UI::SearchField("equipment_sets_search","Search equipment sets...",search);ImGui::Separator();
    bool selectedVisible=false;
    if(sets.empty())ImGui::TextDisabled("No equipment sets. Right-click to create one.");
    for(const auto& set:sets) {
        const auto id=set.value("id",std::string()),name=set.value("name",id);
        if(id==g_EquipmentSetSelectedId)selected=&set;
        if(!search.empty()) {
            auto lower=[](std::string value){for(char& c:value)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return value;};
            if(lower(name).find(lower(search))==std::string::npos)continue;
        }
        ImGui::PushID(id.c_str());
        if(UI::SelectableOption((name+"###set_row").c_str(),id==g_EquipmentSetSelectedId)) {
            MarkNavigation();g_EquipmentSetSelectedId=id;selected=&set;
        }
        if(id==g_EquipmentSetSelectedId)selectedVisible=true;
        if(ImGui::BeginPopupContextItem("##set_name")){
            if(ImGui::MenuItem("New"))BeginNewEquipmentSet();
            ImGui::Separator();
            if(ImGui::MenuItem("Edit"))BeginReferenceEditor(set,true);
            CopyNameMenuItem(name);ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    if(ImGui::BeginPopupContextWindow("##set_empty_actions",ImGuiPopupFlags_MouseButtonRight|ImGuiPopupFlags_NoOpenOverItems)){
        if(ImGui::MenuItem("New"))BeginNewEquipmentSet();ImGui::EndPopup();
    }
    ImGui::EndChild();
    if(split.Divider("##equipment_set_split",g_EquipmentSetListWidth))SaveSettings();
    ImGui::BeginChild("##equipment_set_detail",ImVec2(0,0),true);
    static std::string duplicateName;
    static RecordSelection::RevisionTarget duplicateTarget,deleteTarget;
    if(!selected){g_EquipmentSetSelectedId.clear();ImGui::TextDisabled("Select an equipment set.");}
    else{
    const auto id=selected->value("id",std::string()),name=selected->value("name",id);
    if(!selectedVisible)ImGui::TextDisabled("Selected set is outside current search.");
    ImGui::TextWrapped("%s",name.c_str());
    int users=0;
    for(const auto& l:doc.at("loadouts"))if(l.value("equipment_set_id",std::string())==id)++users;
    ImGui::TextDisabled("Used by %d Loadout(s)",users);
    if(RenderActionButton("Edit##equipment_set"))BeginReferenceEditor(*selected,true);
    UI::SameLineIfActionFits("Duplicate");
    if(RenderActionButton("Duplicate##equipment_set")){
        duplicateName=name+" - Copy";duplicateTarget={id,name,GW2API::GetBuildLibraryRevision()};ImGui::OpenPopup("Duplicate equipment set");
    }
    if(!users){
        UI::SameLineIfActionFits("Delete");
        if(UI::Button("Delete##equipment_set",ImVec2(0,0),UI::ButtonRole::Destructive)){
            deleteTarget={id,name,GW2API::GetBuildLibraryRevision()};ImGui::OpenPopup("Delete equipment set?");
        }
    }else ImGui::TextDisabled("Referenced sets cannot be deleted.");
    ImGui::Separator();
    ImGui::BeginChild("##equipment_set_detail_scroll",ImVec2(0,0),false);
    for(const auto& l:doc.at("loadouts"))if(l.value("equipment_set_id",std::string())==id)
        ImGui::BulletText("%s",l.at("metadata").value("name",l.value("id",std::string())).c_str());
    if(ImGui::BeginTable("##equipment_set_refs",2,ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Category",ImGuiTableColumnFlags_WidthFixed,110.f);
        ImGui::TableSetupColumn("Selection",ImGuiTableColumnFlags_WidthStretch);
        const auto refs=ComponentUIRefs(*selected);
        for(int i:kEquipmentReferenceOrder){
            const std::string kind=kComponentKinds[i].kind;
            RenderReferencePreviewRow(doc,kind,ComponentUIRef(refs,kind),"",false,true,"Not included");
        }
        ImGui::EndTable();
    }
    ImGui::TextWrapped("Not included: linked Loadouts keep their own selections.");
    if(UI::BeginCombo("##equipment_set_apply","Apply to Loadout...")) {
        for(const auto& loadout:doc.at("loadouts")) {
            const auto target=loadout.value("id",std::string());
            if(UI::SelectableOption((loadout.at("metadata").value("name",target)+"##"+target).c_str())){
                BeginReferenceEditor(loadout,false);DraftSelectEquipmentSet(doc,id);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::EndChild();
    }
    auto findSet=[&](const std::string& key){return std::find_if(sets.begin(),sets.end(),[&](const auto& set){return set.value("id",std::string())==key;});};
    if(ImGui::BeginPopupModal("Duplicate equipment set",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
        const bool valid=duplicateTarget.Current(GW2API::GetBuildLibraryRevision())&&findSet(duplicateTarget.id)!=sets.end();
        ImGui::TextWrapped("Duplicate %s",duplicateTarget.name.c_str());
        if(!valid)ImGui::TextWrapped("Library changed. Cancel and review again.");
        UI::InputText("Name",duplicateName);
        const auto action=UI::SaveCancel("duplicate_set",valid&&!Composable::ComponentNameKey(duplicateName).empty());
        if(action==UI::EditAction::Save){
            g_ComponentCommand={ComponentUICommand::DuplicateSet,"","",duplicateTarget.id,duplicateName,ComponentJson::object(),duplicateTarget.revision};
            duplicateTarget={};ImGui::CloseCurrentPopup();
        }else if(action==UI::EditAction::Cancel){duplicateTarget={};ImGui::CloseCurrentPopup();}
        ImGui::EndPopup();
    }
    if(ImGui::BeginPopupModal("Delete equipment set?",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
        const auto& loadouts=doc.at("loadouts");
        const bool referenced=std::any_of(loadouts.begin(),loadouts.end(),[&](const auto& l){return l.value("equipment_set_id",std::string())==deleteTarget.id;});
        const bool valid=deleteTarget.Current(GW2API::GetBuildLibraryRevision())&&!referenced&&findSet(deleteTarget.id)!=sets.end();
        ImGui::TextWrapped("Delete unused set '%s'? Its components are retained.",deleteTarget.name.c_str());
        if(!valid)ImGui::TextWrapped("Library changed or the set is referenced. Cancel and review again.");
        if(valid&&UI::Button("Delete",ImVec2(0,0),UI::ButtonRole::Destructive)){
            g_ComponentCommand={ComponentUICommand::RemoveSet,"","",deleteTarget.id,"",ComponentJson::object(),deleteTarget.revision};
            deleteTarget={};ImGui::CloseCurrentPopup();
        }
        if(valid)ImGui::SameLine();
        if(RenderActionButton("Cancel")){deleteTarget={};ImGui::CloseCurrentPopup();}
        ImGui::EndPopup();
    }
    ImGui::EndChild();
    RenderComponentUIEditor();
}

static void ProcessComponentUICommand() {
    if (g_ComponentCommand.type == ComponentUICommand::None) return;
    const auto command = g_ComponentCommand;
    g_ComponentCommand = ComponentUICommand{};
    const bool fromReferenceEditor=g_ReferenceEditor.queued;
    g_ReferenceEditor.queued=false;
    const bool fromEditor = g_ComponentEditorSaveQueued;
    g_ComponentEditorSaveQueued = false;
    bool success = false;
    using API = AlterEgo::GW2API;
    if (command.expected_revision && command.expected_revision!=API::GetBuildLibraryRevision()) {
        g_ComponentStatus="Library changed; reopen the editor or review duplicates again.";
        if(fromEditor) {g_ComponentEditorError=g_ComponentStatus;g_ComponentEditorOpen=true;}
        if(fromReferenceEditor){g_ReferenceEditor.error=g_ComponentStatus;g_ReferenceEditor.open=true;}
        if(APIDefs) APIDefs->GUI_SendAlert(g_ComponentStatus.c_str());
        return;
    }
    switch (command.type) {
    case ComponentUICommand::CreateSet:
        success=API::CreateEquipmentSet(command.name,command.data.at("refs").get<ReferenceMap>(),command.expected_revision,&g_EquipmentSetSelectedId);
        if(success){g_EquipmentSetSearch.clear();MarkNavigation();}
        break;
    case ComponentUICommand::SaveSetDraft:
        success=API::SaveEquipmentSetDraft(command.loadout_id,command.name,command.data.at("refs").get<ReferenceMap>(),command.expected_revision);
        break;
    case ComponentUICommand::SaveReferences:
        success=API::SaveLoadoutReferenceDraft(command.loadout_id,command.component_id,command.data.at("fashion").get<std::string>(),command.data.at("refs").get<ReferenceMap>(),command.expected_revision);
        break;
    case ComponentUICommand::ApplySet:
        success=API::ApplyEquipmentSet(command.loadout_id,command.component_id);
        break;
    case ComponentUICommand::DetachSet:
        success=API::DetachEquipmentSet(command.loadout_id);
        break;
    case ComponentUICommand::SaveSet:
        success=API::SaveLoadoutAsEquipmentSet(command.loadout_id,command.name,&g_EquipmentSetSelectedId,command.data.get<std::set<std::string>>());
        if(success){g_LibraryCollection=3;MarkNavigation();}
        break;
    case ComponentUICommand::DuplicateSet:
        success=API::DuplicateEquipmentSet(command.component_id,command.name,&g_EquipmentSetSelectedId,command.expected_revision);
        break;
    case ComponentUICommand::AssignSetPart:
        success=API::AssignEquipmentSetComponent(command.loadout_id,command.kind,command.component_id,command.expected_revision);
        break;
    case ComponentUICommand::RenameSet:
        success=API::RenameEquipmentSet(command.component_id,command.name);
        break;
    case ComponentUICommand::RemoveSet:
        success=API::RemoveEquipmentSet(command.component_id,command.expected_revision);
        if(success&&g_EquipmentSetSelectedId==command.component_id)g_EquipmentSetSelectedId.clear();
        break;
    case ComponentUICommand::Consolidate:
        success=API::ConsolidateLoadoutComponent(command.kind,command.component_id,command.loadout_id,command.expected_revision);
        break;
    case ComponentUICommand::Assign:
        success = API::AssignLoadoutComponent(
            command.loadout_id, command.kind, command.component_id);
        break;
    case ComponentUICommand::ReuseEdited:
    case ComponentUICommand::CopyEditedForLoadout:
        if (command.type == ComponentUICommand::ReuseEdited)
            success = API::ReuseEditedLoadoutComponent(command.loadout_id, command.kind,
                command.component_id, command.candidate_id, command.data, command.expected_revision);
        else
            success = API::CopyEditedLoadoutComponent(
                command.loadout_id, command.kind, command.component_id,
                command.name, command.data,command.expected_revision);
        if (success && g_LibraryCollection == 2) {
            const auto& latest = ComponentUISnapshot();
            const auto* target = ComponentUIFindLoadout(latest, command.loadout_id);
            if (target && target->contains("refs"))
                ComponentUIOpenLibrary(command.kind,
                    target->at("refs").value(command.kind, std::string()));
        }
        break;
    case ComponentUICommand::SetFashion:
        success = API::SetLoadoutFashion(command.loadout_id, command.component_id);
        break;
    case ComponentUICommand::Create: {
        std::string savedId;
        success = API::CreateLoadoutComponent(command.kind, command.name, command.data,
                                              &savedId);
        if (success && !savedId.empty()) ComponentUIOpenLibrary(command.kind, savedId);
        break;
    }
    case ComponentUICommand::Duplicate:
        success = API::DuplicateLoadoutComponent(
            command.kind, command.component_id, command.name);
        break;
    case ComponentUICommand::Rename:
        success = API::RenameLoadoutComponent(
            command.kind, command.component_id, command.name);
        break;
    case ComponentUICommand::EditShared:
        success = API::EditAndRenameLoadoutComponent(
            command.kind, command.component_id, command.name, command.data,command.expected_revision);
        break;
    case ComponentUICommand::ImportPortableLoadout:
        success = API::ImportPortableLoadout(command.data);
        if (success) {
            g_LibraryCollection = 0;MarkNavigation();
            const auto& builds = API::GetSavedBuilds();
            if (!builds.empty()) g_LibSelectedId=builds.back().id;
            g_LibDetailsFetched = false;
        }
        break;
    case ComponentUICommand::RemoveUnused:
        success=API::DeleteUnusedLoadoutComponent(command.kind,command.component_id,command.expected_revision);
        if (success && g_ComponentSelectedKind==command.kind && g_ComponentSelectedId == command.component_id)
            g_ComponentSelectedId.clear();
        break;
    default: break;
    }
    if (success) {
        if(fromReferenceEditor)g_ReferenceEditor={};
        g_LibDetailsFetched = false;
        g_EditDraftId.clear();
        g_ComponentStatus.clear();
    } else {
        g_ComponentStatus = "Could not apply component change. Check component compatibility or data fields.";
        // Preserve the draft on a rejected transaction (e.g. the source
        // Loadout was reassigned while Copy & Edit was open).
        if(fromReferenceEditor){g_ReferenceEditor.error=g_ComponentStatus;g_ReferenceEditor.open=true;}
        if (fromEditor) {
            g_ComponentEditorError = g_ComponentStatus;
            g_ComponentEditorOpen = true;
        }
    }
    if (APIDefs) APIDefs->GUI_SendAlert(success
        ? "Loadout component updated." : g_ComponentStatus.c_str());
}

} // anonymous namespace
