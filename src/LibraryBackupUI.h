#pragma once
// Unified Library > Backup / Restore. Keep collection navigation separate from
// import/export controls; the same dialog is available from Builds and Fashion.
#include "LibraryBackup.h"
#include <commdlg.h>
#include <cstdio>
#include <ctime>
#include <string>

struct LibraryBackupUIState {
    bool openExport = false;
    bool openRestore = false;
    bool exportBuilds = true;
    bool exportFashion = true;
    bool exportOther = true;
    bool exportZip = false;
    bool importBuilds = false;
    bool importFashion = false;
    bool importOther = false;
    bool replace = false;
    bool replaceAcknowledged = false;
    bool previewDirty = true;
    std::string importedPath;
    std::string error;
    std::string lastRecovery;
    AlterEgo::Backup::Package package;
    AlterEgo::Backup::Plan preview;
};
static LibraryBackupUIState g_LibraryBackupUI;

static unsigned LibraryBackupExportMask() {
    unsigned mask = 0;
    if (g_LibraryBackupUI.exportBuilds) mask |= AlterEgo::Backup::Builds;
    if (g_LibraryBackupUI.exportFashion) mask |= AlterEgo::Backup::Fashion;
    if (g_LibraryBackupUI.exportOther) mask |= AlterEgo::Backup::Other;
    return mask;
}
static unsigned LibraryBackupImportMask() {
    unsigned mask = 0;
    if (g_LibraryBackupUI.importBuilds) mask |= AlterEgo::Backup::Builds;
    if (g_LibraryBackupUI.importFashion) mask |= AlterEgo::Backup::Fashion;
    if (g_LibraryBackupUI.importOther) mask |= AlterEgo::Backup::Other;
    return mask;
}
static void LibraryBackupNotify(const std::string& message) {
    if (APIDefs) APIDefs->GUI_SendAlert(message.c_str());
}

static bool LibraryBackupFilePicker(bool save, bool zip, char (&path)[MAX_PATH]) {
    OPENFILENAMEA dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFile = path;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrTitle = save ? "Export Alter Ego Data" : "Import Alter Ego Data";
    if (save) {
        dialog.lpstrFilter = zip ?
            "ZIP archive\0*.zip\0All files\0*.*\0" :
            "JSON file\0*.json\0All files\0*.*\0";
        dialog.lpstrDefExt = zip ? "zip" : "json";
        dialog.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST;
        return GetSaveFileNameA(&dialog) == TRUE;
    }
    dialog.lpstrFilter = "Alter Ego backups\0*.zip;*.json\0ZIP archive\0*.zip\0JSON files\0*.json\0All files\0*.*\0";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameA(&dialog) == TRUE;
}

static void LibraryBackupRestoreCurrentState(unsigned selected) {
    // Updating the in-memory state is essential: otherwise normal addon unload
    // can overwrite restored settings, pinned achievements, or login dates.
    if (selected & (AlterEgo::Backup::Builds|AlterEgo::Backup::Fashion)) {
        AlterEgo::GW2API::LoadBuildLibrary();
        g_LibSelectedIdx = -1;
        g_EditDraftId.clear();
        g_LibDetailsFetched = false;
    }
    if (selected & (AlterEgo::Backup::Builds|AlterEgo::Backup::Fashion)) {
        g_FashionLibrary.Load(AlterEgo::GW2API::GetDataDirectory());
        g_FashionSelected = -1;
        g_FashionDialog = false;
    }
    if (selected & AlterEgo::Backup::Other) {
        LoadSettings();
        AlterEgo::AutoRefreshPolicy::SetEnabled(g_AutoRefreshEnabled);
        LoadAchTrackerState();
        g_LoginTimestamps.clear();
        LoadLoginTimestamps();
        g_LoginTimestampsDirty = false;
        LoadCharSortConfig();
        RebuildCharDisplayOrder();
        LoadSession();
        ReloadClearUiSettings();
        s_portraitPathCache.clear();
        s_portraitMissing.clear();
        // Custom font files will be fully reloaded when the addon restarts.
    }
}

static bool LibraryBackupApplyImport() {
    auto& ui = g_LibraryBackupUI;
    const unsigned selected = LibraryBackupImportMask();
    const std::string dir = AlterEgo::GW2API::GetDataDirectory();
    ui.error.clear();
    // Do NOT write any current file before Backup::Apply has created a
    // recovery snapshot. Saved builds/fashion are already persisted on edit.
    AlterEgo::Backup::Plan plan;
    if (!AlterEgo::Backup::Prepare(dir, ui.package, selected, ui.replace, plan, ui.error))
        return false;
    std::string recovery;
    if (!AlterEgo::Backup::Apply(dir, plan, recovery, ui.error)) return false;
    ui.lastRecovery = recovery;
    LibraryBackupRestoreCurrentState(selected);
    char summary[280] = {};
    snprintf(summary, sizeof(summary),
        "%s: %zu builds, %zu fashion, %zu other files (%zu skipped).",
        ui.replace ? "Restore complete" : "Merge complete",
        plan.buildAdded, plan.fashionAdded, plan.otherWritten,
        plan.buildSkipped + plan.fashionSkipped + plan.otherSkipped);
    LibraryBackupNotify(summary);
    if (selected & AlterEgo::Backup::Other)
        LibraryBackupNotify("Preferences restored. Restart Alter Ego to reload custom fonts.");
    return true;
}

// Global Library menu actions: the dialogs are rendered each frame from the
// main window, not nested inside a particular collection page.
static void BeginLibraryBackupExport() {
    g_LibraryBackupUI.error.clear();
    g_LibraryBackupUI.openExport = true;
}

static void BeginLibraryBackupImport() {
    auto& ui = g_LibraryBackupUI;
    char path[MAX_PATH] = {};
    if (!LibraryBackupFilePicker(false, false, path)) return;
    ui.error.clear();
    if (!AlterEgo::Backup::ReadPackage(path, ui.package, ui.error)) {
        LibraryBackupNotify("Import failed: " + ui.error);
        return;
    }
    ui.importedPath = path;
    ui.lastRecovery.clear();
    const unsigned available = ui.package.available;
    ui.importBuilds = (available & AlterEgo::Backup::Builds) != 0;
    ui.importFashion = (available & AlterEgo::Backup::Fashion) != 0;
    ui.importOther = (available & AlterEgo::Backup::Other) != 0;
    ui.replace = false;
    ui.replaceAcknowledged = false;
    ui.previewDirty = true;
    ui.openRestore = true;
}

static void RenderLibraryBackupUI() {
    auto& ui = g_LibraryBackupUI;
    if (ui.openExport) {
        ImGui::OpenPopup("Export Alter Ego Data##ae_backup");
        ui.openExport = false;
    }
    if (ui.openRestore) {
        ImGui::OpenPopup("Import Alter Ego Data##ae_backup");
        ui.openRestore = false;
    }

    ImGui::SetNextWindowSize(ImVec2(470, 0), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Export Alter Ego Data##ae_backup", nullptr,
        ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Include in export");
        ImGui::Separator();
        ImGui::Checkbox("Loadout Library (loadouts, components and notes)", &ui.exportBuilds);
        ImGui::Checkbox("Fashion Library (skins, dyes and characters)", &ui.exportFashion);
        ImGui::Checkbox("Other data (preferences, pins and custom assets)", &ui.exportOther);
        const unsigned mask = LibraryBackupExportMask();
        const bool single = (mask == AlterEgo::Backup::Builds ||
                             mask == AlterEgo::Backup::Fashion);
        ImGui::Spacing();
        ImGui::TextUnformatted("Export format");
        if (single) {
            if (ImGui::RadioButton("Portable JSON", !ui.exportZip)) ui.exportZip = false;
            ImGui::SameLine();
            if (ImGui::RadioButton("ZIP archive", ui.exportZip)) ui.exportZip = true;
        } else {
            ImGui::TextDisabled("ZIP archive (multiple files)");
        }
        ImGui::TextDisabled("GW2 API caches are excluded; custom portraits and fonts are included.");
        ImGui::Spacing();
        if (mask && RenderActionButton("Export##ae_backup_export")) {
            const bool zip = !single || ui.exportZip;
            char filename[MAX_PATH] = {};
            if (!zip) {
                std::snprintf(filename, sizeof(filename), "%s",
                    "alter_ego_library.json");
            } else {
                const std::time_t now = std::time(nullptr);
                std::tm local = *std::localtime(&now);
                std::snprintf(filename, sizeof(filename),
                    "AlterEgo_Backup_%04d%02d%02d.zip",
                    local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
            }
            if (LibraryBackupFilePicker(true, zip, filename)) {
                // Only local preferences need flushing; saved builds and
                // fashion were persisted on every edit. Never overwrite a
                // possibly damaged library file merely to export it.
                if (mask & AlterEgo::Backup::Other) {
                    SaveSettings();
                    SaveSession();
                    SaveAchTrackerState();
                    SaveCharSortConfig();
                    SaveLoginTimestamps();
                }
                ui.error.clear();
                if (AlterEgo::Backup::Export(
                        AlterEgo::GW2API::GetDataDirectory(), mask, filename, zip, ui.error)) {
                    LibraryBackupNotify(zip ? "Alter Ego ZIP backup exported." :
                                               "Library JSON exported.");
                    ImGui::CloseCurrentPopup();
                }
            }
        }
        ImGui::SameLine();
        if (RenderActionButton("Cancel##ae_backup_export")) ImGui::CloseCurrentPopup();
        if (!ui.error.empty()) {
            ImGui::Spacing();
            ImGui::TextWrapped("Export failed: %s", ui.error.c_str());
        }
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowSize(ImVec2(510, 0), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Import Alter Ego Data##ae_backup", nullptr,
        ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Import Library");
        ImGui::TextWrapped("File: %s", ui.importedPath.c_str());
        ImGui::Separator();
        ImGui::TextUnformatted("Select collections to restore");
        if (ui.package.available & AlterEgo::Backup::Builds) {
            if (ImGui::Checkbox("Loadout Library##backup_restore", &ui.importBuilds)) {
                ui.previewDirty = true; ui.replaceAcknowledged = false;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(%zu builds)", ui.package.buildCount);
        }
        if (ui.package.available & AlterEgo::Backup::Fashion) {
            if (ImGui::Checkbox("Fashion Library##backup_restore", &ui.importFashion)) {
                ui.previewDirty = true; ui.replaceAcknowledged = false;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(%zu looks)", ui.package.fashionCount);
        }
        if (ui.package.available & AlterEgo::Backup::Other) {
            if (ImGui::Checkbox("Other data / preferences##backup_restore", &ui.importOther)) {
                ui.previewDirty = true; ui.replaceAcknowledged = false;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(settings, pins, custom assets)");
        }
        ImGui::Spacing();
        ImGui::TextUnformatted("Import behavior");
        if (ImGui::RadioButton("Merge - keep existing entries", !ui.replace)) {
            ui.replace = false; ui.previewDirty = true; ui.replaceAcknowledged = false;
        }
        if (ImGui::RadioButton("Replace selected collections", ui.replace)) {
            ui.replace = true; ui.previewDirty = true; ui.replaceAcknowledged = false;
        }

        const unsigned selected = LibraryBackupImportMask();
        if (selected && ui.previewDirty) {
            ui.error.clear();
            AlterEgo::Backup::Prepare(AlterEgo::GW2API::GetDataDirectory(),
                ui.package, selected, ui.replace, ui.preview, ui.error);
            ui.previewDirty = false;
        }
        if (selected && ui.error.empty()) {
            ImGui::Separator();
            ImGui::Text("Builds: %zu import, %zu skip", ui.preview.buildAdded, ui.preview.buildSkipped);
            ImGui::Text("Fashion: %zu import, %zu skip", ui.preview.fashionAdded, ui.preview.fashionSkipped);
            ImGui::Text("Other files: %zu import, %zu skip", ui.preview.otherWritten, ui.preview.otherSkipped);
            if (ui.preview.fashionSimilar)
                ImGui::TextDisabled("%zu fashion entries resemble existing looks but have different IDs.",
                    ui.preview.fashionSimilar);
        }
        if (!ui.replace)
            ImGui::TextWrapped("Merge keeps existing entries with matching IDs. For other data, existing local files are kept.");
        else {
            ImGui::TextWrapped("Replace overwrites only selected collections and files present in this backup. Linked Fashion dependencies are preserved or included.");
            ImGui::Checkbox("I understand the selected data will be replaced", &ui.replaceAcknowledged);
        }
        ImGui::TextDisabled("Recovery snapshots are saved in Alter Ego's backups/recovery folder.");
        ImGui::Spacing();
        if ((selected & AlterEgo::Backup::Builds) && g_EditDirty)
            ImGui::TextWrapped("Save or cancel unsaved build edits before restoring Builds.");
        const bool canRestore = selected && ui.error.empty() &&
            (!(selected & AlterEgo::Backup::Builds) || !g_EditDirty) &&
            (!ui.replace || ui.replaceAcknowledged);
        if (!canRestore) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
        const bool restoreClicked = RenderActionButton(ui.replace ?
            "Replace Selected##ae_backup_import" : "Merge##ae_backup_import");
        if (!canRestore) ImGui::PopStyleVar();
        if (canRestore && restoreClicked) {
            if (LibraryBackupApplyImport()) {
                ImGui::CloseCurrentPopup();
            }
            ui.previewDirty = true;
        }
        ImGui::SameLine();
        if (RenderActionButton("Cancel##ae_backup_import")) {
            ImGui::CloseCurrentPopup();
            ui.error.clear();
        }
        if (!ui.error.empty()) {
            ImGui::Spacing();
            ImGui::TextWrapped("Import failed: %s", ui.error.c_str());
        }
        if (!ui.lastRecovery.empty() && !ui.error.empty())
            ImGui::TextWrapped("Recovery backup: %s", ui.lastRecovery.c_str());
        ImGui::EndPopup();
    }
}
