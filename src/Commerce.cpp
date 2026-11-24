#include "BackgroundWorkers.h"
#include "Commerce.h"
#include "HttpClient.h"
#include "SkinCache.h"
#include "CommerceCache.h"
#include "SkinDataCache.h"

#include <nlohmann/json.hpp>
#include <thread>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>
#include <chrono>
#include <unordered_set>
#include <stdexcept>

using json = nlohmann::json;

namespace Skinventory {

    static int64_t NowUnix() {
        return std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }
    static const int64_t PRICE_TTL_SEC = 15 * 60; // 15 minutes

    std::string Commerce::s_data_dir;
    std::mutex Commerce::s_mutex;
    std::unordered_map<uint32_t, PriceInfo> Commerce::s_prices;
    std::unordered_map<uint32_t, std::vector<uint32_t>> Commerce::s_skin_to_item;
    std::unordered_map<uint32_t, std::string> Commerce::s_item_names;
    std::atomic<uint64_t> Commerce::s_price_generation{0};
    std::unordered_map<uint32_t, int> Commerce::s_vendor_prices;
    std::atomic<bool> Commerce::s_fetching{false};
    std::string Commerce::s_fetch_status;
    std::atomic<bool> Commerce::s_item_map_ready{false};
    std::atomic<bool> Commerce::s_item_map_building{false};

    std::string FormatCoins(int copper) {
        if (copper <= 0) return "0c";

        int gold = copper / 10000;
        int silver = (copper % 10000) / 100;
        int cop = copper % 100;

        std::string result;
        if (gold > 0) result += std::to_string(gold) + "g ";
        if (silver > 0 || gold > 0) result += std::to_string(silver) + "s ";
        result += std::to_string(cop) + "c";
        return result;
    }

    void Commerce::SetDataDirectory(const std::string& dir) {
        s_data_dir = dir;
    }

    std::optional<PriceInfo> Commerce::GetPrice(uint32_t skin_id) {
        std::lock_guard<std::mutex> lock(s_mutex);
        auto it = s_prices.find(skin_id);
        if (it != s_prices.end()) return it->second;
        return std::nullopt;
    }

    int Commerce::GetVendorPrice(uint32_t skin_id) {
        std::lock_guard<std::mutex> lock(s_mutex);
        auto it = s_vendor_prices.find(skin_id);
        if (it != s_vendor_prices.end()) return it->second;
        return 0;
    }

    // Cultural armor vendor prices (coin) by tier and slot.
    // Prices are fixed and never change.
    static int GetCulturalArmorPrice(int level, const std::string& slot) {
        // Tier from item level: T1=35, T2=60, T3=80
        int tier = 0;
        if (level <= 35) tier = 1;
        else if (level <= 60) tier = 2;
        else tier = 3;

        // slot from item details.type: Boots, Coat, Gloves, Helm, Leggings, Shoulders
        std::string s = slot;
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);

        if (tier == 1) {
            if (s == "boots")     return 3400;
            if (s == "gloves")    return 3400;
            if (s == "helm")      return 4000;
            if (s == "shoulders") return 4000;
            if (s == "leggings")  return 6000;
            if (s == "coat")      return 8000;
        } else if (tier == 2) {
            if (s == "boots")     return 20000;
            if (s == "gloves")    return 20000;
            if (s == "helm")      return 20000;
            if (s == "shoulders") return 20000;
            if (s == "leggings")  return 35000;
            if (s == "coat")      return 50000;
        } else if (tier == 3) {
            if (s == "boots")     return 100000;
            if (s == "gloves")    return 100000;
            if (s == "helm")      return 100000;
            if (s == "shoulders") return 100000;
            if (s == "leggings")  return 175000;
            if (s == "coat")      return 250000;
        }
        return 0;
    }

    bool Commerce::IsFetching() {
        return s_fetching;
    }

    std::string Commerce::GetFetchStatus() {
        std::lock_guard<std::mutex> lock(s_mutex);
        return s_fetch_status;
    }

    bool Commerce::IsItemMapReady() {
        return s_item_map_ready;
    }

    uint32_t Commerce::GetItemIdForSkin(uint32_t skin_id) {
        const auto price = GetPrice(skin_id);
        return price && price->tradeable ? price->item_id : 0;
    }
    std::string Commerce::GetItemName(uint32_t item_id) {
        std::lock_guard<std::mutex> lock(s_mutex);
        const auto it=s_item_names.find(item_id);
        return it==s_item_names.end()?std::string():it->second;
    }
    uint64_t Commerce::GetPriceGeneration() { return s_price_generation.load(); }

    void Commerce::FetchPriceForSkin(uint32_t skin_id) {
        FetchPricesForSkins({skin_id});
    }

    void Commerce::FetchPricesForSkins(const std::vector<uint32_t>& skin_ids) {
        if (!s_item_map_ready || s_fetching.exchange(true)) return;
        std::vector<uint32_t> item_ids;
        std::unordered_map<uint32_t, std::vector<uint32_t>> item_to_skin;
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            for (uint32_t skin_id : skin_ids) {
                const auto it=s_skin_to_item.find(skin_id);
                const auto pit=s_prices.find(skin_id);
                if(pit!=s_prices.end()) {
                    const auto age=NowUnix()-pit->second.fetched_at_unix;
                    if(age>=0 && age<PRICE_TTL_SEC) continue;
                }
                if(it==s_skin_to_item.end() || it->second.empty()) {
                    PriceInfo info; info.fetched_at_unix=NowUnix();
                    s_prices[skin_id]=info;
                    continue;
                }
                for(uint32_t item:it->second) {
                    auto& skins=item_to_skin[item];
                    if(skins.empty()) item_ids.push_back(item);
                    skins.push_back(skin_id);
                }
            }
        }
        if(item_ids.empty()) { s_fetching=false; return; }
        AlterEgo::Background::Workers().Launch([ids = std::move(item_ids), mapping = std::move(item_to_skin)]() mutable { FetchPriceWorker(std::move(ids), std::move(mapping)); }, [] { s_fetching = false; });
    }

    void Commerce::FetchPriceWorker(std::vector<uint32_t> item_ids,
                     std::unordered_map<uint32_t, std::vector<uint32_t>> item_to_skin) {
        std::unordered_map<uint32_t, PriceInfo> best;
        std::unordered_set<uint32_t> failed;
        for(const auto& kv:item_to_skin) for(auto skin:kv.second) best.try_emplace(skin);
        constexpr size_t BATCH_SIZE=200;
        for(size_t i=0;i<item_ids.size();i+=BATCH_SIZE) {
            AlterEgo::Background::Checkpoint();
            const size_t end=std::min(i+BATCH_SIZE,item_ids.size());
            {
                std::lock_guard<std::mutex> lock(s_mutex);
                s_fetch_status="Fetching prices "+std::to_string(i)+"/"+std::to_string(item_ids.size());
            }
            std::string ids;
            for(size_t j=i;j<end;++j) { if(!ids.empty()) ids+=","; ids+=std::to_string(item_ids[j]); }
            bool ok=false;
            try {
                const auto batch=json::parse(HttpClient::Get("https://api.guildwars2.com/v2/commerce/prices?ids="+ids));
                if(batch.is_array()) {
                    for(const auto& entry:batch) {
                        const auto item=entry.value("id",uint32_t(0));
                        const auto it=item_to_skin.find(item);
                        if(it==item_to_skin.end()) continue;
                        PriceInfo info; info.item_id=item; info.tradeable=true;
                        if(entry.contains("buys") && entry["buys"].is_object()) {
                            info.buy_price=entry["buys"].value("unit_price",0);
                            info.buy_quantity=entry["buys"].value("quantity",0);
                        }
                        if(entry.contains("sells") && entry["sells"].is_object()) {
                            info.sell_price=entry["sells"].value("unit_price",0);
                            info.sell_quantity=entry["sells"].value("quantity",0);
                        }
                        for(auto skin:it->second) if(BetterPrice(info,best[skin])) best[skin]=info;
                    }
                    ok=true;
                }
            } catch(...) {}
            if(!ok) for(size_t j=i;j<end;++j) for(auto skin:item_to_skin[item_ids[j]]) failed.insert(skin);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        AlterEgo::Background::Checkpoint();
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            for(auto& kv:best) {
                // Never present a partially queried candidate set as cheapest.
                if(failed.count(kv.first)) continue;
                kv.second.fetched_at_unix=NowUnix();
                s_prices[kv.first]=kv.second;
            }
            s_fetch_status=failed.empty()?"":"Some prices could not be retrieved; previous prices retained.";
            ++s_price_generation;
        }
        SavePriceCache();
        s_fetching=false;
    }

    bool Commerce::SavePriceCache() {
        if (s_data_dir.empty()) return false;
        try { std::filesystem::create_directories(s_data_dir); } catch (...) { return false; }

        json j = json::object();
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            for (const auto& [skin_id, info] : s_prices) {
                json e;
                e["item_id"] = info.item_id;
                e["buy"] = info.buy_price;
                e["sell"] = info.sell_price;
                e["buy_q"] = info.buy_quantity;
                e["sell_q"] = info.sell_quantity;
                e["tradeable"] = info.tradeable;
                e["ts"] = info.fetched_at_unix;
                j[std::to_string(skin_id)] = e;
            }
        }
        return AlterEgo::Cache::WriteCommerce(AlterEgo::Cache::RootFromLegacySkinDirectory(s_data_dir),AlterEgo::Cache::CommerceDataset::Prices,j);
    }

    bool Commerce::LoadPriceCache() {
        if (s_data_dir.empty()) return false;
        std::string path = s_data_dir + "/price_cache_v2.json";
        try {
            auto snapshot=AlterEgo::Cache::ReadCommerce(AlterEgo::Cache::RootFromLegacySkinDirectory(s_data_dir),
                AlterEgo::Cache::CommerceDataset::Prices,std::filesystem::u8path(path));
            if(!snapshot || !snapshot->is_object())return false;
            auto& j=*snapshot;
            std::lock_guard<std::mutex> lock(s_mutex);
            for (auto& [key, e] : j.items()) {
                if (!e.is_object()) continue;
                int64_t ts = e.value("ts", (int64_t)0);
                // Retain the last successful observation; the existing TTL controls refresh.
                uint32_t skin_id = (uint32_t)std::stoul(key);
                PriceInfo info;
                info.item_id = e.value("item_id", (uint32_t)0);
                info.buy_price = e.value("buy", 0);
                info.sell_price = e.value("sell", 0);
                info.buy_quantity = e.value("buy_q", 0);
                info.sell_quantity = e.value("sell_q", 0);
                info.tradeable = e.value("tradeable", false);
                info.fetched_at_unix = ts;
                s_prices[skin_id] = std::move(info);
            }
        } catch (...) { return false; }
        return true;
    }

    // Versioned derived cache: legacy one-item/rarity maps must be rebuilt.
    bool Commerce::SaveItemMap() {
        if(s_data_dir.empty()) return false;
        try { std::filesystem::create_directories(s_data_dir); } catch(...) { return false; }
        json skins=json::object(), names=json::object(), vendors=json::object();
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            for(const auto& kv:s_skin_to_item) skins[std::to_string(kv.first)]=kv.second;
            for(const auto& kv:s_item_names) names[std::to_string(kv.first)]=kv.second;
            for(const auto& kv:s_vendor_prices) vendors[std::to_string(kv.first)]=kv.second;
        }
        return AlterEgo::Cache::WriteCommerce(AlterEgo::Cache::RootFromLegacySkinDirectory(s_data_dir),
            AlterEgo::Cache::CommerceDataset::Mapping,json{{"version",2},{"skins",skins},{"names",names},{"vendors",vendors}});
    }

    bool Commerce::LoadItemMap() {
        try {
            auto snapshot=AlterEgo::Cache::ReadCommerce(AlterEgo::Cache::RootFromLegacySkinDirectory(s_data_dir),
                AlterEgo::Cache::CommerceDataset::Mapping,std::filesystem::u8path(s_data_dir)/"item_map_v2.json");
            if(!snapshot)return false;
            auto& j=*snapshot;
            if(j.value("version",0)!=2 || !j.at("skins").is_object() || j.at("skins").empty()) return false;
            std::unordered_map<uint32_t,std::vector<uint32_t>> skins;
            std::unordered_map<uint32_t,std::string> names;
            std::unordered_map<uint32_t,int> vendors;
            for(auto& kv:j.at("skins").items()) skins[std::stoul(kv.key())]=kv.value().get<std::vector<uint32_t>>();
            for(auto& kv:j.at("names").items()) names[std::stoul(kv.key())]=kv.value().get<std::string>();
            for(auto& kv:j.at("vendors").items()) vendors[std::stoul(kv.key())]=kv.value().get<int>();
            std::lock_guard<std::mutex> lock(s_mutex);
            s_skin_to_item=std::move(skins); s_item_names=std::move(names); s_vendor_prices=std::move(vendors);
            s_item_map_ready=true;
            return true;
        } catch(...) { return false; }
    }

    void Commerce::BuildItemMapAsync() {
        if (s_item_map_building || s_item_map_ready) return;
        s_item_map_building = true;
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            s_fetch_status = "Building item map...";
        }
        AlterEgo::Background::Workers().Launch([] { ItemMapWorker(); }, [] { s_item_map_building = false; });
    }

    void Commerce::ItemMapWorker() {
        try {
            // Step 1: Get all item IDs from the GW2 API
            {
                std::lock_guard<std::mutex> lock(s_mutex);
                s_fetch_status = "Building item map (one-time, cached after)...";
            }
            std::string response = HttpClient::Get("https://api.guildwars2.com/v2/items");
            if (response.empty()) {
                std::lock_guard<std::mutex> lock(s_mutex);
                s_fetch_status = "Item map failed: could not reach GW2 API";
                s_item_map_building = false;
                return;
            }

            json all_ids;
            try {
                all_ids = json::parse(response);
            } catch (...) {
                std::lock_guard<std::mutex> lock(s_mutex);
                s_fetch_status = "Item map failed: invalid response";
                s_item_map_building = false;
                return;
            }

            if (!all_ids.is_array() || all_ids.empty()) throw std::runtime_error("Invalid item index");

            std::vector<uint32_t> ids;
            ids.reserve(all_ids.size());
            for (const auto& id : all_ids) {
                ids.push_back(id.get<uint32_t>());
            }

            // Step 2: Fetch items in batches, extract default_skin for Armor/Weapon types
            const size_t BATCH_SIZE = 200;
            size_t total = ids.size();
            std::unordered_map<uint32_t, std::vector<uint32_t>> skin_map;
            std::unordered_map<uint32_t, std::string> item_names;
            // The commerce index is authoritative for tradeable candidates, not skin rarity.
            const auto market=json::parse(HttpClient::Get("https://api.guildwars2.com/v2/commerce/prices"));
            if(!market.is_array() || market.empty()) throw std::runtime_error("Trading Post index unavailable");
            std::unordered_set<uint32_t> tradeable;
            for(const auto& id:market) tradeable.insert(id.get<uint32_t>());
            std::unordered_map<uint32_t, int> vendor_map;

            for (size_t i = 0; i < total; i += BATCH_SIZE) {
            AlterEgo::Background::Checkpoint();
                size_t end = std::min(i + BATCH_SIZE, total);
                int pct = (int)((i * 100) / total);
                {
                    std::lock_guard<std::mutex> lock(s_mutex);
                    s_fetch_status = "Building item map " + std::to_string(pct) + "% ("
                                   + std::to_string(skin_map.size()) + " skin mappings found)";
                }

                std::string ids_param;
                for (size_t j = i; j < end; j++) {
                    if (!ids_param.empty()) ids_param += ",";
                    ids_param += std::to_string(ids[j]);
                }

                std::string url = "https://api.guildwars2.com/v2/items?ids=" + ids_param;
                std::string batch_response = HttpClient::Get(url);
                if (batch_response.empty()) throw std::runtime_error("Item metadata batch unavailable");

                try {
                    json batch = json::parse(batch_response);
                    if (!batch.is_array()) throw std::runtime_error("Invalid item metadata batch");

                    for (const auto& item : batch) {
                        std::string type = item.value("type", "");
                        const uint32_t item_id=item.value("id",uint32_t(0));
                        if(!item_id) continue;
                        const uint32_t default_skin=item.value("default_skin",uint32_t(0));
                        const bool equipment=type=="Armor" || type=="Weapon" || type=="Back";
                        if(tradeable.count(item_id)) {
                            auto add=[&](uint32_t skin) {
                                if(!skin) return;
                                auto& ids=skin_map[skin];
                                if(std::find(ids.begin(),ids.end(),item_id)==ids.end()) ids.push_back(item_id);
                                item_names[item_id]=item.value("name",std::string());
                            };
                            if(equipment) add(default_skin);
                            // Deterministic skin-unlock consumables, not random wardrobe unlocks.
                            if(type=="Consumable" && item.contains("details") && item["details"].is_object()) {
                                const auto& details=item["details"];
                                const auto kind=details.value("type",std::string());
                                if((kind=="Unlock" || kind=="Transmutation") && details.contains("skins") && details["skins"].is_array())
                                    for(const auto& id:details["skins"]) if(id.is_number_unsigned()) add(id.get<uint32_t>());
                            }
                        }
                        if(!equipment || !default_skin) continue;

                        // Detect cultural armor: Armor with race restrictions
                        if (type == "Armor" && item.contains("restrictions") &&
                            item["restrictions"].is_array() && !item["restrictions"].empty()) {
                            bool hasRace = false;
                            for (const auto& r : item["restrictions"]) {
                                std::string rs = r.get<std::string>();
                                if (rs == "Asura" || rs == "Charr" || rs == "Human" ||
                                    rs == "Norn" || rs == "Sylvari") {
                                    hasRace = true;
                                    break;
                                }
                            }
                            if (hasRace) {
                                int level = item.value("level", 0);
                                std::string slot;
                                if (item.contains("details") && item["details"].is_object()) {
                                    slot = item["details"].value("type", "");
                                }
                                int vendorPrice = GetCulturalArmorPrice(level, slot);
                                if (vendorPrice > 0) {
                                    auto old=vendor_map.find(default_skin);
                                    if(old==vendor_map.end() || vendorPrice<old->second) vendor_map[default_skin]=vendorPrice;
                                }
                            }
                        }
                    }
                } catch (...) {
                    throw;
                }

                // Stay well under the 600 req/min GW2 API limit
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }

            // Step 3: Store
            AlterEgo::Background::Checkpoint();
            {
                std::lock_guard<std::mutex> lock(s_mutex);
                s_skin_to_item = std::move(skin_map);
                s_item_names = std::move(item_names);
                s_vendor_prices = std::move(vendor_map);
                s_fetch_status = "Item map ready (" + std::to_string(s_skin_to_item.size()) + " skin-to-item mappings, "
                               + std::to_string(s_vendor_prices.size()) + " vendor skins)";
            }
            // Save the completed derived map before exposing it to price requests.
            SaveItemMap();
            s_item_map_ready = true;
            s_item_map_building = false;

        } catch (...) {
            std::lock_guard<std::mutex> lock(s_mutex);
            s_fetch_status = "Item map failed: incomplete API data.";
            s_item_map_building = false;
        }
    }

}
