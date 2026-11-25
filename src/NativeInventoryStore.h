#pragma once
#include "CacheDatabase.h"
#include "NativeInventoryCore.h"
#include <map>
#include <tuple>
namespace AlterEgo::NativeInventory {
struct ScopeState {std::string account,scope,character,status;int64_t attempted=0,success=0;};
struct Row {Slot slot;std::string name,rarity,type,chat;int64_t captured=0;};
struct ItemPresentation {std::string icon,description;Json details=Json::object();bool complete=false;};
struct WalletRow {std::string account,name,description;uint32_t id=0;int64_t value=0;int order=0;};
struct GuildRow {std::string id,name,tag,status;int64_t attempted=0,success=0,coins=0;std::vector<Row> rows;};
struct Projection {
    std::vector<WalletRow> wallet;
    std::map<std::string,GuildRow> guilds;
    std::map<std::string,std::set<std::string>> guildAccess;
    std::map<uint32_t,ItemFlags> flags;
    std::vector<Row> rows;
    std::map<uint32_t,ItemPresentation> metadata;
    std::vector<ScopeState> states;
    std::map<std::string,std::set<std::string>> rosters;
};
inline void Record(const std::string& root,const ScopeState& state,const std::vector<Slot>* rows=nullptr){
    std::lock_guard<std::mutex> lock(Cache::Mutex());
    const auto path=Storage::Paths(root).Cache();std::filesystem::create_directories(path.parent_path());
    Database::Connection db(path.u8string(),Database::Connection::OpenMode::Create);
    Cache::Initialize(db);Database::Transaction tx(db);
    auto old=db.Prepare("SELECT attempted FROM native_inventory_scopes WHERE account=? AND scope=? AND character=?");
    old.Bind(1,state.account);old.Bind(2,state.scope);old.Bind(3,state.character);
    if(old.Step()&&old.Integer(0)>state.attempted)return;
    auto mark=db.Prepare("INSERT INTO native_inventory_scopes VALUES(?,?,?,?,?,?) ON CONFLICT(account,scope,character) DO UPDATE SET attempted=excluded.attempted,status=excluded.status,success=CASE WHEN excluded.status='Ready' THEN excluded.success ELSE native_inventory_scopes.success END");
    mark.Bind(1,state.account);mark.Bind(2,state.scope);mark.Bind(3,state.character);mark.Bind(4,state.status);
    mark.Bind(5,state.attempted);mark.Bind(6,state.success);mark.Execute();
    if(rows){
        if(state.status!="Ready")throw std::runtime_error("Cannot publish incomplete inventory");
        auto del=db.Prepare("DELETE FROM native_inventory_slots WHERE account=? AND scope=? AND character=?");
        del.Bind(1,state.account);del.Bind(2,state.scope);del.Bind(3,state.character);del.Execute();
        auto put=db.Prepare("INSERT INTO native_inventory_slots VALUES(?,?,?,?,?,?,?,?,?,?)");
        auto socket=db.Prepare("INSERT INTO native_inventory_sockets VALUES(?,?,?,?,?,?,?)");
        for(const auto& s:*rows){
            if(s.account!=state.account||s.scope!=state.scope||s.character!=state.character)
                throw std::runtime_error("Inventory scope mismatch");
            put.Bind(1,s.account);put.Bind(2,s.scope);put.Bind(3,s.character);put.Bind(4,s.position);
            put.Bind(5,s.id);put.Bind(6,s.count);put.Bind(7,static_cast<int>(s.binding));put.Bind(8,s.boundTo);
            put.Bind(9,s.stat);put.Bind(10,s.charges);put.Execute();put.Reset();
            for(int kind=0;kind<2;++kind){
                const auto& values=kind?s.infusions:s.upgrades;
                for(size_t i=0;i<values.size();++i){
                    socket.Bind(1,s.account);socket.Bind(2,s.scope);socket.Bind(3,s.character);socket.Bind(4,s.position);
                    socket.Bind(5,kind);socket.Bind(6,static_cast<int64_t>(i));socket.Bind(7,values[i]);socket.Execute();socket.Reset();
                }
            }
        }
    }
    tx.Commit();
}
inline void StoreRoster(const std::string& root,const std::string& account,const std::set<std::string>& names,int64_t stamp){
    std::lock_guard<std::mutex> lock(Cache::Mutex());
    const auto path=Storage::Paths(root).Cache();std::filesystem::create_directories(path.parent_path());
    Database::Connection db(path.u8string(),Database::Connection::OpenMode::Create);
    Cache::Initialize(db);Database::Transaction tx(db);
    auto old=db.Prepare("SELECT fetched_at FROM native_roster_state WHERE account=?");
    old.Bind(1,account);if(old.Step()&&old.Integer(0)>stamp)return;
    auto mark=db.Prepare("INSERT INTO native_roster_state VALUES(?,?) ON CONFLICT(account) DO UPDATE SET fetched_at=excluded.fetched_at");
    mark.Bind(1,account);mark.Bind(2,stamp);mark.Execute();
    auto del=db.Prepare("DELETE FROM native_rosters WHERE account=?");del.Bind(1,account);del.Execute();
    auto put=db.Prepare("INSERT INTO native_rosters VALUES(?,?)");
    for(const auto& name:names){put.Bind(1,account);put.Bind(2,name);put.Execute();put.Reset();}
    // Remove only scopes of characters confirmed absent from this successful roster.
    auto prune=db.Prepare("DELETE FROM native_inventory_scopes WHERE account=? AND scope IN ('Bags','Equipment') AND character<>'' AND character NOT IN (SELECT character FROM native_rosters WHERE account=?)");
    prune.Bind(1,account);prune.Bind(2,account);prune.Execute();tx.Commit();
}
inline void StoreNames(const std::string& root,const Json& rows){
    if(!rows.is_array())return;
    std::lock_guard<std::mutex> lock(Cache::Mutex());
    const auto path=Storage::Paths(root).Cache();
    Database::Connection db(path.u8string(),Database::Connection::OpenMode::Existing);
    Cache::Initialize(db);Database::Transaction tx(db);
    auto flags=db.Prepare("INSERT INTO native_item_flags VALUES(?,?,?,?) ON CONFLICT(item_id) DO UPDATE SET account_bound=excluded.account_bound,soulbound=excluded.soulbound,bind_on_use=excluded.bind_on_use");
    auto q=db.Prepare("INSERT INTO reference_items(id,name,rarity,type,chat_link,icon,description,default_skin,details,complete) VALUES(?,?,?,?,?,?,?,?,?,1) ON CONFLICT(id) DO UPDATE SET name=excluded.name,rarity=excluded.rarity,type=excluded.type,chat_link=excluded.chat_link,icon=excluded.icon,description=excluded.description,default_skin=excluded.default_skin,details=excluded.details,complete=1 WHERE reference_items.complete=0");
    for(const auto& row:rows){
        q.Bind(1,ItemId(row.at("id")));q.Bind(2,row.at("name").get<std::string>());q.Bind(3,row.value("rarity",std::string{}));
        q.Bind(4,row.value("type",std::string{}));q.Bind(5,row.value("chat_link",std::string{}));q.Bind(6,row.value("icon",std::string{}));q.Bind(7,row.value("description",std::string{}));q.Bind(8,static_cast<int64_t>(row.value("default_skin",0u)));q.Bind(9,row.value("details",Json::object()).dump());q.Execute();q.Reset();
        if(row.contains("flags")){auto f=ParseFlags(row.at("flags"));flags.Bind(1,ItemId(row.at("id")));flags.Bind(2,int(f.accountBound));flags.Bind(3,int(f.soulbound));flags.Bind(4,int(f.bindOnUse));flags.Execute();flags.Reset();}
    }
    tx.Commit();
}
inline void RecordWallet(const std::string& root,const ScopeState& state,const std::vector<CurrencyBalance>* rows=nullptr){
    if(state.scope!="Wallet")throw std::runtime_error("Wrong wallet scope");
    std::lock_guard<std::mutex> lock(Cache::Mutex());auto path=Storage::Paths(root).Cache();std::filesystem::create_directories(path.parent_path());
    Database::Connection db(path.u8string(),Database::Connection::OpenMode::Create);Cache::Initialize(db);Database::Transaction tx(db);
    auto old=db.Prepare("SELECT attempted FROM native_inventory_scopes WHERE account=? AND scope='Wallet' AND character=''");
    old.Bind(1,state.account);if(old.Step()&&old.Integer(0)>state.attempted)return;
    auto mark=db.Prepare("INSERT INTO native_inventory_scopes VALUES(?,'Wallet','',?,?,?) ON CONFLICT(account,scope,character) DO UPDATE SET attempted=excluded.attempted,status=excluded.status,success=CASE WHEN excluded.status='Ready' THEN excluded.success ELSE native_inventory_scopes.success END");
    mark.Bind(1,state.account);mark.Bind(2,state.status);mark.Bind(3,state.attempted);mark.Bind(4,state.success);mark.Execute();
    if(rows){
        if(state.status!="Ready")throw std::runtime_error("Incomplete wallet");
        auto del=db.Prepare("DELETE FROM native_wallet WHERE account=?");del.Bind(1,state.account);del.Execute();
        auto put=db.Prepare("INSERT INTO native_wallet VALUES(?,?,?)");
        for(auto& r:*rows){put.Bind(1,state.account);put.Bind(2,r.id);put.Bind(3,r.value);put.Execute();put.Reset();}
    }tx.Commit();
}
inline void StoreCurrencies(const std::string& root,const Json& j){
    if(!j.is_array())throw std::runtime_error("Invalid currency definitions");
    std::lock_guard<std::mutex> lock(Cache::Mutex());Database::Connection db(Storage::Paths(root).Cache().u8string(),Database::Connection::OpenMode::Existing);
    Cache::Initialize(db);Database::Transaction tx(db);
    auto q=db.Prepare("INSERT INTO native_currencies VALUES(?,?,?,?) ON CONFLICT(id) DO UPDATE SET name=excluded.name,description=excluded.description,sort_order=excluded.sort_order");
    for(const auto& r:j){q.Bind(1,ItemId(r.at("id")));q.Bind(2,r.at("name").get<std::string>());q.Bind(3,r.value("description",std::string{}));q.Bind(4,r.value("order",0));q.Execute();q.Reset();}tx.Commit();
}
inline void StoreGuildAccess(const std::string& root,const std::string& account,const std::set<std::string>& ids,int64_t stamp){
    std::lock_guard<std::mutex> lock(Cache::Mutex());auto path=Storage::Paths(root).Cache();std::filesystem::create_directories(path.parent_path());
    Database::Connection db(path.u8string(),Database::Connection::OpenMode::Create);Cache::Initialize(db);Database::Transaction tx(db);
    auto old=db.Prepare("SELECT attempted FROM native_inventory_scopes WHERE account=? AND scope='Guild access' AND character=''");
    old.Bind(1,account);if(old.Step()&&old.Integer(0)>stamp)return;
    auto del=db.Prepare("DELETE FROM native_guild_access WHERE account=?");del.Bind(1,account);del.Execute();
    auto put=db.Prepare("INSERT INTO native_guild_access VALUES(?,?)");for(auto& id:ids){put.Bind(1,account);put.Bind(2,id);put.Execute();put.Reset();}
    auto mark=db.Prepare("INSERT INTO native_inventory_scopes VALUES(?,'Guild access','','Ready',?,?) ON CONFLICT(account,scope,character) DO UPDATE SET status='Ready',attempted=excluded.attempted,success=excluded.success");
    mark.Bind(1,account);mark.Bind(2,stamp);mark.Bind(3,stamp);mark.Execute();tx.Commit();
}
inline void RecordGuild(const std::string& root,const GuildRow& g,const GuildStock* stock=nullptr){
    std::lock_guard<std::mutex> lock(Cache::Mutex());auto path=Storage::Paths(root).Cache();std::filesystem::create_directories(path.parent_path());
    Database::Connection db(path.u8string(),Database::Connection::OpenMode::Create);Cache::Initialize(db);Database::Transaction tx(db);
    auto old=db.Prepare("SELECT attempted FROM native_guilds WHERE guild_id=?");old.Bind(1,g.id);if(old.Step()&&old.Integer(0)>g.attempted)return;
    auto mark=db.Prepare("INSERT INTO native_guilds VALUES(?,?,?,?,?,?) ON CONFLICT(guild_id) DO UPDATE SET name=CASE WHEN excluded.name<>'' THEN excluded.name ELSE native_guilds.name END,tag=CASE WHEN excluded.tag<>'' THEN excluded.tag ELSE native_guilds.tag END,status=excluded.status,attempted=excluded.attempted,success=CASE WHEN excluded.status='Ready' THEN excluded.success ELSE native_guilds.success END");
    mark.Bind(1,g.id);mark.Bind(2,g.name);mark.Bind(3,g.tag);mark.Bind(4,g.status);mark.Bind(5,g.attempted);mark.Bind(6,g.success);mark.Execute();
    if(stock){
        if(g.status!="Ready")throw std::runtime_error("Incomplete guild stock");
        auto del=db.Prepare("DELETE FROM native_guild_slots WHERE guild_id=?");del.Bind(1,g.id);del.Execute();
        auto d2=db.Prepare("DELETE FROM native_guild_coins WHERE guild_id=?");d2.Bind(1,g.id);d2.Execute();
        auto put=db.Prepare("INSERT INTO native_guild_slots VALUES(?,?,?,?)");
        for(auto& s:stock->slots){if(s.account!=g.id)throw std::runtime_error("Guild mismatch");put.Bind(1,g.id);put.Bind(2,s.position);put.Bind(3,s.id);put.Bind(4,s.count);put.Execute();put.Reset();}
        auto coins=db.Prepare("INSERT INTO native_guild_coins VALUES(?,?,?)");
        for(auto& [section,value]:stock->coins){coins.Bind(1,g.id);coins.Bind(2,section);coins.Bind(3,value);coins.Execute();coins.Reset();}
    }tx.Commit();
}
inline Projection Read(const std::string& root){
    Projection out;
    std::lock_guard<std::mutex> lock(Cache::Mutex());const auto path=Storage::Paths(root).Cache();
    if(!std::filesystem::exists(path))return out;
    Database::Connection db(path.u8string(),Database::Connection::OpenMode::Existing);
    Cache::Initialize(db);Database::Transaction tx(db);
    auto q=db.Prepare("SELECT account,scope,character,status,attempted,success FROM native_inventory_scopes ORDER BY account,scope,character");
    while(q.Step())out.states.push_back({q.Text(0),q.Text(1),q.Text(2),q.Text(3),q.Integer(4),q.Integer(5)});
    auto r=db.Prepare("SELECT s.account,s.scope,s.character,s.position,s.item_id,s.quantity,s.binding,s.bound_to,s.stat_id,s.charges,COALESCE(i.name,''),COALESCE(i.rarity,''),COALESCE(i.type,''),COALESCE(i.chat_link,''),m.success FROM native_inventory_slots s JOIN native_inventory_scopes m USING(account,scope,character) LEFT JOIN reference_items i ON i.id=s.item_id ORDER BY s.item_id,s.account,s.scope,s.character,s.position");
    while(r.Step()){
        Row row;auto& s=row.slot;s.account=r.Text(0);s.scope=r.Text(1);s.character=r.Text(2);s.position=r.Text(3);
        s.id=static_cast<uint32_t>(r.Integer(4));s.count=r.Integer(5);s.binding=static_cast<Binding>(r.Integer(6));
        s.boundTo=r.Text(7);s.stat=r.Integer(8);s.charges=r.Integer(9);
        row.name=r.Text(10);row.rarity=r.Text(11);row.type=r.Text(12);row.chat=r.Text(13);row.captured=r.Integer(14);
        out.rows.push_back(std::move(row));
    }
    auto metadata=db.Prepare("SELECT id,icon,description,details,complete FROM reference_items WHERE id IN (SELECT item_id FROM native_inventory_slots UNION SELECT item_id FROM native_guild_slots UNION SELECT item_id FROM native_inventory_sockets)");
    while(metadata.Step()){ItemPresentation p;p.icon=metadata.Text(1);p.description=metadata.Text(2);p.complete=metadata.Integer(4)!=0;if(!metadata.IsNull(3)){p.details=Json::parse(metadata.Text(3),nullptr,false);if(!p.details.is_object())p.details=Json::object();}out.metadata.emplace(static_cast<uint32_t>(metadata.Integer(0)),std::move(p));}
    auto sockets=db.Prepare("SELECT account,scope,character,position,kind,item_id FROM native_inventory_sockets ORDER BY account,scope,character,position,kind,ordinal");
    std::map<std::tuple<std::string,std::string,std::string,std::string>,size_t> slotIndex;
    for(size_t i=0;i<out.rows.size();++i){const auto& s=out.rows[i].slot;slotIndex.emplace(std::make_tuple(s.account,s.scope,s.character,s.position),i);}
    while(sockets.Step()){
        auto it=slotIndex.find(std::make_tuple(sockets.Text(0),sockets.Text(1),sockets.Text(2),sockets.Text(3)));
        if(it==slotIndex.end())throw std::runtime_error("Orphan inventory socket");
        auto& s=out.rows[it->second].slot;
        (sockets.Integer(4)?s.infusions:s.upgrades).push_back(static_cast<uint32_t>(sockets.Integer(5)));
    }
    auto rosterState=db.Prepare("SELECT account FROM native_roster_state");
    while(rosterState.Step())out.rosters[rosterState.Text(0)];
    auto roster=db.Prepare("SELECT account,character FROM native_rosters");
    while(roster.Step())out.rosters[roster.Text(0)].insert(roster.Text(1));
    auto wallet=db.Prepare("SELECT w.account,w.currency_id,w.value,COALESCE(c.name,''),COALESCE(c.description,''),COALESCE(c.sort_order,0) FROM native_wallet w LEFT JOIN native_currencies c ON w.currency_id=c.id ORDER BY c.sort_order,w.currency_id");
    while(wallet.Step())out.wallet.push_back({wallet.Text(0),wallet.Text(3),wallet.Text(4),static_cast<uint32_t>(wallet.Integer(1)),wallet.Integer(2),static_cast<int>(wallet.Integer(5))});
    auto access=db.Prepare("SELECT account,guild_id FROM native_guild_access");while(access.Step())out.guildAccess[access.Text(0)].insert(access.Text(1));
    auto guilds=db.Prepare("SELECT guild_id,name,tag,status,attempted,success FROM native_guilds");while(guilds.Step()){GuildRow g;g.id=guilds.Text(0);g.name=guilds.Text(1);g.tag=guilds.Text(2);g.status=guilds.Text(3);g.attempted=guilds.Integer(4);g.success=guilds.Integer(5);out.guilds.emplace(g.id,std::move(g));}
    auto guildItems=db.Prepare("SELECT g.guild_id,g.position,g.item_id,g.quantity,COALESCE(i.name,''),COALESCE(i.rarity,''),COALESCE(i.type,''),COALESCE(i.chat_link,'') FROM native_guild_slots g LEFT JOIN reference_items i ON g.item_id=i.id");while(guildItems.Step()){auto& g=out.guilds.at(guildItems.Text(0));Row row;row.slot.account=g.id;row.slot.scope="Guild bank";row.slot.position=guildItems.Text(1);row.slot.id=static_cast<uint32_t>(guildItems.Integer(2));row.slot.count=guildItems.Integer(3);row.name=guildItems.Text(4);row.rarity=guildItems.Text(5);row.type=guildItems.Text(6);row.chat=guildItems.Text(7);row.captured=g.success;g.rows.push_back(std::move(row));}
    auto coins=db.Prepare("SELECT guild_id,value FROM native_guild_coins");while(coins.Step())out.guilds.at(coins.Text(0)).coins+=coins.Integer(1);
    auto flags=db.Prepare("SELECT item_id,account_bound,soulbound,bind_on_use FROM native_item_flags");while(flags.Step())out.flags[static_cast<uint32_t>(flags.Integer(0))]={true,flags.Integer(1)!=0,flags.Integer(2)!=0,flags.Integer(3)!=0};
    tx.Commit();return out;
}
}
