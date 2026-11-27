#include "BackgroundWorkers.h"
#include "RefreshEngine.h"
#include "Commerce.h"
#include "CommercePriceCore.h"
#include "HttpClient.h"
#include "SkinCache.h"
#include "CommerceCache.h"
#include "PublicCatalog.h"
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
    namespace {
    constexpr const char* PriceDataset="commerce_item_prices";
    // All price observations share the existing Commerce state mutex. Skin
    // projections derive from this map; legacy choices are fallback only.
    std::unordered_map<uint32_t,PriceInfo> itemPrices;
    std::string priceStatus;
    unsigned priceTotal=0,priceDone=0,priceFailed=0;
    std::unordered_map<uint32_t,PriceInfo> FetchItemBatch(const std::vector<uint32_t>& ids,bool& attempted){
        if(ids.empty())return {};
        std::string query;
        for(auto id:ids){if(!query.empty())query+=",";query+=std::to_string(id);}
        const auto response=HttpClient::GetEx("https://api.guildwars2.com/v2/commerce/prices?ids="+query);
        attempted=response.attempted;
        AlterEgo::Background::Checkpoint();
        return ParseCommercePrices(response.status_code,response.body,ids,NowUnix());
    }
    }

    std::string Commerce::s_data_dir;
    std::mutex Commerce::s_mutex;
    std::mutex Commerce::s_publication_mutex;
    std::unordered_map<uint32_t, PriceInfo> Commerce::s_prices;
    std::unordered_map<uint32_t, std::vector<uint32_t>> Commerce::s_skin_to_item;
    std::unordered_map<uint32_t, std::string> Commerce::s_item_names;
    std::atomic<uint64_t> Commerce::s_price_generation{0};
    std::unordered_map<uint32_t, int> Commerce::s_vendor_prices;
    std::atomic<unsigned> Commerce::s_fetching{0};
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

    ItemPriceStatus Commerce::GetItemPriceStatus(uint32_t id){
        ItemPriceStatus s;
        {std::lock_guard<std::mutex> lock(s_mutex);auto it=itemPrices.find(id);if(it!=itemPrices.end())s.price=it->second;}
        const auto claim=AlterEgo::RefreshEngine::InspectId(PriceDataset,id);
        s.pending=claim.pending;if(claim.retrySeconds)s.retry_at=NowUnix()+claim.retrySeconds;
        return s;
    }
    void Commerce::RequestItemPrices(const std::vector<uint32_t>& ids,bool manual){
        RequestPrices(ids,manual,manual);
    }
    void Commerce::RequestPrices(const std::vector<uint32_t>& ids,bool manual,bool explicitDemand){
        using namespace AlterEgo;
        if(!AutoRefreshPolicy::AllowRequest(explicitDemand,AutoRefreshPolicy::Dataset::TradingPrices)||s_data_dir.empty())return;
        std::vector<uint32_t> due;
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            due=SelectPriceRequests(ids,itemPrices,manual,NowUnix(),PriceTTL);
        }
        if(due.empty())return;
        auto claim=RefreshEngine::ClaimIds(PriceDataset,due,manual);
        if(claim->ids.empty())return;
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            if(s_fetching++==0){priceTotal=priceDone=priceFailed=0;}
            priceTotal+=static_cast<unsigned>(claim->ids.size());
            priceStatus="Fetching prices "+std::to_string(priceDone)+"/"+std::to_string(priceTotal);
        }
        auto finish=[](bool failed){
            std::lock_guard<std::mutex> lock(s_mutex);
            if(failed)++priceFailed;
            if(--s_fetching==0)priceStatus=priceFailed?"Some prices could not be retrieved; previous prices retained.":"";
        };
        Background::Workers().Launch([claim,manual,explicitDemand,finish]{
            ApiActivity::ReasonScope reason(manual?ApiActivity::Reason::Manual:ApiActivity::Reason::Visible);
            for(size_t i=0;i<claim->ids.size();i+=200){
                Background::Checkpoint();
                if(!explicitDemand&&!AutoRefreshPolicy::Enabled(AutoRefreshPolicy::Dataset::TradingPrices))break;
                const auto end=std::min(i+200,claim->ids.size());
                std::vector<uint32_t> batch(claim->ids.begin()+i,claim->ids.begin()+end),needed;
                // Recheck after acquiring ownership to close the read/claim race.
                {std::lock_guard<std::mutex> lock(s_mutex);
                    needed=SelectPriceRequests(batch,itemPrices,manual,NowUnix(),PriceTTL);
                }
                bool attempted=false;auto rows=FetchItemBatch(needed,attempted);
                Background::Checkpoint();
                const bool saved=Cache::WriteItemPrices(Cache::RootFromLegacySkinDirectory(s_data_dir),rows);
                {
                    std::lock_guard<std::mutex> lock(s_mutex);
                    // IDs satisfied by the cache during the ownership race are
                    // successful skips, not failed HTTP requests.
                    for(auto id:needed){
                        auto it=rows.find(id);
                        if(saved&&it!=rows.end()){
                            auto prev=itemPrices.find(id);
                            if(prev==itemPrices.end()||prev->second.fetched_at_unix<=it->second.fetched_at_unix)itemPrices[id]=it->second;
                        }else{if(attempted)claim->attempted.insert(id);++priceFailed;}
                    }
                    if(saved&&!rows.empty())++s_price_generation;
                    priceDone+=static_cast<unsigned>(batch.size());
                    priceStatus="Fetching prices "+std::to_string(priceDone)+"/"+std::to_string(priceTotal);
                }
                // Clear any old cooldown on cache-satisfied IDs, even when
                // a different ID in this batch never reached the transport.
                for(auto id:batch)
                    if(std::find(needed.begin(),needed.end(),id)==needed.end())
                        claim->ReleaseIds(&id,&id+1,true);
                claim->ReleaseIds(needed.begin(),needed.end(),attempted);
                if(!needed.empty())Background::Pause(std::chrono::milliseconds(200));
            }
            finish(false);
        },[finish]{finish(true);});
    }

    std::optional<PriceInfo> Commerce::GetPrice(uint32_t skin_id) {
        std::lock_guard<std::mutex> lock(s_mutex);
        const auto mapping=s_skin_to_item.find(skin_id);
        const auto old=s_prices.find(skin_id);
        if(mapping==s_skin_to_item.end()||mapping->second.empty()){
            if(s_item_map_ready)return PriceInfo{};
            return old==s_prices.end()?std::nullopt:std::optional<PriceInfo>(old->second);
        }
        return SelectSkinPrice(mapping->second,itemPrices,old==s_prices.end()?std::nullopt:std::optional<PriceInfo>(old->second));
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

    std::string Commerce::GetPriceFetchStatus(){
        std::lock_guard<std::mutex> lock(s_mutex);
        return priceStatus;
    }

    bool Commerce::IsItemMapReady() {
        return s_item_map_ready;
    }

    uint32_t Commerce::GetItemIdForSkin(uint32_t skin_id) {
        const auto price = GetPrice(skin_id);
        return price && price->tradeable ? price->item_id : 0;
    }
    std::unordered_map<uint32_t,std::vector<uint32_t>> Commerce::UniqueSkinCandidates(){
        std::lock_guard<std::mutex> lock(s_mutex);
        std::unordered_map<uint32_t,unsigned> uses;
        for(const auto& [skin,items]:s_skin_to_item)for(auto item:items)++uses[item];
        std::unordered_map<uint32_t,std::vector<uint32_t>> out;
        for(const auto& [skin,items]:s_skin_to_item)for(auto item:items)if(uses[item]==1)out[skin].push_back(item);
        return out;
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

    // Caller owns publication mutex; no DB or network operations under s_mutex.
    void Commerce::AdoptMapping(CommerceCandidates skins,std::unordered_map<uint32_t,std::string> names,std::unordered_map<uint32_t,int> vendors) {
        std::lock_guard<std::mutex> lock(s_mutex);
        const auto changed=ChangedCommerceCandidates(s_skin_to_item,skins);
        if(!changed.empty()){
            for(auto skin:changed)s_prices.erase(skin);
            ++s_price_generation;
        }
        s_skin_to_item=std::move(skins);s_item_names=std::move(names);s_vendor_prices=std::move(vendors);
    }

    void Commerce::FetchPricesForSkins(const std::vector<uint32_t>& skin_ids,bool manual) {
        if(!s_item_map_ready)return;
        std::vector<uint32_t> ids;
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            for(auto skin:skin_ids){auto it=s_skin_to_item.find(skin);if(it!=s_skin_to_item.end())ids.insert(ids.end(),it->second.begin(),it->second.end());}
        }
        std::sort(ids.begin(),ids.end());ids.erase(std::unique(ids.begin(),ids.end()),ids.end());
        // Skin selection and the existing Shopping demand are explicit lazy
        // reads; only its Refresh Prices action forces fresh observations.
        RequestPrices(ids,manual,true);
    }

    bool Commerce::LoadPriceCache() {
        std::lock_guard<std::mutex> publication(s_publication_mutex);
        if(s_data_dir.empty())return false;
        try{
            const auto root=AlterEgo::Cache::RootFromLegacySkinDirectory(s_data_dir);
            auto legacy=AlterEgo::Cache::ReadCommerce(root,AlterEgo::Cache::CommerceDataset::Prices,std::filesystem::u8path(s_data_dir)/"price_cache_v2.json");
            std::unordered_map<uint32_t,PriceInfo> fallback;
            if(legacy&&legacy->is_object())for(const auto& [key,e]:legacy->items()){
                if(!e.is_object())continue;
                PriceInfo p;p.item_id=e.value("item_id",0u);p.fetched_at_unix=e.value("ts",int64_t(0));
                p.buy_price=e.value("buy",0);p.sell_price=e.value("sell",0);
                p.buy_quantity=e.value("buy_q",0);p.sell_quantity=e.value("sell_q",0);p.tradeable=e.value("tradeable",false);
                fallback.emplace(static_cast<uint32_t>(std::stoul(key)),p);
            }
            auto rows=AlterEgo::Cache::ReadItemPrices(root);
            std::lock_guard<std::mutex> lock(s_mutex);
            itemPrices=std::move(rows);s_prices=std::move(fallback);++s_price_generation;
            return true;
        }catch(...){return false;}
    }

    // Versioned derived cache: legacy one-item/rarity maps must be rebuilt.
    bool Commerce::SaveItemMap() {
        std::lock_guard<std::mutex> publication(s_publication_mutex);
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
        std::lock_guard<std::mutex> publication(s_publication_mutex);
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
            AdoptMapping(std::move(skins),std::move(names),std::move(vendors));
            s_item_map_ready=true;
            return true;
        } catch(...) { return false; }
    }

    void Commerce::BuildItemMapAsync() {
        if (s_item_map_ready || s_item_map_building.exchange(true)) return;
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            s_fetch_status = "Building item map...";
        }
        AlterEgo::Background::Workers().Launch([] { AlterEgo::ApiActivity::ReasonScope reason(AlterEgo::ApiActivity::Reason::Background);ItemMapWorker(); }, [] { s_item_map_building = false; });
    }

    void Commerce::RefreshUpgradeCatalogAsync(bool manual,bool itemsOnly) {
        using namespace AlterEgo;
        if(!AutoRefreshPolicy::AllowRequest(manual,AutoRefreshPolicy::Dataset::PublicCatalog))return;
        auto current=PublicCatalog::Get();
        const auto now=PublicCatalog::Now();
        const bool discover=PublicCatalog::DatasetDue(*current,"public_item_discovery",now,manual&&itemsOnly);
        const bool refresh=PublicCatalog::Due(current->upgrades,current->upgradeAttempt,now,manual&&!itemsOnly,PublicCatalog::Month);
        if((!discover&&!refresh)||s_item_map_building.exchange(true))return;
        {std::lock_guard<std::mutex> lock(s_mutex);s_fetch_status="Refreshing public item catalogs...";}
        Background::Workers().Launch([discover,refresh,manual]{
            ApiActivity::ReasonScope reason(manual?ApiActivity::Reason::Manual:ApiActivity::Reason::Background);
            if(!s_item_map_ready || PublicCatalog::Get()->index.empty()){ItemMapWorker();return;}
            ItemMapWorker(true,discover,refresh);
        },[]{std::lock_guard<std::mutex> lock(s_mutex);s_fetch_status="Item catalog refresh failed; cache retained";s_item_map_building=false;});
    }

    void Commerce::ItemMapWorker(bool incremental,bool discover,bool refresh) {
        using namespace AlterEgo;
        try {
            const auto root=Cache::RootFromLegacySkinDirectory(s_data_dir);
            const auto old=PublicCatalog::Get();
            PublicCatalog::DatasetAttempt discovery(root,"public_item_discovery",discover);
            PublicCatalog::DatasetAttempt upgradesAttempt(root,"public_upgrades",refresh);
            const auto index=discover?PublicCatalog::Index(PublicCatalog::Fetch("/v2/items")):old->index;
            if(index.empty()||(!old->index.empty()&&index.size()<old->index.size()*9/10))
                throw std::runtime_error("Incomplete item index");
            const auto planned=incremental?PublicCatalog::PlanItems(index,old->index,old->upgradeIds,discover,refresh):index;
            std::vector<uint32_t> ids(planned.begin(),planned.end());
            json upgrades=json::array(),observations=json::array();
            Cache::ItemRequestStamps itemStamps;
            const size_t BATCH_SIZE=200,total=ids.size();
            std::unordered_map<uint32_t,std::vector<uint32_t>> skin_map;
            std::unordered_map<uint32_t,std::string> item_names;
            std::unordered_map<uint32_t,int> vendor_map;
            if(incremental&&discover){
                std::lock_guard<std::mutex> lock(s_mutex);
                skin_map=s_skin_to_item;item_names=s_item_names;vendor_map=s_vendor_prices;
            }
            std::unordered_set<uint32_t> tradeable;
            const bool newItems=discover&&(!incremental||std::any_of(ids.begin(),ids.end(),[&](uint32_t id){return !old->index.count(id);}));
            if(newItems){
                const auto market=PublicCatalog::Index(PublicCatalog::Fetch("/v2/commerce/prices"));
                tradeable.insert(market.begin(),market.end());
            }

            for (size_t i = 0; i < total; i += BATCH_SIZE) {
            AlterEgo::Background::Checkpoint();
                size_t end = std::min(i + BATCH_SIZE, total);
                int pct = (int)((i * 100) / total);
                {
                    std::lock_guard<std::mutex> lock(s_mutex);
                    s_fetch_status = "Building item map " + std::to_string(pct) + "% ("
                                   + std::to_string(skin_map.size()) + " skin mappings found)";
                }

                // Full raw observations preserve cultural/vendor and skin
                // mapping fields that are not part of the tooltip projection.
                const std::vector<uint32_t> requested(ids.begin()+i,ids.begin()+end);
                Cache::ItemRequestStamps batchStamps;
                auto batch=GW2API::AcquireCatalogItemBatch(requested,incremental,batchStamps);
                try {
                    for (const auto& item : batch) {
                        std::string type = item.value("type", "");
                        const uint32_t item_id=item.value("id",uint32_t(0));
                        if(!item_id) continue;
                        if(incremental){observations.push_back(item);itemStamps.emplace(item_id,batchStamps.at(item_id));}
                        else if(!PublicCatalog::Kind(item).empty()){upgrades.push_back(item);itemStamps.emplace(item_id,batchStamps.at(item_id));}
                        if(incremental&&(!discover||old->index.count(item_id)))continue;
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
                AlterEgo::Background::Pause(std::chrono::milliseconds(200));
            }

            // Step 3: Store. A price worker cannot commit against an obsolete mapping.
            std::lock_guard<std::mutex> publication(s_publication_mutex);
            AlterEgo::Background::Checkpoint();
            // Persist derived mappings before marking the ID check complete.
            // Repeating a failed discovery is idempotent.
            if(newItems){
                json skins=json::object(),names=json::object(),vendors=json::object();
                for(const auto& kv:skin_map)skins[std::to_string(kv.first)]=kv.second;
                for(const auto& kv:item_names)names[std::to_string(kv.first)]=kv.second;
                for(const auto& kv:vendor_map)vendors[std::to_string(kv.first)]=kv.second;
                if(!Cache::WriteCommerce(root,Cache::CommerceDataset::Mapping,
                    json{{"version",2},{"skins",skins},{"names",names},{"vendors",vendors}}))
                    throw std::runtime_error("Item mapping persistence failed");
                AdoptMapping(std::move(skin_map),std::move(item_names),std::move(vendor_map));
                s_item_map_ready=true;
            }
            if(incremental)PublicCatalog::CommitIncrementalItems(root,index,observations,discover,refresh,itemStamps);
            else PublicCatalog::CommitUpgrades(root,index,upgrades,itemStamps);
            discovery.Finish();upgradesAttempt.Finish();
            {
                std::lock_guard<std::mutex> lock(s_mutex);
                s_fetch_status="Public item catalogs up to date";
            }
            s_item_map_ready = true;
            s_item_map_building = false;

        } catch (...) {
            std::lock_guard<std::mutex> lock(s_mutex);
            s_fetch_status = "Item map failed: incomplete API data.";
            s_item_map_building = false;
        }
    }

}
