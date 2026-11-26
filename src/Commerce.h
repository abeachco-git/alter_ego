#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <atomic>
#include <chrono>
#include <optional>
#include "SkinSearchCore.h"

namespace Skinventory {

    // Format copper value as gold/silver/copper string
    std::string FormatCoins(int copper);

    struct ItemPriceStatus {
        std::optional<PriceInfo> price;
        bool pending=false;
        int64_t retry_at=0;
    };
    class Commerce {
    public:
        // Set data directory for item map cache
        static void SetDataDirectory(const std::string& dir);

        // Fetch TP price for a single skin (async, on-demand)
        static void FetchPriceForSkin(uint32_t skin_id);
        static constexpr int64_t PriceTTL=3600;
        static ItemPriceStatus GetItemPriceStatus(uint32_t id);
        static void RequestItemPrices(const std::vector<uint32_t>& ids,bool manual=false);

        // Batch fetch prices for multiple skins (for shopping list)
        static void FetchPricesForSkins(const std::vector<uint32_t>& skin_ids,bool manual=false);

        // Thread-safe cached price snapshot. Empty if not fetched yet.
        static std::optional<PriceInfo> GetPrice(uint32_t skin_id);

        // Get vendor price for a skin (0 if not a vendor skin). In copper.
        static int GetVendorPrice(uint32_t skin_id);

        // Is a price fetch in progress?
        static bool IsFetching();
        static std::string GetFetchStatus();

        // Item map: skin_id -> candidate item IDs and actual TP item names
        // Must be built once from /v2/items
        static void BuildItemMapAsync();
        static void RefreshUpgradeCatalogAsync(bool manual, bool itemsOnly=false);
        static bool IsItemMapReady();
        static uint32_t GetItemIdForSkin(uint32_t skin_id);
        static std::unordered_map<uint32_t,std::vector<uint32_t>> UniqueSkinCandidates();
        static std::string GetItemName(uint32_t item_id);
        static uint64_t GetPriceGeneration();

        // Persistence for item map
        static bool LoadItemMap();
        static bool SaveItemMap();

        // Persistence for cached prices
        static bool LoadPriceCache();

    private:
        static void RequestPrices(const std::vector<uint32_t>& ids,bool manual,bool explicitDemand);
        static void ItemMapWorker(bool incremental=false,bool discover=true,bool refresh=true);

        static std::string s_data_dir;
        static std::mutex s_mutex;
        // Serialize durable mapping/price publication; never acquire from getters.
        static std::mutex s_publication_mutex;
        static uint64_t s_mapping_generation; // guarded by s_mutex
        static void AdoptMapping(CommerceCandidates skins,std::unordered_map<uint32_t,std::string> names,std::unordered_map<uint32_t,int> vendors);

        // skin_id -> PriceInfo
        static std::unordered_map<uint32_t, PriceInfo> s_prices;

        // skin_id -> all known Trading Post item candidates
        static std::unordered_map<uint32_t, std::vector<uint32_t>> s_skin_to_item;
        static std::unordered_map<uint32_t, std::string> s_item_names;
        static std::atomic<uint64_t> s_price_generation;

        // skin_id -> vendor price in copper (cultural armor)
        static std::unordered_map<uint32_t, int> s_vendor_prices;

        static std::atomic<unsigned> s_fetching;
        static std::string s_fetch_status;
        static std::atomic<bool> s_item_map_ready;
        static std::atomic<bool> s_item_map_building;
    };

}
