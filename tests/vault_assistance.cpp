#include "../src/VaultAssistance.h"
#include <cassert>
#include <algorithm>
int case_vault_assistance(int, char**) {
    using namespace AlterEgo::VaultAssist;
    auto a = Match("Complete 3 Events","PvE");
    assert(std::any_of(a.begin(), a.end(), [](const auto& option) { return option.link == "[&BL8MAAA=]"; }));
    assert(std::any_of(a.begin(), a.end(), [](const auto& option) { return option.link == "[&BP8MAAA=]"; }));
    assert(Match("Complete 3 Events","WvW").empty());
    assert(Match("Salvage 10 Items","PvE").empty());
    assert(Match("Complete 10 Events in Queensdale","PvE").empty());
    assert(!Match("Gather 25 Crafting Resources","PvE").empty());
    a = Match("Complete 1 Event in Cantha","PvE");
    assert(!a.empty());
    for (const auto& option : a) assert(option.map != "The Silverwastes");
    assert(Match("Complete a Renown Heart in Janthir","PvE").empty());
    a = Match("Defeat the Inquest Golem Mark II World Boss or Complete 10 Events in Mount Maelstrom","PvE");
    assert(a.size() == 1);
    assert(NextSeconds(a[0], 119 * 60 + 30) == 30);
    assert(NextSeconds(a[0], 120 * 60) == 0);
    assert(NextSeconds(a[0], 120 * 60 + 1) == 179 * 60 + 59);
    a = Match("Complete a Meta-Event or Events in Cantha or 15 Events in Maguuma Jungle","PvE");
    assert(!a.empty());
    Sort(a, 119 * 60);
    assert(a[0].label == "Kaineng Blackout");
    assert(NextSeconds(a.back(),0) == -1);
    a = Match("Defeat Tequatl the Sunless or Complete Events in Sparkfly Fen","PvE");
    assert(NextSeconds(a[0], 23 * 3600) == 3600);
    a = Meta("castora");
    auto event = std::find_if(a.begin(),a.end(),[](const auto& d){return d.label=="Shackles of the Ancients";});
    assert(event!=a.end() && NextSeconds(*event,70*60)==5*60);
    assert(NextSeconds(a.back(),119*60)==60);
    namespace W=AlterEgo::WikiEvents;
    W::Json raw={{"events",W::Json::object()}};
    for(const auto& p:W::Phases){
        auto& group=raw["events"][p.group];
        if(group.is_null()){
            group={{"segments",W::Json::object()},{"sequences",{{"partial",W::Json::array()},{"pattern",W::Json::array()}}}};
        }
        const int id=static_cast<int>(group["segments"].size())+1;
        group["segments"][std::to_string(id)]={{"name",p.name}};
        group["sequences"]["pattern"].push_back({{"r",id},{"d",1}});
    }
    for(auto& group:raw["events"]){
        auto& pattern=group["sequences"]["pattern"];
        group["segments"]["0"]={{"name",""}};
        const int used=static_cast<int>(pattern.size());
        pattern.push_back({{"r",0},{"d",120-used}});
    }
    for(int i=0;raw["events"].size()<40;++i)raw["events"]["unused"+std::to_string(i)]=W::Json::object();
    raw["events"]["voe-eg"]["sequences"]["partial"]=W::Json::array({{{"r",0},{"d",75}}});
    auto parsed=W::Parse(raw);
    assert(parsed.at("Shackles of the Ancients")[0]==75);
    W::Json envelope={{"parse",{{"title","Widget:Event timer/data.json"},{"wikitext",{{"*",raw.dump()}}}}}};
    assert(W::ParseResponse(envelope.dump(),true)==parsed);
    assert(W::ParseResponse(raw.dump(),false)==parsed);
    auto modified=W::Embedded();modified.timers=parsed;modified.embedded=false;W::Publish(modified);
    a=Meta("castora");assert(NextSeconds(a[2],70*60)==5*60);
    W::Publish(W::Embedded());
    raw["events"]["voe-eg"]["sequences"]["pattern"][0]["d"]=0;
    bool rejected=false;try{(void)W::Parse(raw);}catch(...){rejected=true;}
    assert(rejected);
    assert(Countdown(61) == "in 2m");
    assert(Countdown(0) == "Scheduled now");
    assert(Countdown(-1) == "Variable start");
    return 0;
}
