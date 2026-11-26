#pragma once
#include "WikiEventCache.h"
#include "PublicCatalog.h"
#include "HttpClient.h"
namespace AlterEgo::WikiEvents {
inline std::atomic<bool> busy{false};
inline std::mutex statusMutex;
inline std::string status;
inline void SetStatus(const std::string& s){std::lock_guard<std::mutex> lock(statusMutex);status=s;}
inline std::string Status(){std::lock_guard<std::mutex> lock(statusMutex);return status;}
inline void Load(const std::string& root) {
    Publish(Embedded());busy=false;SetStatus("");
    try{PublicCatalog::WithDb(root,[](auto& db){Publish(LoadBest(db));});}
    catch(...){SetStatus("SQLite unavailable; using embedded timers");}
}
inline Timers Fetch() {
    auto request=[](const char* url,bool wrapped){
        Background::Checkpoint();
        auto response=Skinventory::HttpClient::GetEx(url);
        Background::Checkpoint();
        if(response.status_code!=200)throw std::runtime_error("Wiki HTTP "+std::to_string(response.status_code));
        return ParseResponse(response.body,wrapped);
    };
    try{
        return request("https://wiki.guildwars2.com/api.php?action=parse&page=Widget%3AEvent_timer%2Fdata.json&prop=wikitext&format=json",true);
    }catch(const Background::Cancelled&){throw;}
    catch(...){
        return request("https://wiki.guildwars2.com/wiki/Widget:Event_timer/data.json?action=raw",false);
    }
}
inline void Refresh(const std::string& root,bool manual=false) {
    if(!AutoRefreshPolicy::AllowRequest(manual,AutoRefreshPolicy::Dataset::PublicCatalog)||
       !Due(*Get(),PublicCatalog::Now(),manual)||busy.exchange(true))return;
    SetStatus("Refreshing event timers...");
    Background::Workers().Launch([root]{
        const auto attempt=PublicCatalog::Now();
        Background::CompletedAttempt outcome([]{return Background::Workers().Stopping();},[&]{
            PublicCatalog::WithDb(root,[&](auto& db){
                if(Background::Workers().Stopping())return;
                PublicCatalog::MarkAttempt(db,AttemptDataset,attempt);
                auto existing=*Get();existing.attempt=PublicCatalog::Fresh(db,AttemptDataset);Publish(std::move(existing));
            });
        });
        auto timers=Fetch();
        Background::Checkpoint();
        PublicCatalog::WithDb(root,[&](auto& db){
            Snapshot next{std::move(timers),PublicCatalog::Now(),attempt,false};
            Background::Checkpoint();
            Database::Transaction tx(db);Store(db,next);PublicCatalog::MarkAttempt(db,AttemptDataset,attempt);tx.Commit();
            Publish(std::move(next));
        });
        outcome.Dismiss();SetStatus("Event timers up to date");busy=false;
    },[]{SetStatus(Background::Workers().Stopping()?"Event timer refresh cancelled":"Event timer refresh failed; previous snapshot retained");busy=false;});
}
} // namespace AlterEgo::WikiEvents
