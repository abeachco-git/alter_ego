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
static std::string g_ComponentSkillProfessionHint; // UI-only skill picker filter
static ComponentJson g_ComponentEditorDraft = ComponentJson::object();
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
// Human-readable inventory before the advanced JSON representation. This
// remains deliberately lossless: it reads the same component payload, without
// interpreting missing item names as empty slots.
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
        if (data.contains("relic_name") && data["relic_name"].is_string())
            ImGui::Text("Relic: %s", data["relic_name"].get<std::string>().c_str());
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
            for (auto it = data["gear"].begin(); it != data["gear"].end(); ++it) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(it.key().c_str());
                ImGui::TableSetColumnIndex(1);
                const auto& item = it.value();
                if (!item.is_object()) {
                    ImGui::TextWrapped("%s", item.dump().c_str());
                } else {
                    std::string detail;
                    for (auto field = item.begin(); field != item.end(); ++field) {
                        if (!detail.empty()) detail += " | ";
                        detail += field.key() + ": " + field.value().dump();
                    }
                    ImGui::TextWrapped("%s", detail.empty() ? "(no details)" : detail.c_str());
                }
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
    g_ComponentEditorKind = kind;
    g_ComponentEditorId = id;
    g_ComponentSkillProfessionHint.clear();
    g_ComponentEditorName = component
        ? component->value("name", std::string()) : std::string("New ") + ComponentLabel(kind);
    if (mode == "duplicate") g_ComponentEditorName += " (Copy)";
    g_ComponentEditorDraft = component && component->contains("data") &&
        component->at("data").is_object()
        ? component->at("data") : ComponentJson::object();
    g_ComponentEditorError.clear();
    g_ComponentEditorOpen = true;
}
static bool ComponentUIEditorCanCommit() {
    if (g_ComponentEditorName.empty()) {
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
                g_ComponentEditorMode == "create" ? "Create" : "Rename");
    const int uses = ComponentUIUsage(ComponentUISnapshot(), g_ComponentEditorKind,
                                      g_ComponentEditorId);
    if (g_ComponentEditorMode == "edit") {
        ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.40f, 1.0f),
            "SHARED EDIT: updates %d referencing Loadout%s. Copy first for a private edit.",
            uses, uses == 1 ? "" : "s");
    } else if (g_ComponentEditorMode == "duplicate") {
        ImGui::TextDisabled("Creates an independent component; the source remains unchanged.");
    } else {
        ImGui::TextDisabled("Components may be reused by multiple Loadouts.");
    }
    ImGui::SetNextItemWidth(-1);
    InputTextString("Name##component_name", g_ComponentEditorName);
    if (g_ComponentEditorMode != "rename") {
        ImGui::Separator();
        ImGui::BeginChild("##component_native_editor", ImVec2(0, -60.0f), false);
        RenderComponentNativeEditor(g_ComponentEditorKind, g_ComponentEditorId,
                                    g_ComponentEditorDraft);
        ImGui::EndChild();
    }
    if (!g_ComponentEditorError.empty())
        ImGui::TextWrapped("%s", g_ComponentEditorError.c_str());
    if (RenderActionButton("Save##component_editor")) {
        if (ComponentUIEditorCanCommit()) {
            ComponentUICommand cmd;
            cmd.kind = g_ComponentEditorKind;
            cmd.component_id = g_ComponentEditorId;
            cmd.name = g_ComponentEditorName;
            if (g_ComponentEditorMode == "create" ||
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

static void ComponentUIOpenLibrary(const std::string& kind, const std::string& id) {
    for (int i = 0; i < 9; ++i)
        if (kind == kComponentKinds[i].kind) {
            g_ComponentFilterKind = i;
            break;
        }
    g_ComponentSelectedKind = kind;
    g_ComponentSelectedId = id;
    g_LibraryCollection = 2;
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
            ImGui::SetNextItemWidth(ImMax(110.f, ImGui::GetContentRegionAvail().x - 265.f));
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
            if (active && RenderActionButton("Copy & Edit##part")) {
                g_ComponentCommand = {ComponentUICommand::CopyForLoadout,
                    loadoutId, kind, id, shown + " (Private)", ComponentJson::object()};
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Create an independent copy, then edit without affecting other Loadouts.");
            ImGui::SameLine();
            if (active && RenderActionButton("Edit Shared##part")) {
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
            ImGui::SameLine();
            if (active && RenderActionButton("Open##part"))
                ComponentUIOpenLibrary(kind, id);
            if (active && ImGui::IsItemHovered())
                ImGui::SetTooltip("Open this component in the Components collection.");
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

    const float listWidth = ImMax(200.0f, ImMin(290.0f, ImGui::GetContentRegionAvail().x * 0.37f));
    ImGui::BeginChild("##component_list", ImVec2(listWidth, 0), true);
    const auto group = doc["components"].find(kind);
    if (group != doc["components"].end() && group->is_array()) {
        for (const auto& item : *group) {
            const std::string id = item.value("id", std::string());
            const std::string name = item.value("name", id);
            if (!ComponentUISearchMatch(name, g_ComponentSearch) &&
                !ComponentUISearchMatch(id, g_ComponentSearch) &&
                !ComponentUISearchMatch(ComponentUIShortDescription(kind, item.at("data")),
                                         g_ComponentSearch)) continue;
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
        RenderComponentInventory(kind, item->at("data"));
        if (ImGui::CollapsingHeader("Advanced: raw component data##preview")) {
            ImGui::BeginChild("##component_json_preview", ImVec2(0, 180), true);
            ImGui::TextUnformatted(item->at("data").dump(2).c_str());
            ImGui::EndChild();
        }
        ImGui::Spacing();
        ImGui::TextDisabled("Used by (select to open Loadout):");
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
                            g_LibraryCollection = 0;
                            g_LibDetailsFetched = false;
                            break;
                        }
                }
                ImGui::PopID();
            }
        }
        if (!anyUsage) ImGui::TextDisabled("No Loadouts currently reference this component.");
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
        if (success) {
            // The copy is selected by its new reference after a successful commit.
            const auto& latest = ComponentUISnapshot();
            const auto* loadout = ComponentUIFindLoadout(latest, command.loadout_id);
            if (loadout && loadout->contains("refs")) {
                const auto id = loadout->at("refs").value(command.kind, std::string());
                const auto* copy = ComponentUIFind(latest, command.kind, id);
                if (copy) ComponentUIStartEditor("edit", command.kind, id, copy);
            }
        }
        break;
    case ComponentUICommand::SetFashion:
        success = API::SetLoadoutFashion(command.loadout_id, command.component_id);
        break;
    case ComponentUICommand::Create:
        success = API::CreateLoadoutComponent(command.kind, command.name, command.data);
        if (success) {
            const auto& fresh = ComponentUISnapshot();
            const auto group = fresh["components"].find(command.kind);
            if (group != fresh["components"].end() && group->is_array()) {
                for (auto it = group->rbegin(); it != group->rend(); ++it) {
                    if (!it->is_object() || it->value("name", std::string()) != command.name ||
                        !it->contains("data") || it->at("data") != command.data) continue;
                    // An imported/created component is now navigable directly.
                    ComponentUIOpenLibrary(command.kind,
                        it->value("id", std::string()));
                    break;
                }
            }
        }
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
        success = API::EditAndRenameLoadoutComponent(
            command.kind, command.component_id, command.name, command.data);
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
