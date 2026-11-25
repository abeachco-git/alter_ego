#include "BackgroundWorkers.h"
#include "SkinCache.h"
#include "SkinSearchCore.h"
#include "SkinDataCache.h"
#include "PublicCatalog.h"
#include "HttpClient.h"

#include <nlohmann/json.hpp>
#include <thread>
#include <fstream>
#include <algorithm>
#include <filesystem>
#include <sstream>
#include <windows.h>

using json = nlohmann::json;

namespace Skinventory {

    std::string SkinCache::s_data_dir;
    std::atomic<CacheStatus> SkinCache::s_status{CacheStatus::Empty};
    std::string SkinCache::s_status_message;
    std::mutex SkinCache::s_mutex;
    std::atomic<uint64_t> SkinCache::s_generation{0};
    std::unordered_map<uint32_t, SkinInfo> SkinCache::s_skins;
    std::unordered_map<SkinCache::CategoryKey, std::vector<uint32_t>, SkinCache::CategoryKeyHash> SkinCache::s_category_index;

    void SkinCache::SetDataDirectory(const std::string& dir) {
        s_data_dir = dir;
    }

    bool SkinCache::EnsureDataDirectory() {
        if (s_data_dir.empty()) return false;
        try {
            std::filesystem::create_directories(s_data_dir);
            return true;
        } catch (...) {
            return false;
        }
    }

    CacheStatus SkinCache::GetStatus() {
        return s_status.load();
    }

    std::string SkinCache::GetStatusMessage() {
        std::lock_guard<std::mutex> lock(s_mutex);
        return s_status_message;
    }

    std::optional<SkinInfo> SkinCache::GetSkin(uint32_t id) {
        std::lock_guard<std::mutex> lock(s_mutex);
        auto it = s_skins.find(id);
        if (it != s_skins.end()) return it->second;
        return std::nullopt;
    }

    std::vector<uint32_t> SkinCache::GetSkinsByCategory(const std::string& type,
                                                         const std::string& subtype,
                                                         const std::string& weight_class) {
        std::lock_guard<std::mutex> lock(s_mutex);
        CategoryKey key{type, SkinCategorySubtype(type, subtype), weight_class};
        auto it = s_category_index.find(key);
        if (it != s_category_index.end()) return it->second;
        return {};
    }

    std::vector<std::string> SkinCache::GetArmorWeights() {
        return {"Heavy", "Medium", "Light", "Clothing"};
    }

    std::vector<std::string> SkinCache::GetArmorSlots(const std::string& weight_class) {
        (void)weight_class;
        return {"Helm", "Shoulders", "Coat", "Gloves", "Leggings", "Boots"};
    }

    std::vector<std::string> SkinCache::GetWeaponTypes() {
        return {
            "Axe", "Dagger", "Mace", "Pistol", "Scepter", "Sword",
            "Focus", "Shield", "Torch", "Warhorn",
            "Greatsword", "Hammer", "Longbow", "Rifle", "Short Bow", "Staff",
            "Harpoon Gun", "Spear", "Trident"
        };
    }

    size_t SkinCache::GetTotalSkinCount() {
        std::lock_guard<std::mutex> lock(s_mutex);
        return s_skins.size();
    }

    size_t SkinCache::GetCategorySkinCount(const std::string& type,
                                           const std::string& subtype,
                                           const std::string& weight_class) {
        std::lock_guard<std::mutex> lock(s_mutex);
        CategoryKey key{type, SkinCategorySubtype(type, subtype), weight_class};
        auto it = s_category_index.find(key);
        if (it != s_category_index.end()) return it->second.size();
        return 0;
    }

    uint64_t SkinCache::GetGeneration() { return s_generation.load(); }
    std::vector<SkinInfo> SkinCache::GetCatalogSnapshot() {
        std::lock_guard<std::mutex> lock(s_mutex);
        std::vector<SkinInfo> out;out.reserve(s_skins.size());
        for(const auto& kv:s_skins)out.push_back(kv.second);
        return out;
    }
    void SkinCache::RebuildIndex() {
        ++s_generation;
        // Called with lock held
        s_category_index.clear();
        for (const auto& [id, skin] : s_skins) {
            if (skin.type == "Armor") {
                CategoryKey key{skin.type, skin.subtype, skin.weight_class};
                s_category_index[key].push_back(id);
            } else if (skin.type == "Weapon") {
                CategoryKey key{skin.type, SkinCategorySubtype(skin.type, skin.subtype), ""};
                s_category_index[key].push_back(id);
            } else if (skin.type == "Back") {
                CategoryKey key{"Back", "", ""};
                s_category_index[key].push_back(id);
            }
        }
        // Sort each category by name
        for (auto& [key, ids] : s_category_index) {
            std::sort(ids.begin(), ids.end(), [&](uint32_t a, uint32_t b) {
                auto itA = s_skins.find(a);
                auto itB = s_skins.find(b);
                if (itA == s_skins.end() || itB == s_skins.end()) return a < b;
                return itA->second.name < itB->second.name;
            });
        }
    }

    std::vector<uint32_t> SkinCache::GetAllSkinIds() {
        std::lock_guard<std::mutex> lock(s_mutex);
        std::vector<uint32_t> ids;
        ids.reserve(s_skins.size());
        for (const auto& [id, skin] : s_skins) ids.push_back(id);
        return ids;
    }

    bool SkinCache::SaveToDisk() {
        if (!EnsureDataDirectory()) return false;
        json j = json::array();
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            for (const auto& [id, skin] : s_skins) {
                json entry;
                entry["id"] = skin.id;
                entry["name"] = skin.name;
                entry["type"] = skin.type;
                entry["subtype"] = skin.subtype;
                entry["weight_class"] = skin.weight_class;
                entry["rarity"] = skin.rarity;
                entry["icon"] = skin.icon_url;
                entry["flags"] = skin.flags;
                entry["restrictions"] = skin.restrictions;
                j.push_back(std::move(entry));
            }
        }

        return AlterEgo::Cache::WriteSkins(
            AlterEgo::Cache::RootFromLegacySkinDirectory(s_data_dir),
            AlterEgo::Cache::SkinProjection::Catalog,j);
    }

    bool SkinCache::LoadFromDisk() {
        try {
            auto snapshot=AlterEgo::Cache::ReadSkins(
                AlterEgo::Cache::RootFromLegacySkinDirectory(s_data_dir),
                AlterEgo::Cache::SkinProjection::Catalog,
                std::filesystem::u8path(s_data_dir)/"skin_cache.json");
            if(!snapshot)return false;
            auto& j=*snapshot;
            if (!j.is_array() || j.empty()) return false;

            std::unordered_map<uint32_t, SkinInfo> parsed;
            for (const auto& entry : j) {
                SkinInfo skin;
                skin.id = entry.value("id", (uint32_t)0);
                skin.name = entry.value("name", "");
                skin.type = entry.value("type", "");
                skin.subtype = entry.value("subtype", "");
                skin.weight_class = entry.value("weight_class", "");
                skin.rarity = entry.value("rarity", "");
                skin.icon_url = entry.value("icon", "");
                if (entry.contains("flags") && entry["flags"].is_array())
                    skin.flags = entry["flags"].get<std::vector<std::string>>();
                if (entry.contains("restrictions") && entry["restrictions"].is_array())
                    skin.restrictions = entry["restrictions"].get<std::vector<std::string>>();
                if (skin.id > 0) {
                    parsed[skin.id] = std::move(skin);
                }
            }
            // Publish only a fully parsed catalog. A legacy or damaged file
            // must not replace an existing working index with partial data.
            std::lock_guard<std::mutex> lock(s_mutex);
            s_skins = std::move(parsed);
            RebuildIndex();
            s_status = CacheStatus::Ready;
            s_status_message = "Loaded " + std::to_string(s_skins.size()) + " skins from cache";
            return true;
        } catch (...) {
            return false;
        }
    }

    void SkinCache::FetchAllSkinsAsync() {
        using namespace AlterEgo;
        if(!PublicCatalog::BeginDataset("public_skins",false,true))return;
        s_status=CacheStatus::Loading;
        {std::lock_guard<std::mutex> lock(s_mutex);s_status_message="Starting skin fetch...";}
        Background::Workers().Launch([] { FetchWorker(); }, [] {
            PublicCatalog::EndDataset("public_skins",false);
            std::lock_guard<std::mutex> lock(s_mutex);s_status_message="Skin fetch interrupted";s_status=CacheStatus::Error;
        });
    }
    void SkinCache::UpdateCacheAsync(bool manual) {
        using namespace AlterEgo;
        const auto expected=s_status.load();
        if(expected==CacheStatus::Loading||expected==CacheStatus::Updating)return;
        if(!PublicCatalog::BeginDataset("public_skins",manual))return;
        const bool empty=expected==CacheStatus::Empty||expected==CacheStatus::Error;
        s_status=empty?CacheStatus::Loading:CacheStatus::Updating;
        {std::lock_guard<std::mutex> lock(s_mutex);s_status_message="Checking for new skins...";}
        Background::Workers().Launch([empty] {if(empty)FetchWorker();else UpdateWorker();}, [empty] {
            PublicCatalog::EndDataset("public_skins",false);
            std::lock_guard<std::mutex> lock(s_mutex);s_status_message="Skin update interrupted";s_status=empty?CacheStatus::Error:CacheStatus::Ready;
        });
    }

    // Helper: fetch skin IDs from the API. Returns empty vector on failure.
    static std::vector<uint32_t> FetchSkinIdList() {
        std::string response = HttpClient::Get("https://api.guildwars2.com/v2/skins");
        if (response.empty()) return {};
        try {
            json j = json::parse(response);
            const auto ids=AlterEgo::PublicCatalog::Index(j);
            return {ids.begin(),ids.end()};
        } catch (...) { return {}; }
    }

    // Helper: fetch skin details for a batch of IDs, append to 'out'.
    static void FetchSkinBatch(const std::vector<uint32_t>& ids, size_t start, size_t end,
                                std::unordered_map<uint32_t, SkinInfo>& out) {
        std::string ids_param;
        for (size_t j = start; j < end; j++) {
            if (!ids_param.empty()) ids_param += ",";
            ids_param += std::to_string(ids[j]);
        }
        std::string url = "https://api.guildwars2.com/v2/skins?ids=" + ids_param;
        std::string response = HttpClient::Get(url);
        if(response.empty())throw std::runtime_error("Missing skin batch");
        {
            json batch=json::parse(response);
            AlterEgo::PublicCatalog::ValidateBatch(batch,std::set<uint32_t>(ids.begin()+start,ids.begin()+end));
            for (const auto& entry : batch) {
                SkinInfo skin;
                skin.id = entry.value("id", (uint32_t)0);
                skin.name = entry.value("name", "");
                skin.type = entry.value("type", "");
                skin.rarity = entry.value("rarity", "");
                skin.icon_url = entry.value("icon", "");
                if (entry.contains("details") && entry["details"].is_object()) {
                    const auto& details = entry["details"];
                    skin.subtype = details.value("type", "");
                    skin.weight_class = details.value("weight_class", "");
                }
                if (entry.contains("flags") && entry["flags"].is_array()) {
                    for (const auto& f : entry["flags"]) skin.flags.push_back(f.get<std::string>());
                }
                if (entry.contains("restrictions") && entry["restrictions"].is_array()) {
                    for (const auto& r : entry["restrictions"]) skin.restrictions.push_back(r.get<std::string>());
                }
                if (skin.id > 0) out[skin.id] = std::move(skin);
            }
        }
    }

    void SkinCache::FetchWorker() {
        using namespace AlterEgo;
        const auto root=Cache::RootFromLegacySkinDirectory(s_data_dir);
        try {
            PublicCatalog::Attempt(root,"public_skins");
            // Step 1: Get all skin IDs
            {
                std::lock_guard<std::mutex> lock(s_mutex);
                s_status_message = "Fetching skin ID list...";
            }
            auto ids = FetchSkinIdList();
            if(ids.empty())throw std::runtime_error("Missing skin index");

            // Step 2: Fetch skin details in batches of 200
            const size_t BATCH_SIZE = 200;
            size_t total = ids.size();
            std::unordered_map<uint32_t, SkinInfo> fetched;

            for (size_t i = 0; i < total; i += BATCH_SIZE) {
            AlterEgo::Background::Checkpoint();
                size_t end = std::min(i + BATCH_SIZE, total);
                {
                    std::lock_guard<std::mutex> lock(s_mutex);
                    s_status_message = "Fetching skins " + std::to_string(i) + "/" + std::to_string(total) + "...";
                }
                FetchSkinBatch(ids, i, end, fetched);
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }

        AlterEgo::Background::Checkpoint();
            if(fetched.size()!=ids.size())throw std::runtime_error("Incomplete skin catalog");

            // Step 3: Store and index
            {
                std::lock_guard<std::mutex> lock(s_mutex);
                s_skins = std::move(fetched);
                RebuildIndex();
                s_status_message = "Loaded " + std::to_string(s_skins.size()) + " skins";
            }
            if(!SaveToDisk())throw std::runtime_error("Skin persistence failed");
            PublicCatalog::CompleteCheck(root,"public_skins");
            PublicCatalog::EndDataset("public_skins",true);
            s_status=CacheStatus::Ready;

        } catch (...) {
            PublicCatalog::EndDataset("public_skins",false);
            std::lock_guard<std::mutex> lock(s_mutex);
            s_status = CacheStatus::Error;
            s_status_message = "Unexpected error during skin fetch";
        }
    }

    void SkinCache::UpdateWorker() {
        using namespace AlterEgo;
        const auto root=Cache::RootFromLegacySkinDirectory(s_data_dir);
        try {
            PublicCatalog::Attempt(root,"public_skins");
            // Step 1: Get current API skin IDs
            auto api_ids = FetchSkinIdList();
            if(api_ids.empty())throw std::runtime_error("Missing skin index");

            // Step 2: Find IDs not in our cache
            std::vector<uint32_t> new_ids;
            {
                std::lock_guard<std::mutex> lock(s_mutex);
                for (uint32_t id : api_ids) {
                    if (s_skins.find(id) == s_skins.end()) {
                        new_ids.push_back(id);
                    }
                }
            }

            if (new_ids.empty()) {
                PublicCatalog::CompleteCheck(root,"public_skins");
                PublicCatalog::EndDataset("public_skins",true);
                std::lock_guard<std::mutex> lock(s_mutex);
                s_status = CacheStatus::Ready;
                s_status_message = "Cache up to date (" + std::to_string(s_skins.size()) + " skins)";
                return;
            }

            // Step 3: Fetch only the new skins
            {
                std::lock_guard<std::mutex> lock(s_mutex);
                s_status_message = "Fetching " + std::to_string(new_ids.size()) + " new skins...";
            }

            const size_t BATCH_SIZE = 200;
            std::unordered_map<uint32_t, SkinInfo> fetched;
            for (size_t i = 0; i < new_ids.size(); i += BATCH_SIZE) {
            AlterEgo::Background::Checkpoint();
                size_t end = std::min(i + BATCH_SIZE, new_ids.size());
                FetchSkinBatch(new_ids, i, end, fetched);
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }

        AlterEgo::Background::Checkpoint();
            if(fetched.size()!=new_ids.size())throw std::runtime_error("Incomplete skin update");
            // Step 4: Merge into existing cache
            if (!fetched.empty()) {
                std::lock_guard<std::mutex> lock(s_mutex);
                for (auto& [id, skin] : fetched) {
                    s_skins[id] = std::move(skin);
                }
                RebuildIndex();
                s_status_message = "Updated: " + std::to_string(s_skins.size()) + " skins (" +
                                   std::to_string(fetched.size()) + " new)";
            } else {
                std::lock_guard<std::mutex> lock(s_mutex);
                s_status_message = "Cache up to date (" + std::to_string(s_skins.size()) + " skins)";
            }

            if(!SaveToDisk())throw std::runtime_error("Skin persistence failed");
            PublicCatalog::CompleteCheck(root,"public_skins");
            PublicCatalog::EndDataset("public_skins",true);
            s_status=CacheStatus::Ready;

        } catch (...) {
            PublicCatalog::EndDataset("public_skins",false);
            // On error during update, stay Ready with existing data
            std::lock_guard<std::mutex> lock(s_mutex);
            s_status = CacheStatus::Ready;
            s_status_message = "Update error, using cached data";
        }
    }

}
