#pragma once
// UI for independently saved Loadout components.
//
// Include from dllmain.cpp after the existing UI globals/helper controls.
// All mutations are queued and applied after the visible panes finish
// rendering: SavedBuild references remain stable for the whole frame.
#include "ComposableSemantics.h"
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
    {"armor", "Armor"},
    {"trinkets", "Trinkets"},
    {"runes", "Runes"},
    {"sigils", "Sigils"},
    {"infusions", "Infusions"},
    {"relic", "Relic"}
};

static const char* ComponentLabel(const std::string& kind) {
    for (const auto& entry : kComponentKinds)
        if (kind == entry.kind) return entry.label;
    return "Unknown";
}

struct ComponentUICommand {
    enum Type { None, Assign, CopyForLoadout, SetFashion, Create, Duplicate,
                Rename, EditShared, RemoveUnused } type = None;
    std::string loadout_id, kind, component_id, name;
    ComponentJson data = ComponentJson::object();
};
static ComponentUICommand g_ComponentCommand;
static std::string g_ComponentSearch;
static std::string g_ComponentSelectedId;
static std::string g_ComponentSelectedKind;
static std::string g_ComponentEditorId;
static std::string g_ComponentEditorKind;
static std::string g_ComponentEditorName;
static std::string g_ComponentEditorData;
static std::string g_ComponentEditorError;
static bool g_ComponentEditorOpen = false;
static bool g_ComponentEditorNew = false;
static std::string g_ComponentEditorMode; // create, duplicate, rename, edit
static std::string g_ComponentStatus;

static const ComponentJson& ComponentUISnapshot() {
    static ComponentJson cached = ComponentJson::object();
    static uint64_t previousRevision = 0;
    const uint64_t revision = AlterEgo::GW2API::GetBuildLibraryRevision();
    if (revision != previousRevision || !cached.contains("loadouts")) {
        ComponentJson latest;
        if (AlterEgo::GW2API::GetComposableLoadouts(latest))
            cached = std::move(latest);
        else cached = ComponentJson::object();
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
    int count = 0;
    if (!doc.contains("loadouts") || !doc["loadouts"].is_array()) return 0;
    for (const auto& loadout : doc["loadouts"]) {
        if (!loadout.is_object() || !loadout.contains("refs") ||
            !loadout["refs"].is_object()) continue;
        auto ref = loadout["refs"].find(kind);
        if (ref != loadout["refs"].end() && ref->is_string() &&
            ref->get<std::string>() == id) ++count;
    }
    return count;
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
static bool ComponentUISearchMatch(std::string text, std::string needle) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    std::transform(needle.begin(), needle.end(), needle.begin(),
                   [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    return text.find(needle) != std::string::npos;
}
static void ComponentUIStartEditor(const std::string& mode,
                                   const std::string& kind,
                                   const std::string& id,
                                   const ComponentJson* component) {
    g_ComponentEditorMode = mode;
    g_ComponentEditorKind = kind;
    g_ComponentEditorId = id;
    g_ComponentEditorName = component
        ? component->value("name", std::string()) : std::string("New ") + ComponentLabel(kind);
    if (mode == "duplicate") g_ComponentEditorName += " (Copy)";
    g_ComponentEditorData = component && component->contains("data")
        ? component->at("data").dump(2) : std::string("{}");
    g_ComponentEditorError.clear();
    g_ComponentEditorOpen = true;
}
static bool ComponentUIEditorCanCommit() {
    if (g_ComponentEditorName.empty()) {
        g_ComponentEditorError = "Give the component a name.";
        return false;
    }
    if (g_ComponentEditorMode != "edit" && g_ComponentEditorMode != "create")
        return true;
    try {
        const auto parsed = ComponentJson::parse(g_ComponentEditorData);
        if (!parsed.is_object()) {
            g_ComponentEditorError = "Component data must be a JSON object.";
            return false;
        }
    } catch (const std::exception& e) {
        g_ComponentEditorError = std::string("Invalid component data: ") + e.what();
        return false;
    }
    return true;
}
static void RenderComponentUIEditor() {
    if (g_ComponentEditorOpen && !ImGui::IsPopupOpen("Component editor##ae_components"))
        ImGui::OpenPopup("Component editor##ae_components");
    ImGui::SetNextWindowSize(ImVec2(590.0f, 0), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Component editor##ae_components",
                                &g_ComponentEditorOpen,
                                ImGuiWindowFlags_AlwaysAutoResize)) return;

    ImGui::Text("%s - %s", ComponentLabel(g_ComponentEditorKind),
                g_ComponentEditorMode == "edit" ? "Edit shared data" :
                g_ComponentEditorMode == "duplicate" ? "Duplicate" :
                g_ComponentEditorMode == "create" ? "Create" : "Rename");
    ImGui::TextDisabled("Component records can be reused by multiple Loadouts.");
    ImGui::SetNextItemWidth(-1);
    InputTextString("Name##component_name", g_ComponentEditorName);
    if (g_ComponentEditorMode == "edit" || g_ComponentEditorMode == "create") {
        ImGui::Spacing();
        ImGui::TextWrapped(
            "Advanced component fields (JSON). Edit the visible Loadout's regular "
            "equipment and skill panels for guided editing. Changes to this "
            "shared component affect every referencing Loadout.");
        ImGui::SetNextItemWidth(-1);
        InputTextMultilineString("Data##component_json", g_ComponentEditorData,
                                 ImVec2(530, 230));
    }
    if (!g_ComponentEditorError.empty())
        ImGui::TextWrapped("%s", g_ComponentEditorError.c_str());
    if (RenderActionButton("Save##component_editor")) {
        if (ComponentUIEditorCanCommit()) {
            ComponentUICommand cmd;
            cmd.kind = g_ComponentEditorKind;
            cmd.component_id = g_ComponentEditorId;
            cmd.name = g_ComponentEditorName;
            if (g_ComponentEditorMode == "create") {
                cmd.type = ComponentUICommand::Create;
                cmd.data = ComponentJson::parse(g_ComponentEditorData);
            } else if (g_ComponentEditorMode == "duplicate")
                cmd.type = ComponentUICommand::Duplicate;
            else if (g_ComponentEditorMode == "rename")
                cmd.type = ComponentUICommand::Rename;
            else {
                cmd.type = ComponentUICommand::EditShared;
                cmd.data = ComponentJson::parse(g_ComponentEditorData);
            }
            g_ComponentCommand = std::move(cmd);
            g_ComponentEditorOpen = false;
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::SameLine();
    if (RenderActionButton("Cancel##component_editor")) {
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

static void RenderLoadoutComponentSelectors(const std::string& loadoutId) {
    const ComponentJson& doc = ComponentUISnapshot();
    const auto* loadout = ComponentUIFindLoadout(doc, loadoutId);
    if (!loadout || !loadout->contains("refs")) return;
    if (!ImGui::CollapsingHeader("Loadout components##loadout_selector",
                                 ImGuiTreeNodeFlags_DefaultOpen)) return;
    if (g_LibEditMode) {
        ImGui::TextDisabled("Finish or cancel the current edit before switching components.");
    } else {
        ImGui::TextDisabled("Choose reusable components. Equipment details remain below.");
    }
    if (g_LibEditMode) return;
    if (ImGui::BeginTable("##loadout_component_grid", 2,
        ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("Component", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableSetupColumn("Saved selection", ImGuiTableColumnFlags_WidthStretch);
        for (int i = 0; i < 9; ++i) {
            const std::string kind = kComponentKinds[i].kind;
            const auto ref = (*loadout)["refs"].find(kind);
            const std::string id = ref != (*loadout)["refs"].end() && ref->is_string()
                ? ref->get<std::string>() : std::string();
            const auto* active = ComponentUIFind(doc, kind, id);
            const std::string shown = active
                ? active->value("name", std::string("Unnamed")) : "(not set)";
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(ComponentLabel(kind));
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(ImMax(110.f, ImGui::GetContentRegionAvail().x - 162.f));
            if (ImGui::BeginCombo("##part", shown.c_str())) {
                const auto group = doc["components"].find(kind);
                if (group != doc["components"].end() && group->is_array()) {
                    for (const auto& candidate : *group) {
                        if (!ComponentUICompatible(doc, *loadout, kind, candidate)) continue;
                        const std::string candidateId = candidate.value("id", std::string());
                        const std::string candidateName = candidate.value("name", candidateId);
                        const bool current = candidateId == id;
                        if (ImGui::Selectable((candidateName + "##" + candidateId).c_str(), current)) {
                            g_ComponentCommand = {ComponentUICommand::Assign,
                                loadoutId, kind, candidateId, "", ComponentJson::object()};
                        }
                        if (ImGui::IsItemHovered() && candidate.contains("data"))
                            ImGui::SetTooltip("%s | Used in %d Loadouts",
                                ComponentUIShortDescription(kind, candidate["data"]).c_str(),
                                ComponentUIUsage(doc, kind, candidateId));
                        if (current) ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            if (active && ImGui::IsItemHovered())
                ImGui::SetTooltip("%s | Used in %d Loadouts",
                    ComponentUIShortDescription(kind, (*active)["data"]).c_str(),
                    ComponentUIUsage(doc, kind, id));
            ImGui::SameLine();
            if (RenderActionButton("Copy##part")) {
                g_ComponentCommand = {ComponentUICommand::CopyForLoadout,
                    loadoutId, kind, id, shown + " (Copy)", ComponentJson::object()};
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Create an independent copy, then edit without affecting other Loadouts.");
            ImGui::SameLine();
            if (active && RenderActionButton("Edit##part")) {
                const int usage = ComponentUIUsage(doc, kind, id);
                ComponentUIStartEditor("edit", kind, id, active);
                if (usage > 1)
                    g_ComponentEditorError = "Shared by " + std::to_string(usage) +
                        " Loadouts. Edits here will update all of them. "
                        "Use Copy to edit only this Loadout.";
            }
            if (active && ImGui::IsItemHovered()) {
                const int usage = ComponentUIUsage(doc, kind, id);
                ImGui::SetTooltip(usage > 1
                    ? "Edit SHARED component (%d Loadouts). Use Copy for a private edit."
                    : "Edit this component's stored fields.", usage);
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::TextUnformatted("Fashion");
    ImGui::SameLine(116.0f);
    std::string fashionId = loadout->value("fashion_id", std::string());
    std::string fashionName = "(none)";
    for (const auto& look : g_FashionLibrary.looks)
        if (look.id == fashionId) { fashionName = look.name; break; }
    if (!fashionId.empty() && fashionName == "(none)") fashionName = "(missing saved appearance)";
    ImGui::SetNextItemWidth(ImMax(150.f, ImGui::GetContentRegionAvail().x - 8.f));
    if (ImGui::BeginCombo("##loadoutFashion", fashionName.c_str())) {
        if (ImGui::Selectable("None", fashionId.empty()))
            g_ComponentCommand = {ComponentUICommand::SetFashion,
                loadoutId, "", "", "", ComponentJson::object()};
        for (const auto& look : g_FashionLibrary.looks)
            if (ImGui::Selectable((look.name + "##" + look.id).c_str(), look.id == fashionId))
                g_ComponentCommand = {ComponentUICommand::SetFashion,
                    loadoutId, "", look.id, "", ComponentJson::object()};
        ImGui::EndCombo();
    }
    ImGui::Separator();
}

static void RenderComponentLibrary() {
    const auto& doc = ComponentUISnapshot();
    if (!doc.contains("components") || !doc["components"].is_object()) {
        ImGui::TextDisabled("Component library is not available.");
        RenderComponentUIEditor();
        return;
    }
    ImGui::SetNextItemWidth(230);
    if (ImGui::BeginCombo("Component type##library_component_type",
                         ComponentLabel(kComponentKinds[g_ComponentFilterKind].kind))) {
        for (int i = 0; i < 9; ++i)
            if (ImGui::Selectable(kComponentKinds[i].label, i == g_ComponentFilterKind)) {
                g_ComponentFilterKind = i;
                g_ComponentSelectedId.clear();
            }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImMax(150.f, ImGui::GetContentRegionAvail().x - 125.f));
    InputTextString("Search##component_search", g_ComponentSearch);
    const std::string kind = kComponentKinds[g_ComponentFilterKind].kind;
    if (RenderActionButton("New Component##library_component"))
        ComponentUIStartEditor("create", kind, "", nullptr);
    ImGui::Separator();

    const float listWidth = ImMax(200.0f, ImMin(290.0f, ImGui::GetContentRegionAvail().x * 0.37f));
    ImGui::BeginChild("##component_list", ImVec2(listWidth, 0), true);
    const auto group = doc["components"].find(kind);
    if (group != doc["components"].end() && group->is_array()) {
        for (const auto& item : *group) {
            const std::string id = item.value("id", std::string());
            const std::string name = item.value("name", id);
            if (!ComponentUISearchMatch(name, g_ComponentSearch)) continue;
            ImGui::PushID(id.c_str());
            if (ImGui::Selectable(name.c_str(), g_ComponentSelectedId == id)) {
                g_ComponentSelectedId = id;
                g_ComponentSelectedKind = kind;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s\nUsed in %d Loadouts", id.c_str(),
                    ComponentUIUsage(doc, kind, id));
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();
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
        if (RenderActionButton("Rename##component_library"))
            ComponentUIStartEditor("rename", kind, id, item);
        ImGui::SameLine();
        if (RenderActionButton("Duplicate##component_library"))
            ComponentUIStartEditor("duplicate", kind, id, item);
        ImGui::SameLine();
        if (RenderActionButton("Edit Fields##component_library"))
            ComponentUIStartEditor("edit", kind, id, item);
        if (usage == 0) {
            if (RenderActionButton("Delete Unused##component_library"))
                ImGui::OpenPopup("Delete unused component##ae");
            if (ImGui::BeginPopupModal("Delete unused component##ae", nullptr,
                                      ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::TextWrapped("Delete %s? This component is not referenced by a Loadout.",
                                   name.c_str());
                if (RenderActionButton("Delete##component_confirm")) {
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
        ImGui::Spacing();
        ImGui::TextDisabled("Saved fields (read-only preview)");
        ImGui::BeginChild("##component_json_preview", ImVec2(0, 0), true);
        ImGui::TextUnformatted(item->at("data").dump(2).c_str());
        ImGui::EndChild();
    }
    ImGui::EndChild();
    RenderComponentUIEditor();
}

static void ProcessComponentUICommand() {
    if (g_ComponentCommand.type == ComponentUICommand::None) return;
    const auto command = g_ComponentCommand;
    g_ComponentCommand = ComponentUICommand{};
    bool success = false;
    using API = AlterEgo::GW2API;
    switch (command.type) {
    case ComponentUICommand::Assign:
        success = API::AssignLoadoutComponent(
            command.loadout_id, command.kind, command.component_id);
        break;
    case ComponentUICommand::CopyForLoadout:
        success = API::CopyLoadoutComponent(
            command.loadout_id, command.kind, command.name);
        break;
    case ComponentUICommand::SetFashion:
        success = API::SetLoadoutFashion(command.loadout_id, command.component_id);
        break;
    case ComponentUICommand::Create:
        success = API::CreateLoadoutComponent(command.kind, command.name, command.data);
        break;
    case ComponentUICommand::Duplicate:
        success = API::DuplicateLoadoutComponent(
            command.kind, command.component_id, command.name);
        break;
    case ComponentUICommand::Rename:
        success = API::RenameLoadoutComponent(
            command.kind, command.component_id, command.name);
        break;
    case ComponentUICommand::EditShared:
        success = API::EditSharedLoadoutComponent(
            command.kind, command.component_id, command.data);
        if (success && !command.name.empty())
            success = API::RenameLoadoutComponent(
                command.kind, command.component_id, command.name);
        break;
    case ComponentUICommand::RemoveUnused:
        success = API::DeleteUnusedLoadoutComponent(command.kind, command.component_id);
        if (success && g_ComponentSelectedId == command.component_id)
            g_ComponentSelectedId.clear();
        break;
    default: break;
    }
    if (success) {
        g_LibDetailsFetched = false;
        g_EditDraftId.clear();
        g_ComponentStatus.clear();
    } else {
        g_ComponentStatus = "Could not apply component change. Check component compatibility or data fields.";
    }
    if (APIDefs) APIDefs->GUI_SendAlert(success
        ? "Loadout component updated." : g_ComponentStatus.c_str());
}

} // anonymous namespace
