#pragma once
#include <array>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string_view>

namespace AlterEgo::ApiActivity {
// Fixed labels only. Never retain URL strings, query values, character names,
// credentials, response bodies or account identifiers.
enum class Endpoint : size_t {
    Characters, Builds, Equipment, Bags, Crafting, Roster,
    AchProgress, Vault, AccountStorage, Wallet, Guild, AccountOther,
    AchDefinitions, Catalog, Commerce, ApiOther, RenderArtwork, WikiApi, WikiArtwork, WikiOther, Count
};
inline constexpr size_t Count=static_cast<size_t>(Endpoint::Count);
inline constexpr std::array<const char*,Count> Labels{{
    "Characters / core","Character builds","Character equipment","Character bags",
    "Character crafting","Roster","Achievement progress","Vault","Account storage",
    "Wallet","Guild","Other account","Achievement definitions","Public catalogs",
    "Trading Post","Other ArenaNet","ArenaNet render artwork","Wiki API","Wiki artwork","Other Wiki"
}};
inline bool Prefix(std::string_view s,std::string_view p){return s.substr(0,p.size())==p;}
inline bool WikiUrl(std::string_view u){return Prefix(u,"https://wiki.guildwars2.com/");}
inline bool RenderUrl(std::string_view u){return Prefix(u,"https://render.guildwars2.com/");}
inline Endpoint WikiEndpoint(std::string_view u){
    if(Prefix(u,"https://wiki.guildwars2.com/api.php"))return Endpoint::WikiApi;
    if(Prefix(u,"https://wiki.guildwars2.com/images/"))return Endpoint::WikiArtwork;
    return Endpoint::WikiOther;
}
inline Endpoint ApiEndpoint(std::string_view p){
    p=p.substr(0,p.find('?'));
    if(p=="/v2/characters")return Endpoint::Roster;
    if(Prefix(p,"/v2/characters/")){
        const auto tail=p.substr(15); const auto slash=tail.find('/');
        if(slash==std::string_view::npos)return Endpoint::Characters;
        const auto part=tail.substr(slash+1);
        if(part=="buildtabs"||part=="buildtabs/active")return Endpoint::Builds;
        if(part=="equipment"||Prefix(part,"equipmenttabs"))return Endpoint::Equipment;
        if(part=="inventory")return Endpoint::Bags;
        if(part=="crafting")return Endpoint::Crafting;
        return Endpoint::Characters;
    }
    if(p=="/v2/account/achievements")return Endpoint::AchProgress;
    if(Prefix(p,"/v2/account/wizardsvault")||Prefix(p,"/v2/wizardsvault"))return Endpoint::Vault;
    if(p=="/v2/account/wallet")return Endpoint::Wallet;
    if(Prefix(p,"/v2/guild/"))return Endpoint::Guild;
    if(p=="/v2/account/bank"||p=="/v2/account/inventory"||p=="/v2/account/materials"||
       p=="/v2/account/legendaryarmory"||p=="/v2/account/delivery"||p=="/v2/commerce/delivery")return Endpoint::AccountStorage;
    if(Prefix(p,"/v2/account")||Prefix(p,"/v2/tokeninfo"))return Endpoint::AccountOther;
    if(Prefix(p,"/v2/achievements"))return Endpoint::AchDefinitions;
    if(Prefix(p,"/v2/commerce"))return Endpoint::Commerce;
    if(Prefix(p,"/v2/items")||Prefix(p,"/v2/skins")||Prefix(p,"/v2/skills")||
       Prefix(p,"/v2/traits")||Prefix(p,"/v2/specializations")||Prefix(p,"/v2/professions")||
       Prefix(p,"/v2/colors")||Prefix(p,"/v2/itemstats")||Prefix(p,"/v2/pets")||
       Prefix(p,"/v2/legends"))return Endpoint::Catalog;
    return Endpoint::ApiOther;
}
inline constexpr size_t SourceCount=3;
inline size_t Source(Endpoint e){return e==Endpoint::RenderArtwork?2u:e>=Endpoint::WikiApi?1u:0u;}
enum class Reason : size_t { Unclassified, Background, Visible, Objective, Manual, Count };
inline constexpr size_t ReasonCount=static_cast<size_t>(Reason::Count);
inline Reason& CurrentReason(){static thread_local Reason reason=Reason::Unclassified;return reason;}
class ReasonScope {
    Reason previous_;
public:
    explicit ReasonScope(Reason reason):previous_(CurrentReason()){CurrentReason()=reason;}
    ~ReasonScope(){CurrentReason()=previous_;}
    ReasonScope(const ReasonScope&)=delete;
    ReasonScope& operator=(const ReasonScope&)=delete;
};
inline unsigned Priority(Reason r){
    return r==Reason::Manual?3u:r==Reason::Objective?2u:r==Reason::Background?0u:1u;
}
inline int64_t Now(){return std::chrono::duration_cast<std::chrono::seconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count();}
struct Totals {uint64_t started=0,success=0,failed=0,limited=0,active=0;};
struct Snapshot {
    std::array<Totals,Count> endpoints{};
    std::array<uint64_t,SourceCount> last10{},last60{},peak{};
    std::array<std::array<uint64_t,ReasonCount>,SourceCount> reasons{};
    uint64_t sharedHits=0,sharedCoalesced=0;
    uint64_t metadataOwned=0,metadataCoalesced=0,metadataCooldown=0,metadataManual=0;
};
class Recorder {
    struct Bucket {int64_t second=-1;std::array<uint64_t,SourceCount> starts{};};
    std::mutex mutex_;
    std::array<Bucket,3600> buckets_{};
    Snapshot session_{};
public:
    void Start(Endpoint e,int64_t second=Now(),Reason reason=CurrentReason()){
        std::lock_guard<std::mutex> lock(mutex_);
        auto& t=session_.endpoints[static_cast<size_t>(e)];++t.started;++t.active;
        auto& b=buckets_[static_cast<size_t>(second)%buckets_.size()];
        if(b.second!=second){b={};b.second=second;}
        auto& n=b.starts[Source(e)];++n;
        ++session_.reasons[Source(e)][static_cast<size_t>(reason)];
        auto& peak=session_.peak[Source(e)];peak=std::max(peak,n);
    }
    void Finish(Endpoint e,int status,bool complete){
        std::lock_guard<std::mutex> lock(mutex_);
        auto& t=session_.endpoints[static_cast<size_t>(e)];
        if(t.active)--t.active;
        if(complete&&status>=200&&status<300)++t.success;else ++t.failed;
        if(status==429)++t.limited;
    }
    void Shared(bool coalesced){
        std::lock_guard<std::mutex> lock(mutex_);
        if(coalesced)++session_.sharedCoalesced;else ++session_.sharedHits;
    }
    void MetadataClaim(uint64_t owned,uint64_t coalesced,uint64_t cooldown,bool manual){
        std::lock_guard<std::mutex> lock(mutex_);
        session_.metadataOwned+=owned;session_.metadataCoalesced+=coalesced;
        session_.metadataCooldown+=cooldown;if(manual)session_.metadataManual+=owned;
    }
    Snapshot Read(int64_t now=Now()){
        std::lock_guard<std::mutex> lock(mutex_);
        auto s=session_;
        for(const auto& b:buckets_){
            if(b.second<0||b.second>now||now-b.second>=3600)continue;
            for(size_t i=0;i<SourceCount;++i){
                s.last60[i]+=b.starts[i];
                if(now-b.second<600)s.last10[i]+=b.starts[i];
            }
        }
        return s;
    }
};
inline Recorder& Session(){static Recorder r;return r;}
// Create immediately before the actual WinINet request call, not before
// admission, handle setup or a local cache lookup.
class Attempt {
    Endpoint endpoint_;bool enabled_;int status_=0;bool complete_=false;
public:
    explicit Attempt(Endpoint endpoint,bool enabled=true):endpoint_(endpoint),enabled_(enabled){
        if(enabled_)Session().Start(endpoint_);
    }
    ~Attempt(){if(enabled_)Session().Finish(endpoint_,status_,complete_);}
    Attempt(const Attempt&)=delete;
    Attempt& operator=(const Attempt&)=delete;
    void Status(int s){status_=s;}
    void Complete(){complete_=true;}
};
}
