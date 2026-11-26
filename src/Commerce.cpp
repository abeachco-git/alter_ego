#include "BackgroundWorkers.h"
#include "Commerce.h"
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
    static const int64_t PRICE_TTL_SEC = Commerce::PriceTTL;
    // One coordinator for both item and skin consumers. The request mutex is
    // never held during HTTP, SQLite or UI callbacks.
    namespace {
    std::mutex itemPriceMutex;
    std::unordered_map<uint32_t,PriceInfo> itemPrices;
    std::unordered_map<uint32_t,int64_t> priceRetry;
    std::unordered_set<uint32_t> pricePending;
    std::map<uint32_t,bool> priceQueue;
    bool priceWorkerRunning=false;
    bool FreshPrice(const PriceInfo& p,int64_t now){return p.fetched_at_unix>0&&now>=p.fetched_at_unix&&now-p.fetched_at_unix<Commerce::PriceTTL;}
    int PriceField(const json& j,const char* key){
        const auto& v=j.at(key);
        if(!v.is_number_integer())throw std::runtime_error("Invalid commerce integer");
        const auto n=v.get<int64_t>();if(n<0||n>2147483647LL)throw std::runtime_error("Invalid commerce range");
        return static_cast<int>(n);
    }
    // Called only by owned workers. Missing IDs are not inferred to be
    // nontradeable unless the API explicitly reports a partial/not-found result.
    std::unordered_map<uint32_t,PriceInfo> FetchItemBatch(const std::vector<uint32_t>& ids){
        std::unordered_map<uint32_t,PriceInfo> out;
        if(ids.empty())return out;
        std::string query;std::unordered_set<uint32_t> expected(ids.begin(),ids.end());
        for(auto id:ids){if(!query.empty())query+=",";query+=std::to_string(id);}
        auto response=HttpClient::GetEx("https://api.guildwars2.com/v2/commerce/prices?ids="+query);
        AlterEgo::Background::Checkpoint();
        if(response.status_code!=200&&response.status_code!=206&&response.status_code!=404)return {};
        try{
            auto body=AlterEgo::Untrusted::Parse(response.body,AlterEgo::Untrusted::PublicApi);
            if(response.status_code==404){
                if(!body.is_object()||body.value("text",std::string())!="all ids provided are invalid")return {};
            }else{
                if(!body.is_array())return {};
                for(const auto& e:body){
                    const auto& raw=e.at("id");if(!raw.is_number_integer())return {};
                    const auto n=raw.get<int64_t>();if(n<=0||n>4294967295LL)return {};
                    auto id=static_cast<uint32_t>(n);if(!expected.count(id)||out.count(id))return {};
                    PriceInfo p;p.item_id=id;p.tradeable=true;p.fetched_at_unix=NowUnix();
                    p.buy_price=PriceField(e.at("buys"),"unit_price");p.buy_quantity=PriceField(e.at("buys"),"quantity");
                    p.sell_price=PriceField(e.at("sells"),"unit_price");p.sell_quantity=PriceField(e.at("sells"),"quantity");
                    out.emplace(id,p);
                }
            }
            if(response.status_code==200&&out.size()!=expected.size())return {};
            if(response.status_code!=200)for(auto id:ids)if(!out.count(id)){PriceInfo p;p.item_id=id;p.fetched_at_unix=NowUnix();out.emplace(id,p);}
            return out;
        }catch(const AlterEgo::Background::Cancelled&){throw;}catch(...){return {};}
    }
    }


    std::string Commerce::s_data_dir;
    std::mutex Commerce::s_mutex;
    std::mutex Commerce::s_publication_mutex;
    uint64_t Commerce::s_mapping_generation=0;
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
        try {auto rows=AlterEgo::Cache::ReadItemPrices(AlterEgo::Cache::RootFromLegacySkinDirectory(dir));std::lock_guard<std::mutex> lock(itemPriceMutex);itemPrices=std::move(rows);}catch(...){}
    }

    ItemPriceStatus Commerce::GetItemPriceStatus(uint32_t id){
        std::lock_guard<std::mutex> lock(itemPriceMutex);
        ItemPriceStatus s;auto it=itemPrices.find(id);if(it!=itemPrices.end())s.price=it->second;
        s.pending=pricePending.count(id)!=0;auto r=priceRetry.find(id);if(r!=priceRetry.end())s.retry_at=r->second;
        return s;
    }
    void Commerce::RequestItemPrices(const std::vector<uint32_t>& ids,bool manual){
        if(!AlterEgo::AutoRefreshPolicy::AllowRequest(manual,AlterEgo::AutoRefreshPolicy::Dataset::TradingPrices))return;
        {
            std::lock_guard<std::mutex> lock(itemPriceMutex);const auto now=NowUnix();
            for(auto id:ids){
                if(!id)continue;
                if(pricePending.count(id)){auto q=priceQueue.find(id);if(manual&&q!=priceQueue.end())q->second=true;continue;}
                auto p=itemPrices.find(id);auto r=priceRetry.find(id);
                if(!manual&&((p!=itemPrices.end()&&FreshPrice(p->second,now))||(r!=priceRetry.end()&&r->second>now)))continue;
                if(pricePending.size()>=10000)break;
                pricePending.insert(id);priceQueue.emplace(id,manual);
            }
            if(priceWorkerRunning||priceQueue.empty())return;
            priceWorkerRunning=true;
        }
        AlterEgo::Background::Workers().Launch([]{
            for(;;){
                AlterEgo::Background::Checkpoint();
                std::vector<uint32_t> batch;bool manual=false;
                {
                    std::lock_guard<std::mutex> lock(itemPriceMutex);
                    if(priceQueue.empty()){priceWorkerRunning=false;return;}
                    for(int pass=0;pass<2;++pass)for(auto it=priceQueue.begin();it!=priceQueue.end()&&batch.size()<200;){
                        if(it->second!=(pass==0)){++it;continue;}
                        if(!it->second&&!AlterEgo::AutoRefreshPolicy::Enabled(AlterEgo::AutoRefreshPolicy::Dataset::TradingPrices)){pricePending.erase(it->first);it=priceQueue.erase(it);continue;}
                        manual=manual||it->second;batch.push_back(it->first);it=priceQueue.erase(it);
                    }
                }
                if(batch.empty())continue;
                AlterEgo::ApiActivity::ReasonScope reason(manual?AlterEgo::ApiActivity::Reason::Manual:AlterEgo::ApiActivity::Reason::Visible);
                auto rows=FetchItemBatch(batch);
                const bool saved=AlterEgo::Cache::WriteItemPrices(AlterEgo::Cache::RootFromLegacySkinDirectory(s_data_dir),rows);
                {
                    std::lock_guard<std::mutex> lock(itemPriceMutex);
                    for(auto id:batch){
                        auto it=rows.find(id);
                        if(saved&&it!=rows.end()){itemPrices[id]=it->second;priceRetry.erase(id);}
                        else priceRetry[id]=NowUnix()+900;
                        pricePending.erase(id);
                    }
                    if(saved&&!rows.empty())++s_price_generation;
                }
                AlterEgo::Background::Pause(std::chrono::milliseconds(200));
            }
        },[]{std::lock_guard<std::mutex> lock(itemPriceMutex);priceWorkerRunning=false;priceQueue.clear();pricePending.clear();});
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
            ++s_mapping_generation;
            for(auto skin:changed)s_prices.erase(skin);
            ++s_price_generation;
        }
        s_skin_to_item=std::move(skins);s_item_names=std::move(names);s_vendor_prices=std::move(vendors);
    }

    void Commerce::FetchPricesForSkins(const std::vector<uint32_t>& skin_ids) {
        if (!s_item_map_ready || s_fetching.exchange(true)) return;
        std::vector<uint32_t> item_ids;
        std::unordered_map<uint32_t, std::vector<uint32_t>> item_to_skin;
        uint64_t mappingGeneration=0;
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            mappingGeneration=s_mapping_generation;
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
        AlterEgo::Background::Workers().Launch([ids = std::move(item_ids), mapping = std::move(item_to_skin),mappingGeneration]() mutable { AlterEgo::ApiActivity::ReasonScope reason(AlterEgo::ApiActivity::Reason::Visible);FetchPriceWorker(std::move(ids), std::move(mapping),mappingGeneration); }, [] { s_fetching = false; });
    }

    void Commerce::FetchPriceWorker(std::vector<uint32_t> item_ids,
                     std::unordered_map<uint32_t, std::vector<uint32_t>> item_to_skin,uint64_t mappingGeneration) {
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
            std::vector<uint32_t> needed;
            std::unordered_map<uint32_t,PriceInfo> observations;
            {
                std::lock_guard<std::mutex> lock(itemPriceMutex);
                for(size_t j=i;j<end;++j){
                    auto id=item_ids[j];auto it=itemPrices.find(id);auto retry=priceRetry.find(id);
                    if(it!=itemPrices.end()&&FreshPrice(it->second,NowUnix()))observations.emplace(id,it->second);
                    else if(!pricePending.count(id)&&(retry==priceRetry.end()||retry->second<=NowUnix())){
                        pricePending.insert(id);needed.push_back(id);
                    }
                }
            }
            if(!needed.empty()){
                // Guard claims even on cancellation and exceptions.
                struct Claims{const std::vector<uint32_t>& ids;~Claims(){std::lock_guard<std::mutex> lock(itemPriceMutex);for(auto id:ids)pricePending.erase(id);}} claims{needed};
                auto rows=FetchItemBatch(needed);
                const bool saved=AlterEgo::Cache::WriteItemPrices(AlterEgo::Cache::RootFromLegacySkinDirectory(s_data_dir),rows);
                std::lock_guard<std::mutex> lock(itemPriceMutex);
                for(auto id:needed){auto it=rows.find(id);if(saved&&it!=rows.end()){
                    itemPrices[id]=it->second;observations[id]=it->second;priceRetry.erase(id);
                }else priceRetry[id]=NowUnix()+900;}
                if(saved&&!rows.empty())++s_price_generation;
            }
            for(size_t j=i;j<end;++j){
                auto id=item_ids[j];auto it=observations.find(id);
                if(it==observations.end()){for(auto skin:item_to_skin[id])failed.insert(skin);continue;}
                for(auto skin:item_to_skin[id])if(BetterPrice(it->second,best[skin]))best[skin]=it->second;
            }
            AlterEgo::Background::Pause(std::chrono::milliseconds(200));
        }
        std::lock_guard<std::mutex> publication(s_publication_mutex);
        AlterEgo::Background::Checkpoint();
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            if(mappingGeneration!=s_mapping_generation){
                s_fetch_status="Item mappings changed; prices will be retried.";
                s_fetching=false;return;
            }
        }
        // Commit only this worker's accepted observations, then adopt them.
        // A failed database write must not publish successful freshness.
        json rows=json::object();
        for(auto& [skin,info]:best){
            if(failed.count(skin))continue;
            if(!info.fetched_at_unix)info.fetched_at_unix=NowUnix();
            rows[std::to_string(skin)]={{"item_id",info.item_id},{"buy",info.buy_price},{"sell",info.sell_price},
                {"buy_q",info.buy_quantity},{"sell_q",info.sell_quantity},{"tradeable",info.tradeable},{"ts",info.fetched_at_unix}};
        }
        AlterEgo::Background::Checkpoint();
        const bool saved=rows.empty()||AlterEgo::Cache::WriteCommerce(
            AlterEgo::Cache::RootFromLegacySkinDirectory(s_data_dir),AlterEgo::Cache::CommerceDataset::Prices,rows);
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            if(saved){
                for(auto& [skin,info]:best)if(!failed.count(skin))s_prices[skin]=info;
                if(!rows.empty())++s_price_generation;
            }
            s_fetch_status=!saved?"Price persistence failed; previous prices retained.":
                failed.empty()?"":"Some prices could not be retrieved; previous prices retained.";
        }
        s_fetching=false;
    }

    bool Commerce::SavePriceCache() {
        std::lock_guard<std::mutex> publication(s_publication_mutex);
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
        std::lock_guard<std::mutex> publication(s_publication_mutex);
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

                std::string ids_param;
                for (size_t j = i; j < end; j++) {
                    if (!ids_param.empty()) ids_param += ",";
                    ids_param += std::to_string(ids[j]);
                }

                std::string url = "https://api.guildwars2.com/v2/items?ids=" + ids_param;
                const auto requestStamp=Cache::ReferenceSnapshotTime();
                std::string batch_response = HttpClient::Get(url);
                Background::Checkpoint();
                if (batch_response.empty()) throw std::runtime_error("Item metadata batch unavailable");

                try {
                    json batch = Untrusted::Parse(batch_response,Untrusted::PublicApi);
                    AlterEgo::PublicCatalog::ValidateBatch(batch,std::set<uint32_t>(ids.begin()+i,ids.begin()+end));

                    for (const auto& item : batch) {
                        std::string type = item.value("type", "");
                        const uint32_t item_id=item.value("id",uint32_t(0));
                        if(!item_id) continue;
                        if(incremental){observations.push_back(item);itemStamps.emplace(item_id,requestStamp);}
                        else if(!PublicCatalog::Kind(item).empty()){upgrades.push_back(item);itemStamps.emplace(item_id,requestStamp);}
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
