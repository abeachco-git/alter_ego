#pragma once
#include "CacheDatabase.h"
#include "NativeInventoryCore.h"
#include <map>
#include <tuple>
namespace AlterEgo::NativeInventory {
struct ScopeState {std::string account,scope,character,status;int64_t attempted=0,success=0;};
struct Row {Slot slot;std::string name,rarity,type,chat;int64_t captured=0;};
struct Projection {
    std::vector<Row> rows;
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
    auto prune=db.Prepare("DELETE FROM native_inventory_scopes WHERE account=? AND character<>'' AND character NOT IN (SELECT character FROM native_rosters WHERE account=?)");
    prune.Bind(1,account);prune.Bind(2,account);prune.Execute();tx.Commit();
}
inline void StoreNames(const std::string& root,const Json& rows){
    if(!rows.is_array())return;
    std::lock_guard<std::mutex> lock(Cache::Mutex());
    const auto path=Storage::Paths(root).Cache();
    Database::Connection db(path.u8string(),Database::Connection::OpenMode::Existing);
    Cache::Initialize(db);Database::Transaction tx(db);
    auto q=db.Prepare("INSERT INTO reference_items(id,name,rarity,type,chat_link) VALUES(?,?,?,?,?) ON CONFLICT(id) DO UPDATE SET name=excluded.name,rarity=excluded.rarity,type=excluded.type,chat_link=excluded.chat_link WHERE reference_items.complete=0");
    for(const auto& row:rows){
        q.Bind(1,ItemId(row.at("id")));q.Bind(2,row.at("name").get<std::string>());q.Bind(3,row.value("rarity",std::string{}));
        q.Bind(4,row.value("type",std::string{}));q.Bind(5,row.value("chat_link",std::string{}));q.Execute();q.Reset();
    }
    tx.Commit();
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
    auto sockets=db.Prepare("SELECT account,scope,character,position,kind,item_id FROM native_inventory_sockets ORDER BY account,scope,character,position,kind,ordinal");
    std::map<std::tuple<std::string,std::string,std::string,std::string>,size_t> slotIndex;
    for(size_t i=0;i<out.rows.size();++i){const auto& s=out.rows[i].slot;slotIndex.emplace(std::make_tuple(s.account,s.scope,s.character,s.position),i);}
    while(sockets.Step()){
        auto it=slotIndex.find(std::make_tuple(sockets.Text(0),sockets.Text(1),sockets.Text(2),sockets.Text(3)));
        if(it==slotIndex.end())throw std::runtime_error("Orphan inventory socket");
        auto& s=out.rows[it->second].slot;
        (sockets.Integer(4)?s.infusions:s.upgrades).push_back(static_cast<uint32_t>(sockets.Integer(5)));
    }
    auto roster=db.Prepare("SELECT account,character FROM native_rosters");
    while(roster.Step())out.rosters[roster.Text(0)].insert(roster.Text(1));
    tx.Commit();return out;
}
}
