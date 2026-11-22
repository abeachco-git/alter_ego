#include "FontManager.h"
#include "GW2API.h"
#include "imgui.h"
#include <windows.h>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <filesystem>

// Inter Regular is an OFL-licensed Windows resource embedded in the DLL.
// Its copyright/license text is embedded as resource 202 and in assets/OFL.txt.
// The user's existing on-disk font is supported as a compatibility fallback.

namespace AlterEgo { namespace FontManager {

static AddonAPI_t* s_api = nullptr;
static std::string s_fontsDir;
static std::mutex s_mtx;
static std::unordered_map<std::string, ImFont*> s_fonts;
static std::unordered_set<std::string> s_requested;

static void ReceiveFont(const char* id, void* font) {
    std::lock_guard<std::mutex> lock(s_mtx);
    s_fonts[id] = static_cast<ImFont*>(font);
}

static HMODULE OwnModule() {
    HMODULE module = nullptr;
    GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (LPCSTR)&OwnModule, &module);
    return module;
}

void Initialize(AddonAPI_t* api) {
    s_api = api;
    s_fontsDir = AlterEgo::GW2API::GetDataDirectory() + "/fonts";
}

static ImFont* RequestTtf(const Font::Config& cfg) {
    if (!s_api) return nullptr;
    const std::string id = Font::Identifier(cfg);
    if (id.empty()) return nullptr;

    std::string path;
    bool useResource = cfg.face == Font::Face::Bundled && s_api->Fonts_AddFromResource;
    HMODULE module = useResource ? OwnModule() : nullptr;
    if (useResource && !module) useResource = false;

    if (!useResource) {
        path = cfg.face == Font::Face::Bundled
            ? s_fontsDir + "/inter_regular.ttf" : cfg.customPath;
        if (path.empty() || !std::filesystem::is_regular_file(path) ||
            !s_api->Fonts_AddFromFile) return nullptr;
    }

    std::unique_lock<std::mutex> lock(s_mtx);
    auto it = s_fonts.find(id);
    if (it != s_fonts.end() && it->second) return it->second;
    if (s_requested.count(id)) return nullptr;
    s_requested.insert(id);
    s_fonts[id] = nullptr;
    lock.unlock();

    if (useResource) {
        // Resource 201 is assets/inter_regular.ttf; Nexus owns its font atlas.
        s_api->Fonts_AddFromResource(id.c_str(), cfg.px, 201, module,
                                      &ReceiveFont, nullptr);
    } else {
        s_api->Fonts_AddFromFile(id.c_str(), cfg.px, path.c_str(),
                                 &ReceiveFont, nullptr);
    }
    return nullptr;
}

ImFont* ResolveDefault(const Font::Config& cfg) {
    return Font::IsDefault(cfg) ? nullptr : RequestTtf(cfg);
}

void Shutdown() {
    std::lock_guard<std::mutex> lock(s_mtx);
    if (s_api && s_api->Fonts_Release)
        for (auto& entry : s_fonts)
            s_api->Fonts_Release(entry.first.c_str(), &ReceiveFont);
    s_fonts.clear();
    s_requested.clear();
    s_api = nullptr;
}

}} // namespace AlterEgo::FontManager
