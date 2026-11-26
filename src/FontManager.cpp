#include "CallbackLifetime.h"
#include "FontManager.h"
#include "GW2API.h"
#include "imgui.h"
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <filesystem>

// Optional user-supplied local fonts are registered with Nexus on demand.

namespace AlterEgo { namespace FontManager {

static AddonAPI_t* s_api = nullptr;
static std::mutex s_mtx;
static std::unordered_map<std::string, ImFont*> s_fonts;
static std::unordered_set<std::string> s_requested;

static void ReceiveFont(const char* id, void* font) {
    AlterEgo::Callbacks::Gate::Lease callbackLease(AlterEgo::Callbacks::Runtime());
    if (!callbackLease) return;
    std::lock_guard<std::mutex> lock(s_mtx);
    if (s_api && id && s_requested.count(id))s_fonts[id]=static_cast<ImFont*>(font);
}

void Initialize(AddonAPI_t* api) {
    std::lock_guard<std::mutex> lock(s_mtx);s_api=api;
}

static ImFont* RequestTtf(const Font::Config& cfg) {
    const std::string id = Font::Identifier(cfg);
    if (id.empty()) return nullptr;

    const auto& path=cfg.customPath;
    if (cfg.face!=Font::Face::Custom || path.empty() ||
        !std::filesystem::is_regular_file(path))
        return nullptr;

    std::unique_lock<std::mutex> lock(s_mtx);
    auto* api=s_api;
    if(!api||!api->Fonts_AddFromFile)return nullptr;
    auto it = s_fonts.find(id);
    if (it != s_fonts.end() && it->second) return it->second;
    if (s_requested.count(id)) return nullptr;
    s_requested.insert(id);
    s_fonts[id] = nullptr;
    lock.unlock();

    api->Fonts_AddFromFile(id.c_str(), cfg.px, path.c_str(),
                            &ReceiveFont, nullptr);
    return nullptr;
}

ImFont* ResolveDefault(const Font::Config& cfg) {
    return Font::IsDefault(cfg) ? nullptr : RequestTtf(cfg);
}

void Shutdown() {
    decltype(s_fonts) releasing;
    AddonAPI_t* api;
    {
        std::lock_guard<std::mutex> lock(s_mtx);
        api = s_api;
        s_api = nullptr;
        releasing.swap(s_fonts);
        s_requested.clear();
    }
    if (api && api->Fonts_Release)
        for (const auto& entry : releasing)
            api->Fonts_Release(entry.first.c_str(), &ReceiveFont);
}

}} // namespace AlterEgo::FontManager
