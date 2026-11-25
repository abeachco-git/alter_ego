#pragma once
#include "WikiEventCore.h"
#include "PublicCatalogCache.h"
namespace AlterEgo::WikiEvents {
inline constexpr const char* Dataset="wiki_event_timers";
inline constexpr const char* AttemptDataset="wiki_event_timers_attempt";
inline Snapshot Read(Database::Connection& db) {
    Snapshot out;
    out.attempt=PublicCatalog::Fresh(db,AttemptDataset);
    out.verified=PublicCatalog::Fresh(db,Dataset);
    if(!out.verified)return out;
    if(out.verified<0||out.verified>PublicCatalog::Now()+300)throw std::runtime_error("Invalid Wiki verification time");
    auto q=db.Prepare("SELECT label,minute FROM wiki_event_starts ORDER BY label,minute");
    while(q.Step())out.timers[q.Text(0)].push_back(static_cast<int>(q.Integer(1)));
    Validate(out.timers);
    auto origin=db.Prepare("SELECT embedded FROM wiki_event_snapshot WHERE singleton=1");
    if(!origin.Step())throw std::runtime_error("Missing Wiki snapshot origin");
    out.embedded=origin.Integer(0)!=0;
    return out;
}
// Caller owns transaction and Cache::Mutex.
inline void Store(Database::Connection& db,const Snapshot& s) {
    Validate(s.timers);
    if(s.verified<=0||s.verified>PublicCatalog::Now()+300||
       s.verified<PublicCatalog::Fresh(db,Dataset))
        throw std::runtime_error("Invalid or older Wiki snapshot");
    db.Exec("DELETE FROM wiki_event_starts");
    auto put=db.Prepare("INSERT INTO wiki_event_starts VALUES(?,?)");
    for(const auto& [label,starts]:s.timers)for(auto minute:starts){
        put.Bind(1,label);put.Bind(2,minute);put.Execute();put.Reset();
    }
    auto origin=db.Prepare("INSERT INTO wiki_event_snapshot VALUES(1,?) ON CONFLICT(singleton) DO UPDATE SET embedded=excluded.embedded");
    origin.Bind(1,s.embedded?1:0);origin.Execute();
    PublicCatalog::Mark(db,Dataset,s.verified);
}
inline Snapshot LoadBest(Database::Connection& db) {
    const auto baseline=Embedded();
    Snapshot cached;
    try{cached=Read(db);}
    catch(...){
        auto safe=baseline;
        safe.attempt=PublicCatalog::Fresh(db,AttemptDataset);
        return safe;
    }
    auto chosen=Select(baseline,cached);
    if(!cached.verified||baseline.verified>cached.verified){
        Database::Transaction tx(db);Store(db,chosen);tx.Commit();
    }
    return chosen;
}
inline bool Due(const Snapshot& s,int64_t now,bool manual){
    return PublicCatalog::Due(s.verified,s.attempt,now,manual,RuntimeDays*86400LL);
}
} // namespace AlterEgo::WikiEvents
