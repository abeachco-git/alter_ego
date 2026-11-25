#pragma once
// UI for independently saved Loadout components.
//
// Include from dllmain.cpp after the existing UI globals/helper controls.
// All mutations are queued and applied after the visible panes finish
// rendering: SavedBuild references remain stable for the whole frame.
#include "ComposableSemantics.h"
#include "NativeLibraryEditing.h"
#include "ComposableGearPresentation.h"
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
                Rename, EditShared, RemoveUnused, ImportPortableLoadout, Consolidate, ReuseEdited, ApplySet, RenameSet, RemoveSet, DetachSet, SaveSet, DuplicateSet, AssignSetPart, SaveReferences, SaveSetDraft } type = None;
    std::string loadout_id, kind, component_id, name;
    ComponentJson data = ComponentJson::object();
    uint64_t expected_revision = 0;
    std::string candidate_id;
};
static ComponentUICommand g_ComponentCommand;
using ReferenceMap=std::map<std::string,std::string>;
struct ReferenceEditorDraft {
    bool open=false, isSet=false, queued=false, confirmed=false, parentDraft=false;
    uint64_t revision=0;
    std::string id,name,setId,fashionId,error;
    ReferenceMap refs,originalRefs;
    std::string originalName,originalSet,originalFashion;
};
static ReferenceEditorDraft g_ReferenceEditor;
static std::string g_ComponentSearch;
static bool g_ComponentUnusedOnly = false;
static int g_ComponentSort = 0; // 0 name, 1 most used, 2 least used
static std::string g_ComponentSelectedId;
static std::string g_ComponentSelectedKind;
static std::string g_ComponentEditorId;
static std::string g_ComponentEditorKind;
static std::string g_ComponentEditorName;
static bool g_ComponentEditorAutoName = false;
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
                ImGui::TextWrapped("%s", AlterEgo::Composable::Presentation::
                    GearSlotSummary(kind, item).c_str());
                if(ImGui::BeginPopupContextItem("##component_item_names")){
                    ComponentStoredNameActions(item);ImGui::EndPopup();
                }
                if (ImGui::TreeNode("Exact stored fields##component_slot")) {
                    if (item.is_object()) {
                        for (auto field = item.begin(); field != item.end(); ++field)
                            ImGui::TextWrapped("%s: %s", field.key().c_str(),
                                               field.value().dump().c_str());
                    } else {
                        ImGui::TextWrapped("%s", item.dump(2).c_str());
                    }
                    ImGui::TreePop();
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
    if (g_ComponentEditorAutoName)
        g_ComponentEditorName = AlterEgo::GW2API::SuggestLibraryComponentName(
            g_ComponentEditorKind, g_ComponentEditorDraft);
    ImGui::SetNextItemWidth(-1);
    if (AlterEgo::UI::InputText("Name##component_name", g_ComponentEditorName))
        g_ComponentEditorAutoName = false;
    if (RenderActionButton("Auto Name##component_editor")) {
        g_ComponentEditorAutoName = true;
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
            cmd.name = g_ComponentEditorAutoName
                ? AlterEgo::GW2API::SuggestLibraryComponentName(g_ComponentEditorKind, g_ComponentEditorDraft)
                : g_ComponentEditorName;
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
    for (int i = 0; i < 9; ++i)
        if (kind == kComponentKinds[i].kind) {
            g_ComponentFilterKind = i;
            break;
        }
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
                const ComponentJson incoming = ComponentJson::parse(clipboard);
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
            ImGui::SetClipboardText(text.c_str());
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
static void DraftSelectEquipmentSet(const ComponentJson& doc,const std::string& id) {
    auto& e=g_ReferenceEditor;
    if(e.setId==id)return;
    if(const auto* old=ComponentUIFindSet(doc,e.setId))
        for(const auto& [kind,ref]:ComponentUIRefs(*old))e.refs[kind]=ref;
    if(const auto* next=ComponentUIFindSet(doc,id))
        for(const auto& [kind,ref]:ComponentUIRefs(*next))e.refs.erase(kind);
    e.setId=id;e.confirmed=false;
}
static void RenderReferenceGroupLabel(const char* label) {
    ImGui::Spacing();ImGui::TextUnformatted(label);ImGui::Separator();
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
    }
    return changed;
}
static std::string ComponentUIFashionName(const std::string& id){
    if(id.empty())return "None";
    for(const auto& look:g_FashionLibrary.looks)if(look.id==id)return look.name;
    return "(missing saved appearance)";
}
static constexpr int kEquipmentReferenceOrder[]={2,4,6,5,3,8,7};
static void RenderReferenceEditor(){
    using namespace AlterEgo;
    auto& e=g_ReferenceEditor;
    if(!e.open)return;
    const char* popup=e.isSet?"Edit Equipment Set###ae_reference_editor":"Edit Loadout Components###ae_reference_editor";
    if(!ImGui::IsPopupOpen(popup))ImGui::OpenPopup(popup);
    const auto display=ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowSize(ImVec2(std::min(580.f,display.x-24.f),std::min(620.f,display.y-24.f)),ImGuiCond_Appearing);
    if(!ImGui::BeginPopupModal(popup,&e.open))return;
    const auto& doc=ComponentUISnapshot();
    const auto* owner=e.isSet?ComponentUIFindSet(doc,e.id):ComponentUIFindLoadout(doc,e.id);
    const bool stale=e.revision!=GW2API::GetBuildLibraryRevision()||!owner;
    if(!e.isSet)ImGui::TextWrapped("%s",e.name.c_str());
    if(stale)ImGui::TextWrapped("Library changed. Cancel and reopen to avoid overwriting newer changes.");
    ImGui::BeginChild("##reference_editor_fields",ImVec2(0,-110),false);
    if(e.isSet){ImGui::SetNextItemWidth(-1);UI::InputText("Name",e.name);}
    const auto renderRows=[&](const char* table,const int* kinds,int count){
        if(ImGui::BeginTable(table,2,ImGuiTableFlags_SizingStretchProp)){
            ImGui::TableSetupColumn("Category",ImGuiTableColumnFlags_WidthFixed,116.f);
            ImGui::TableSetupColumn("Selection",ImGuiTableColumnFlags_WidthStretch);
            const auto* linked=e.isSet?nullptr:ComponentUIFindSet(doc,e.setId);
            for(int j=0;j<count;++j){
                const std::string kind=kComponentKinds[kinds[j]].kind;
                ImGui::PushID(kind.c_str());ImGui::TableNextRow();ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();ImGui::TextUnformatted(ComponentLabel(kind));
                ImGui::TableSetColumnIndex(1);
                if(linked&&linked->at("refs").contains(kind)){
                    const auto id=linked->at("refs").value(kind,std::string());
                    ImGui::TextWrapped("%s",ComponentUIRefName(doc,kind,id).c_str());
                    ImGui::TextDisabled("From Equipment Set");
                }else if(RenderReferencePicker(doc,e.isSet?nullptr:owner,kind,e.refs,e.isSet))e.confirmed=false;
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    };
    if(owner&&doc.contains("components")){
        if(!e.isSet){
            RenderReferenceGroupLabel("Build");
            const int buildKinds[]={0,1};renderRows("##draft_build",buildKinds,2);
        }
        RenderReferenceGroupLabel("Equipment");
        if(!e.isSet){
            const auto* linked=ComponentUIFindSet(doc,e.setId);
            const std::string shown=linked?linked->value("name",e.setId):"Independent components";
            ImGui::TextUnformatted("Equipment Set");ImGui::SetNextItemWidth(-1);
            if(UI::BeginCombo("##draft_set",shown.c_str())){
                if(UI::SelectableOption("Independent components",e.setId.empty()))DraftSelectEquipmentSet(doc,"");
                if(doc.contains("equipment_sets"))for(const auto& set:doc.at("equipment_sets")){
                    const auto id=set.value("id",std::string());
                    if(UI::SelectableOption((set.value("name",id)+"##"+id).c_str(),id==e.setId))DraftSelectEquipmentSet(doc,id);
                }
                ImGui::EndCombo();
            }
        }
        renderRows("##draft_equipment",kEquipmentReferenceOrder,IM_ARRAYSIZE(kEquipmentReferenceOrder));
        if(!e.isSet){
            RenderReferenceGroupLabel("Fashion");
            ImGui::SetNextItemWidth(-1);
            if(UI::BeginCombo("##draft_fashion",ComponentUIFashionName(e.fashionId).c_str())){
                if(UI::SelectableOption("None",e.fashionId.empty()))e.fashionId.clear();
                for(const auto& look:g_FashionLibrary.looks)
                    if(UI::SelectableOption((look.name+"##"+look.id).c_str(),look.id==e.fashionId))e.fashionId=look.id;
                ImGui::EndCombo();
            }
        }
    }
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
            if(!g_LibEditMode||e.id!=g_EditDraftId||g_GearDialogOpen)
                e.error="Parent Loadout draft is no longer available.";
            else if(GW2API::RebaseLoadoutDraft(g_EditDraft,g_EditSetId,g_EditFashionId,g_EditRefs,
                        e.setId,e.fashionId,e.refs,e.revision,e.error)){
                g_EditRefs=e.refs;g_EditSetId=e.setId;g_EditFashionId=e.fashionId;
                // Consent was for the previous identities, never transfer it
                // silently to a newly selected component.
                g_EditSharedKinds.clear();
                MarkEditDirty();e.open=false;ImGui::CloseCurrentPopup();
            }
        }else{
            g_ComponentCommand={e.isSet?ComponentUICommand::SaveSetDraft:ComponentUICommand::SaveReferences,e.id,"",e.setId,e.name,
                ComponentJson{{"refs",e.refs},{"fashion",e.fashionId}},e.revision};
            e.queued=true;e.open=false;ImGui::CloseCurrentPopup();
        }
    }else if(action==UI::EditAction::Cancel){e={};ImGui::CloseCurrentPopup();}
    ImGui::EndPopup();
}
// Read-only reference rows are identical in normal and aggregate edit views.
// Payload edits have their own Save/Cancel dialog; reference changes are staged separately.
static void RenderReferencePreviewRow(const ComponentJson& doc,const std::string& kind,const std::string& ref,
    const std::string& loadoutId,bool inherited,bool actions,const char* empty="Not set"){
    ImGui::PushID(kind.c_str());ImGui::TableNextRow();ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(ComponentLabel(kind));ImGui::TableSetColumnIndex(1);
    ImGui::TextWrapped("%s",ComponentUIRefName(doc,kind,ref,empty).c_str());
    if(inherited)ImGui::TextDisabled("From Equipment Set");
    const auto* part=ComponentUIFind(doc,kind,ref);
    if(actions&&part){
        const auto fit=[&](const char* label){
            const float right=ImGui::GetCursorScreenPos().x+ImGui::GetContentRegionAvail().x;
            if(right-ImGui::GetItemRectMax().x>=AlterEgo::UI::CompactButtonWidth(label)+ImGui::GetStyle().ItemSpacing.x)ImGui::SameLine();
        };
        fit("Open");
        if(AlterEgo::UI::Button("Open##ref",ImVec2(0,0),AlterEgo::UI::ButtonRole::Compact))ComponentUIOpenLibrary(kind,ref);
        // Loadout payloads are edited through the parent draft. Standalone
        // Equipment Sets keep their explicit shared-component editor.
        if(loadoutId.empty()){
            fit("Edit Shared");
            if(AlterEgo::UI::Button("Edit Shared##ref",ImVec2(0,0),AlterEgo::UI::ButtonRole::Compact))ComponentUIStartEditor("edit",kind,ref,part);
        }
    }
    ImGui::PopID();
}
static void RenderLoadoutComponentSelectors(const std::string& loadoutId) {
    using namespace AlterEgo;
    const auto& doc=ComponentUISnapshot();
    const auto* l=ComponentUIFindLoadout(doc,loadoutId);
    if(!l)return;
    static bool expanded=false;
    ImGui::PushID(loadoutId.c_str());
    if(AlterEgo::UI::Button(expanded?"Loadout Components -##loadout_selector":"Loadout Components +##loadout_selector",ImVec2(0,0),AlterEgo::UI::ButtonRole::Compact))expanded=!expanded;
    ImGui::PopID();
    if(!expanded)return;
    const bool editing=g_LibEditMode&&g_EditRefsReady&&loadoutId==g_EditDraftId;
    const auto direct=editing?g_EditRefs:ComponentUIRefs(*l);
    const auto linkedId=editing?g_EditSetId:l->value("equipment_set_id",std::string());
    const auto* linked=ComponentUIFindSet(doc,linkedId);
    const auto inherited=linked?ComponentUIRefs(*linked):ReferenceMap{};
    if(editing){
        if(!g_GearDialogOpen&&RenderActionButton("Change Components"))BeginReferenceEditor(*l,false);
        ImGui::TextWrapped("Edit gear and skills below. When saving, review any shared components and exact matches.");
    }else if(g_LibEditMode)ImGui::TextDisabled("Component draft unavailable; cancel and reopen Edit.");
    else if(RenderActionButton("Edit Components")){
        for(const auto& build:GW2API::GetSavedBuilds())if(build.id==loadoutId){
            g_LibEditBuildId=build.id;EnterBuildEditMode(build);
            if(g_EditRefsReady)BeginReferenceEditor(*l,false);
            break;
        }
    }
    const auto rows=[&](const char* table,const int* kinds,int count){
        if(ImGui::BeginTable(table,2,ImGuiTableFlags_SizingStretchProp)){
            ImGui::TableSetupColumn("Category",ImGuiTableColumnFlags_WidthFixed,110.f);
            ImGui::TableSetupColumn("Selection",ImGuiTableColumnFlags_WidthStretch);
            for(int j=0;j<count;++j){
                const std::string kind=kComponentKinds[kinds[j]].kind;
                const bool shared=inherited.count(kind)!=0;
                const auto ref=ComponentUIRef(shared?inherited:direct,kind);
                RenderReferencePreviewRow(doc,kind,ref,loadoutId,shared,!g_LibEditMode);

            }
            ImGui::EndTable();
        }
    };
    RenderReferenceGroupLabel("Build");
    const int buildKinds[]={0,1};rows("##loadout_build_refs",buildKinds,2);
    RenderReferenceGroupLabel("Equipment");
    ImGui::TextUnformatted("Equipment Set");ImGui::SameLine();
    ImGui::TextWrapped("%s",linked?linked->value("name",linkedId).c_str():"Independent components");
    if(!g_LibEditMode){
        if(linked){
            if(RenderActionButton("Open Set")){g_EquipmentSetSelectedId=linkedId;g_LibraryCollection=3;MarkNavigation();}
            ImGui::SameLine();
            if(RenderActionButton("Edit Shared Set"))BeginReferenceEditor(*linked,true);
            ImGui::SameLine();
            if(RenderActionButton("Detach Set")){
                BeginReferenceEditor(*l,false);DraftSelectEquipmentSet(doc,"");
            }
            ImGui::SameLine();
        }
        static std::string setName;
        static std::set<std::string> setKinds;
        static uint64_t setDraftRevision=0;
        if(RenderActionButton(linked?"Copy Set":"Save as Equipment Set")){
            setName=(linked?linked->value("name",linkedId):l->at("metadata").value("name",loadoutId))+(linked?" - Copy":" - Equipment");
            setKinds.clear();
            for(const auto& [kind,id]:(linked?inherited:direct))if(Composable::IsEquipmentKind(kind))setKinds.insert(kind);
            setDraftRevision=GW2API::GetBuildLibraryRevision();ImGui::OpenPopup("Name equipment set##loadout");
        }
        if(ImGui::BeginPopupModal("Name equipment set##loadout",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
            UI::InputText("Name",setName);
            for(const auto& entry:kComponentKinds)if(Composable::IsEquipmentKind(entry.kind)){
                bool included=setKinds.count(entry.kind)!=0;
                if(ImGui::Checkbox(entry.label,&included)){if(included)setKinds.insert(entry.kind);else setKinds.erase(entry.kind);}
            }
            ImGui::TextWrapped("Save creates and links a separate named set. Existing component payloads remain shared.");
            const auto action=UI::SaveCancel("new_set",!setKinds.empty()&&!Composable::ComponentNameKey(setName).empty());
            if(action==UI::EditAction::Save){
                g_ComponentCommand={ComponentUICommand::SaveSet,loadoutId,"","",setName,ComponentJson(setKinds),setDraftRevision};
                ImGui::CloseCurrentPopup();
            }else if(action==UI::EditAction::Cancel)ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }
    rows("##loadout_equipment_refs",kEquipmentReferenceOrder,IM_ARRAYSIZE(kEquipmentReferenceOrder));
    RenderReferenceGroupLabel("Fashion");
    ImGui::TextWrapped("%s",ComponentUIFashionName(editing?g_EditFashionId:l->value("fashion_id",std::string())).c_str());
    ImGui::Separator();
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

static void RenderComponentLibrary() {
    const auto& doc = ComponentUISnapshot();
    if (!doc.contains("components") || !doc["components"].is_object()) {
        ImGui::TextDisabled("Component library is not available.");
        RenderComponentUIEditor();
        return;
    }
    const char* sorts[] = {"Name", "Most used", "Least used"};
    AlterEgo::UI::SearchToolbar("components", "Search components...", g_ComponentSearch,
        [&] {
            AlterEgo::UI::FilterChips chips;
            const char* kindLabel = ComponentLabel(kComponentKinds[g_ComponentFilterKind].kind);
            if (g_ComponentFilterKind != 0)
                chips.push_back({"kind",kindLabel,[&] {g_ComponentFilterKind=0;g_ComponentSelectedId.clear();}});
            else chips.push_back({"kind",kindLabel,{}}); // required default scope
            if(g_ComponentUnusedOnly) chips.push_back({"usage","Unused only",[&] {g_ComponentUnusedOnly=false;}});
            if(g_ComponentSort != 0) chips.push_back({"sort",sorts[g_ComponentSort],[&] {g_ComponentSort=0;}});
            return chips;
        },
        [&] {
            const char* kinds[9];
            for (int n=0;n<9;++n) kinds[n]=kComponentKinds[n].label;
            if (AlterEgo::UI::FilterChoice("Component type", g_ComponentFilterKind, kinds, 9))
                g_ComponentSelectedId.clear();
            ImGui::Checkbox("Unused only", &g_ComponentUnusedOnly);
            AlterEgo::UI::FilterChoice("Sort", g_ComponentSort, sorts, IM_ARRAYSIZE(sorts));
        },
        [&] {
            if (g_ComponentFilterKind != 0) g_ComponentSelectedId.clear();
            g_ComponentFilterKind=0; g_ComponentUnusedOnly=false; g_ComponentSort=0;
        });
    const std::string kind = kComponentKinds[g_ComponentFilterKind].kind;
    if (!g_ComponentSelectIdPending.empty()) {
        g_ComponentSelectedKind = kind;
        g_ComponentSelectedId = g_ComponentSelectIdPending;
        g_ComponentSelectIdPending.clear();
    }
    if (!g_ComponentSelectNamePending.empty()) {
        const auto group = doc["components"].find(kind);
        if (group != doc["components"].end() && group->is_array())
            for (auto it = group->rbegin(); it != group->rend(); ++it)
                if (it->is_object() &&
                    it->value("name", std::string()) == g_ComponentSelectNamePending) {
                    g_ComponentSelectedId = it->value("id", std::string());
                    g_ComponentSelectedKind = kind;
                    break;
                }
        g_ComponentSelectNamePending.clear();
    }
    if (RenderActionButton("New Component##library_component"))
        ComponentUIStartEditor("create", kind, "", nullptr);
    ImGui::SameLine();
    if (RenderActionButton("Import Component##clipboard")) {
        const char* clipboard = ImGui::GetClipboardText();
        g_ComponentStatus.clear();
        if (!clipboard || !*clipboard) {
            g_ComponentStatus = "Clipboard is empty.";
        } else {
            try {
                const ComponentJson packet = ComponentJson::parse(clipboard);
                std::string importedKind, error;
                AlterEgo::Composable::Component imported;
                if (!AlterEgo::Composable::ImportComponent(
                        packet, importedKind, imported, error))
                    g_ComponentStatus = error;
                else {
                    g_ComponentCommand = {ComponentUICommand::Create, "",
                        importedKind, "", imported.name, imported.data};
                    for (int i = 0; i < 9; ++i)
                        if (importedKind == kComponentKinds[i].kind)
                            g_ComponentFilterKind = i;
                }
            } catch (const std::exception& e) {
                g_ComponentStatus = std::string("Invalid component clipboard JSON: ") + e.what();
            }
        }
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Import a standalone component copied from Alter Ego.\n"
                          "Creates a new independent ID; does not replace existing Loadouts.");
    if (!g_ComponentStatus.empty())
        ImGui::TextWrapped("%s", g_ComponentStatus.c_str());
    ImGui::Separator();

    const AlterEgo::UI::SplitPanel split(g_ComponentListWidth,270.f,160.f,220.f);
    ImGui::BeginChild("##component_list", ImVec2(split.left, 0), true);
    const auto group = doc["components"].find(kind);
    if (group != doc["components"].end() && group->is_array()) {
        std::vector<const ComponentJson*> visible;
        for (const auto& item : *group) {
            if (!item.is_object() || !item.contains("data") ||
                !item["data"].is_object()) continue;
            const std::string id = item.value("id", std::string());
            const std::string name = item.value("name", id);
            if (g_ComponentUnusedOnly && ComponentUIUsage(doc, kind, id) > 0)
                continue;
            if (!ComponentUISearchMatch(name, g_ComponentSearch) &&
                !ComponentUISearchMatch(id, g_ComponentSearch) &&
                !ComponentUISearchMatch(ComponentUIShortDescription(kind, item["data"]),
                                         g_ComponentSearch)) continue;
            visible.push_back(&item);
        }
        std::stable_sort(visible.begin(), visible.end(),
            [&](const ComponentJson* a, const ComponentJson* b) {
                const int usesA = ComponentUIUsage(doc, kind,
                    a->value("id", std::string()));
                const int usesB = ComponentUIUsage(doc, kind,
                    b->value("id", std::string()));
                if (g_ComponentSort != 0 && usesA != usesB)
                    return g_ComponentSort == 1 ? usesA > usesB : usesA < usesB;
                std::string left = a->value("name", std::string());
                std::string right = b->value("name", std::string());
                std::transform(left.begin(), left.end(), left.begin(),
                    [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
                std::transform(right.begin(), right.end(), right.begin(),
                    [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
                return left < right;
            });
        ImGui::TextDisabled("%zu of %zu %s component%s", visible.size(), group->size(),
            ComponentLabel(kind), group->size() == 1 ? "" : "s");
        for (const auto* item : visible) {
            const std::string id = item->value("id", std::string());
            const std::string name = item->value("name", id);
            const int users = ComponentUIUsage(doc, kind, id);
            ImGui::PushID(id.c_str());
            const std::string shown = name + "  [" + std::to_string(users) + "]";
            if (ImGui::Selectable(shown.c_str(),
                    g_ComponentSelectedKind == kind && g_ComponentSelectedId == id)) {
                MarkNavigation();g_ComponentSelectedId = id;
                g_ComponentSelectedKind = kind;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s\nReferenced by %d records", id.c_str(), users);
            if(ImGui::BeginPopupContextItem("##component_name")){
                CopyNameMenuItem(name);ImGui::EndPopup();
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    if(split.Divider("##component_split",g_ComponentListWidth))SaveSettings();
    ImGui::BeginChild("##component_detail", ImVec2(0, 0), true);
    const auto* item = (g_ComponentSelectedKind == kind)
        ? ComponentUIFind(doc, kind, g_ComponentSelectedId) : nullptr;
    if (!item) ImGui::TextDisabled("Choose a saved component to view or edit.");
    else {
        const std::string id = item->value("id", std::string());
        const std::string name = item->value("name", id);
        const int usage = ComponentUIUsage(doc, kind, id);
        ImGui::TextWrapped("%s", name.c_str());
        ImGui::TextDisabled("%s | %d Loadout%s", ComponentLabel(kind),
                            usage, usage == 1 ? "" : "s");
        ImGui::TextWrapped("%s", ComponentUIShortDescription(kind, item->at("data")).c_str());
        ImGui::Separator();
        if (RenderActionButton("Copy Component##clipboard")) {
            AlterEgo::Composable::Component exported;
            exported.name = name;
            exported.data = item->at("data");
            const std::string portable = AlterEgo::Composable::ExportComponent(
                kind, exported).dump(2);
            ImGui::SetClipboardText(portable.c_str());
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Copy a portable independent component for importing elsewhere.\n"
                              "Includes unknown stored fields but not the current library ID.");
        ImGui::SameLine();
        if (RenderActionButton("Rename##component_library"))
            ComponentUIStartEditor("rename", kind, id, item);
        ImGui::SameLine();
        if (RenderActionButton("Duplicate##component_library"))
            ComponentUIStartEditor("duplicate", kind, id, item);
        ImGui::SameLine();
        if (RenderActionButton("Edit Fields##component_library"))
            ComponentUIStartEditor("edit", kind, id, item);
        if (usage == 0) {
            if (AlterEgo::UI::Button("Delete Unused##component_library", ImVec2(0,0), AlterEgo::UI::ButtonRole::Destructive))
                ImGui::OpenPopup("Delete unused component##ae");
            if (ImGui::BeginPopupModal("Delete unused component##ae", nullptr,
                                      ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::TextWrapped("Delete %s? This component is not referenced by a Loadout.",
                                   name.c_str());
                if (AlterEgo::UI::Button("Delete##component_confirm", ImVec2(0,0), AlterEgo::UI::ButtonRole::Destructive)) {
                    g_ComponentCommand = {ComponentUICommand::RemoveUnused,
                        "", kind, id, "", ComponentJson::object()};
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (RenderActionButton("Cancel##component_confirm"))
                    ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
        } else {
            ImGui::TextDisabled("Referenced components cannot be deleted.");
        }
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
        if (ImGui::CollapsingHeader("Advanced: raw component data##preview")) {
            ImGui::BeginChild("##component_json_preview", ImVec2(0, 180), true);
            ImGui::TextUnformatted(item->at("data").dump(2).c_str());
            ImGui::EndChild();
        }
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
                            g_LibSelectedIdx = index;
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

    }
    ImGui::EndChild();
    RenderComponentUIEditor();
}

static std::string g_EquipmentSetSearch;
static void RenderEquipmentSetLibrary() {
    using namespace AlterEgo;
    const auto& doc=ComponentUISnapshot();
    auto& search=g_EquipmentSetSearch;
    UI::SearchField("equipment_sets_search","Search equipment sets...",search);
    if(!doc.contains("equipment_sets")||doc.at("equipment_sets").empty()) {
        ImGui::TextDisabled("No equipment sets saved. Use Save to Library on a character.");
        return;
    }
    const ComponentJson* selected=nullptr;
    const auto& sets=doc.at("equipment_sets");
    ImGui::BeginChild("##equipment_set_list",ImVec2(0,150),true);
    for(const auto& set:sets) {
        const auto id=set.value("id",std::string()),name=set.value("name",id);
        if(id==g_EquipmentSetSelectedId)selected=&set;
        if(!search.empty()) {
            auto lower=[](std::string value){for(char& c:value)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return value;};
            if(lower(name).find(lower(search))==std::string::npos)continue;
        }
        if(UI::SelectableOption((name+"##"+id).c_str(),id==g_EquipmentSetSelectedId)) {
            MarkNavigation();g_EquipmentSetSelectedId=id;selected=&set;
        }
        ImGui::PushID(id.c_str());
        if(ImGui::BeginPopupContextItem("##set_name")){CopyNameMenuItem(name);ImGui::EndPopup();}
        ImGui::PopID();
    }
    ImGui::EndChild();
    if(!selected){ImGui::TextDisabled("Select an equipment set.");return;}
    const auto id=selected->value("id",std::string()),name=selected->value("name",id);
    ImGui::TextUnformatted(name.c_str());
    int users=0;
    for(const auto& l:doc.at("loadouts"))if(l.value("equipment_set_id",std::string())==id)++users;
    ImGui::TextDisabled("Used by %d Loadout(s)",users);
    for(const auto& l:doc.at("loadouts"))if(l.value("equipment_set_id",std::string())==id)
        ImGui::BulletText("%s",l.at("metadata").value("name",l.value("id",std::string())).c_str());
    static std::string duplicateName;
    static uint64_t duplicateRevision=0;
    if(RenderActionButton("Edit##equipment_set"))BeginReferenceEditor(*selected,true);
    ImGui::SameLine();
    if(RenderActionButton("Duplicate##equipment_set")){duplicateName=name+" - Copy";duplicateRevision=GW2API::GetBuildLibraryRevision();ImGui::OpenPopup("Duplicate equipment set");}
    if(ImGui::BeginPopupModal("Duplicate equipment set",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        UI::InputText("Name",duplicateName);
        const auto action=UI::SaveCancel("duplicate_set",!Composable::ComponentNameKey(duplicateName).empty()&&duplicateRevision==GW2API::GetBuildLibraryRevision());
        if(action==UI::EditAction::Save) {
            g_ComponentCommand={ComponentUICommand::DuplicateSet,"","",id,duplicateName,ComponentJson::object(),duplicateRevision};
            ImGui::CloseCurrentPopup();
        }else if(action==UI::EditAction::Cancel)ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if(!users) {
        if(UI::Button("Delete##equipment_set",ImVec2(0,0),UI::ButtonRole::Destructive))ImGui::OpenPopup("Delete equipment set?");
    } else ImGui::TextDisabled("Referenced sets cannot be deleted.");
    if(ImGui::BeginPopupModal("Delete equipment set?",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("Delete this unused named set? Its components are retained.");
        if(UI::Button("Delete",ImVec2(0,0),UI::ButtonRole::Destructive)) {
            g_ComponentCommand={ComponentUICommand::RemoveSet,"","",id,"",ComponentJson::object()};
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();if(RenderActionButton("Cancel"))ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::Separator();
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
        success=API::DuplicateEquipmentSet(command.component_id,command.name,&g_EquipmentSetSelectedId);
        break;
    case ComponentUICommand::AssignSetPart:
        success=API::AssignEquipmentSetComponent(command.loadout_id,command.kind,command.component_id,command.expected_revision);
        break;
    case ComponentUICommand::RenameSet:
        success=API::RenameEquipmentSet(command.component_id,command.name);
        break;
    case ComponentUICommand::RemoveSet:
        success=API::RemoveEquipmentSet(command.component_id);
        if(success)g_EquipmentSetSelectedId.clear();
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
            if (!builds.empty()) g_LibSelectedIdx = static_cast<int>(builds.size()) - 1;
            g_LibDetailsFetched = false;
        }
        break;
    case ComponentUICommand::RemoveUnused:
        success = API::DeleteUnusedLoadoutComponent(command.kind, command.component_id);
        if (success && g_ComponentSelectedId == command.component_id)
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
