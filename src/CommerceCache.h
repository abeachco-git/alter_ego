#pragma once
#include "AchievementCatalogCache.h"

namespace AlterEgo::Cache {
enum class CommerceDataset { Mapping, Prices };
inline const char* CommerceName(CommerceDataset d) {
    return d==CommerceDataset::Mapping?"commerce_item_map":"commerce_prices";
}
inline bool WriteCommerce(const std::string& root,CommerceDataset d,const Json& data) {
    if(root.empty()||!data.is_object())return false;
    try {
        std::lock_guard<std::mutex> lock(Mutex());
        const auto path=Storage::Paths(root).Cache();
        std::filesystem::create_directories(path.parent_path());
        Database::Connection db(path.u8string(),Database::Connection::OpenMode::Create);
        Initialize(db);
        Database::Transaction tx(db);
        if(d==CommerceDataset::Mapping) {
            if(data.value("version",0)!=2||!data.at("skins").is_object()||data.at("skins").empty()||
               !data.at("names").is_object()||!data.at("vendors").is_object())return false;
            db.Exec("DELETE FROM commerce_skin_items; DELETE FROM commerce_item_names; DELETE FROM commerce_vendor_prices;");
            auto skin=db.Prepare("INSERT INTO commerce_skin_items VALUES(?,?,?)");
            auto name=db.Prepare("INSERT INTO commerce_item_names VALUES(?,?)");
            auto vendor=db.Prepare("INSERT INTO commerce_vendor_prices VALUES(?,?)");
            for(const auto& [key,ids]:data.at("skins").items()) {
                if(!ids.is_array())throw std::runtime_error("Invalid skin item mapping");
                const auto id=CatalogId(key);int n=0;
                for(const auto& item:ids) {
                    skin.Bind(1,id);skin.Bind(2,item.get<uint32_t>());skin.Bind(3,n++);
                    skin.Execute();skin.Reset();
                }
            }
            for(const auto& [key,v]:data.at("names").items()) {
                name.Bind(1,CatalogId(key));name.Bind(2,v.get<std::string>());name.Execute();name.Reset();
            }
            for(const auto& [key,v]:data.at("vendors").items()) {
                vendor.Bind(1,CatalogId(key));vendor.Bind(2,v.get<int>());vendor.Execute();vendor.Reset();
            }
        } else {
            // A choice is a verified observation of the cheapest candidate
            // for that skin at its own fetch time. Keep item observations
            // normalized, without changing the existing winner semantics.
            auto current=db.Prepare("SELECT fetched_at FROM commerce_skin_price_choices WHERE skin_id=?");
            auto item=db.Prepare("INSERT INTO commerce_item_prices VALUES(?,?,?,?,?,?,?) ON CONFLICT(item_id,fetched_at) DO UPDATE SET buy=excluded.buy,sell=excluded.sell,buy_quantity=excluded.buy_quantity,sell_quantity=excluded.sell_quantity,tradeable=excluded.tradeable");
            auto choice=db.Prepare("INSERT INTO commerce_skin_price_choices VALUES(?,?,?) ON CONFLICT(skin_id) DO UPDATE SET item_id=excluded.item_id,fetched_at=excluded.fetched_at");
            for(const auto& [key,v]:data.items()) {
                const auto id=CatalogId(key);
                const auto stamp=v.value("ts",int64_t(0));
                current.Bind(1,id);
                const bool older=current.Step()&&current.Integer(0)>stamp;
                current.Reset();if(older)continue;
                const auto itemId=v.value("item_id",0u);
                if(itemId) {
                    item.Bind(1,itemId);item.Bind(2,stamp);item.Bind(3,v.value("buy",0));item.Bind(4,v.value("sell",0));
                    item.Bind(5,v.value("buy_q",0));item.Bind(6,v.value("sell_q",0));item.Bind(7,v.value("tradeable",false));
                    item.Execute();item.Reset();
                }
                choice.Bind(1,id);if(itemId)choice.Bind(2,itemId);else choice.BindNull(2);
                choice.Bind(3,stamp);choice.Execute();choice.Reset();
            }
            db.Exec("DELETE FROM commerce_item_prices WHERE NOT EXISTS (SELECT 1 FROM commerce_skin_price_choices c WHERE c.item_id=commerce_item_prices.item_id AND c.fetched_at=commerce_item_prices.fetched_at)");
        }
        auto fresh=db.Prepare("INSERT INTO reference_freshness VALUES(?,?) ON CONFLICT(dataset) DO UPDATE SET saved_at=excluded.saved_at");
        fresh.Bind(1,std::string(CommerceName(d)));
        fresh.Bind(2,std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
        fresh.Execute();tx.Commit();return true;
    } catch(...) {return false;}
}
inline std::optional<Json> ReadCommerce(const std::string& root,CommerceDataset d,const std::filesystem::path& legacy) {
    if(root.empty())return std::nullopt;
    {
        std::lock_guard<std::mutex> lock(Mutex());
        const auto path=Storage::Paths(root).Cache();
        if(std::filesystem::exists(path)) {
            Database::Connection db(path.u8string(),Database::Connection::OpenMode::Existing);
            Initialize(db);
            Database::Transaction tx(db);
            auto fresh=db.Prepare("SELECT saved_at FROM reference_freshness WHERE dataset=?");
            fresh.Bind(1,std::string(CommerceName(d)));
            if(fresh.Step()) {
                Json out=Json::object();
                if(d==CommerceDataset::Mapping) {
                    out={{"version",2},{"skins",Json::object()},{"names",Json::object()},{"vendors",Json::object()}};
                    auto skin=db.Prepare("SELECT skin_id,item_id FROM commerce_skin_items ORDER BY skin_id,ordinal");
                    while(skin.Step())out["skins"][std::to_string(skin.Integer(0))].push_back(skin.Integer(1));
                    auto name=db.Prepare("SELECT item_id,name FROM commerce_item_names");
                    while(name.Step())out["names"][std::to_string(name.Integer(0))]=name.Text(1);
                    auto vendor=db.Prepare("SELECT skin_id,copper FROM commerce_vendor_prices");
                    while(vendor.Step())out["vendors"][std::to_string(vendor.Integer(0))]=vendor.Integer(1);
                } else {
                    auto q=db.Prepare(R"SQL(
SELECT c.skin_id,COALESCE(c.item_id,0),c.fetched_at,COALESCE(p.buy,0),COALESCE(p.sell,0),
COALESCE(p.buy_quantity,0),COALESCE(p.sell_quantity,0),COALESCE(p.tradeable,0)
FROM commerce_skin_price_choices c LEFT JOIN commerce_item_prices p
ON c.item_id=p.item_id AND c.fetched_at=p.fetched_at
)SQL");
                    while(q.Step())out[std::to_string(q.Integer(0))]={{"item_id",q.Integer(1)},{"ts",q.Integer(2)},
                        {"buy",q.Integer(3)},{"sell",q.Integer(4)},{"buy_q",q.Integer(5)},{"sell_q",q.Integer(6)},{"tradeable",q.Integer(7)!=0}};
                }
                tx.Commit();return out;
            }
        }
    }
    std::ifstream file(legacy);if(!file)return std::nullopt;
    auto data=Json::parse(file);file.close();
    if(!WriteCommerce(root,d,data))return std::nullopt;
    std::error_code ec;std::filesystem::remove(legacy,ec);
    return data;
}
} // namespace AlterEgo::Cache
