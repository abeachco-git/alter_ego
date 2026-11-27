#pragma once
#include "RefreshEngine.h"
#include "CharacterIdentityRuntime.h"
#include "BackgroundWorkers.h"
#include <nlohmann/json.hpp>
#include <tuple>
#include <ctime>
namespace AlterEgo::CharacterActivity {
using Json=nlohmann::json;
using Key=std::pair<std::string,std::string>;
struct PortableLogin {
    Key key;
    CharacterIdentity::Evidence evidence;
    bool operator<(const PortableLogin& o)const{
        return std::tie(key,evidence)<std::tie(o.key,o.evidence);
    }
    bool HasEvidence()const{return CharacterIdentity::Normalize(evidence).has_value();}
};
using PortableLogins=std::map<PortableLogin,int64_t>;
inline void MergePortableLogins(PortableLogins& to,const PortableLogins& from){
    for(const auto& [key,stamp]:from)to[key]=std::max(to[key],stamp);
}
inline Json EncodePortableLogins(const PortableLogins& logins){
    Json entries=Json::array();
    for(const auto& [p,stamp]:logins){
        if(!CharacterIdentity::Text(p.key.second)||stamp<=0)continue;
        Json row={{"account",p.key.first},{"character",p.key.second},{"login",stamp}};
        if(p.HasEvidence()){
            row["created"]=p.evidence.created;row["profession"]=p.evidence.profession;row["race"]=p.evidence.race;
        }
        entries.push_back(std::move(row));
    }
    return {{"format","alter_ego_character_logins"},{"version",2},{"entries",entries}};
}
inline PortableLogins DecodePortableLogins(const Json& j){
    const int version=j.at("version").get<int>();
    if(j.at("format")!="alter_ego_character_logins"||(version!=1&&version!=2)||!j.at("entries").is_array())
        throw std::runtime_error("Invalid login history");
    PortableLogins out;
    for(const auto& row:j.at("entries")){
        PortableLogin p{{row.at("account").get<std::string>(),row.at("character").get<std::string>()},{}};
        if(!CharacterIdentity::Text(p.key.second)||(!p.key.first.empty()&&!CharacterIdentity::Text(p.key.first))||
            !row.at("login").is_number_integer())throw std::runtime_error("Invalid login record");
        if(version==2&&(row.contains("created")||row.contains("profession")||row.contains("race"))){
            const auto e=CharacterIdentity::Normalize({row.at("created").get<std::string>(),row.at("profession").get<std::string>(),row.at("race").get<std::string>()});
            if(!e||p.key.first.empty())throw std::runtime_error("Invalid login evidence");
            p.evidence=*e;
        }
        const auto stamp=row.at("login").get<int64_t>();
        if(stamp<=0||!out.emplace(p,stamp).second)throw std::runtime_error("Invalid or duplicate login record");
    }
    return out;
}

struct Scope {
    int64_t success=0,attempt=0,covered=0,specCovered=0;
    uint64_t attemptSerial=0; // local only; distinct even within the same second
    std::string error; // session-local, never persisted
};
struct Entry {
    int64_t lastSeen=0,login=0,specChanged=0;
    uint32_t spec=0;
    std::map<std::string,Scope> scopes;
    int64_t bindingId=0,bindingRevision=0;
    CharacterIdentity::Evidence evidence;
    int64_t historicalLogin=0;
};
struct Ticket {int64_t started=0,seen=0,login=0,specChanged=0;bool active=false;int64_t verificationStarted=0;uint64_t serial=0;};
using RefreshEngine::WaitReason;
using RefreshEngine::Eligibility;
struct Inspection { bool active=false, fresh=false; int64_t login=0,lastSeen=0,attempt=0,success=0; Eligibility eligibility; std::string error; };
class Tracker {
    std::map<Key,Entry> rows_;
    // Address-keyed live projection; histories of unavailable characters
    // remain keyed by stable local identity and corroborating evidence.
    struct Detached {std::string name;Entry entry;};
    std::map<CharacterIdentity::Ref,Detached> detached_;
    std::map<Key,int64_t> unresolvedLogins_;
    PortableLogins pendingPortable_;
    Json legacyEntries_=Json::array();
    Key active_;
    int64_t missingSince_=0;
    bool dirty_=false;
    uint64_t revision_=0,nextTicket_=0,loginRevision_=0;
    void Dirty(){dirty_=true;++revision_;}
    bool Active(const Key& k)const{
        return !active_.first.empty()&&k.first==active_.first&&(k.second.empty()||k.second==active_.second);
    }
    void Touch(const Key& k,int64_t now){
        auto& e=rows_[k];if(now>e.lastSeen){e.lastSeen=now;Dirty();}
    }
    void End(int64_t now){
        if(active_.first.empty())return;
        Touch(active_,now);Touch({active_.first,{}},now);active_={};Dirty();
    }
    static int64_t Covered(const Entry& e,const Scope& s,int64_t success){
        if(!success)return 0;
        if(s.success==success)return s.covered;
        // A mismatched snapshot with an existing ledger is not a legacy
        // snapshot. Its request may have started before the logout grace or
        // failed to persist the verification watermark. Reconcile again.
        if(s.success||s.attempt)return 0;
        // A pre-migration snapshot is trustworthy for known activity only if
        // retrieved beyond its propagation window. Unknown history is not
        // evidence of change; retain cached rows until observed activity.
        return e.lastSeen>0&&success>=e.lastSeen+RefreshEngine::LogoutGraceSeconds?e.lastSeen:0;
    }
public:
    void Observe(const std::string& account,const std::string& name,uint32_t spec,bool gameplay,int64_t now){
        if(!gameplay||name.empty()||account.empty()){
            if(!missingSince_)missingSince_=now;
            if(now-missingSince_>=12&&!active_.first.empty())End(missingSince_);
            return;
        }
        // A long gap can end between observations (e.g. a paused render
        // callback). A resumed identical identity is still a new session.
        if(missingSince_&&now-missingSince_>=12)End(missingSince_);
        missingSince_=0;
        const Key k{account,name};
        if(k!=active_){
            const auto oldAccount=active_.first;
            if(!oldAccount.empty()){Touch(active_,now);Touch({oldAccount,{}},now);}
            active_=k;
            auto& e=rows_[k];e.login=now;e.spec=spec;e.specChanged=0;
            ++loginRevision_;
            if(oldAccount!=account)rows_[{account,{}}].login=now;
            Dirty();
        }else if(spec&&rows_[k].spec&&spec!=rows_[k].spec){
            rows_[k].spec=spec;rows_[k].specChanged=now;Dirty();
        }else if(spec&&!rows_[k].spec){rows_[k].spec=spec;Dirty();}
        Touch(k,now);Touch({account,{}},now);
    }
    void ObserveBound(const std::optional<CharacterIdentity::Address>& address,uint32_t spec,bool gameplay,int64_t now){
        if(address){
            const Key key{address->account,address->name};
            const auto it=rows_.find(key);
            // Reject a superseded view after acquiring the activity mutex.
            if(it!=rows_.end()&&it->second.bindingId==address->binding.id&&
               it->second.bindingRevision==address->binding.revision&&it->second.evidence==address->binding.evidence){
                Observe(key.first,key.second,spec,gameplay,now);return;
            }
        }
        Observe({},{},0,false,now);
    }
    void RetainUnresolvedLogin(const Key& k,int64_t stamp){
        if(k.second.empty()||stamp<=0)return;
        auto& prior=unresolvedLogins_[k];
        if(stamp>prior){prior=stamp;Dirty();}
    }
    void Finish(int64_t now){End(missingSince_?std::min(now,missingSince_):now);missingSince_=0;}
    Key ActiveKey()const{return active_;}
    uint64_t LoginRevision()const{return loginRevision_;}
    int64_t LastLogin(const Key& k)const{auto i=rows_.find(k);return i==rows_.end()?0:std::max(i->second.login,i->second.historicalLogin);}
    std::map<Key,int64_t> LoginTimes()const{
        std::map<Key,int64_t> out;for(const auto& [k,e]:rows_)if(!k.second.empty()&&std::max(e.login,e.historicalLogin)>0)out.emplace(k,std::max(e.login,e.historicalLogin));return out;
    }
    // A portable login is historical only. Preserve distinct owners even
    // when their last known name is the same.
    std::map<Key,int64_t> HistoricalLogins()const{
        auto out=LoginTimes();
        for(const auto& [ref,d]:detached_){
            const auto stamp=std::max(d.entry.login,d.entry.historicalLogin);
            if(stamp>0){const Key key{ref.account,d.name};out[key]=std::max(out[key],stamp);}
        }
        for(const auto& [key,stamp]:unresolvedLogins_)if(!key.first.empty())out[key]=std::max(out[key],stamp);
        return out;
    }
    PortableLogins PortableHistory()const{
        auto out=pendingPortable_;
        const auto add=[&](const Key& key,const Entry& e){
            const auto stamp=std::max(e.login,e.historicalLogin);
            if(key.second.empty()||stamp<=0)return;
            PortableLogin p{key,e.bindingId>0?e.evidence:CharacterIdentity::Evidence{}};
            out[p]=std::max(out[p],stamp);
        };
        for(const auto& [key,e]:rows_)add(key,e);
        for(const auto& [ref,d]:detached_)add({ref.account,d.name},d.entry);
        for(const auto& [key,stamp]:unresolvedLogins_){PortableLogin p{key,{}};out[p]=std::max(out[p],stamp);}
        return out;
    }
    void StagePortableHistory(const PortableLogins& logins){
        for(const auto& [p,stamp]:logins){
            if(!p.HasEvidence()){RetainUnresolvedLogin(p.key,stamp);continue;}
            auto& prior=pendingPortable_[p];if(stamp>prior){prior=stamp;Dirty();}
        }
    }
    void AdoptPortableHistory(const CharacterIdentity::State& state){
        for(auto it=pendingPortable_.begin();it!=pendingPortable_.end();){
            const auto& p=it->first;if(p.key.first!=state.account){++it;continue;}
            const CharacterIdentity::Record* match=nullptr;bool ambiguous=false;
            for(const auto& [id,r]:state.records)if(r.evidence==p.evidence){if(match){ambiguous=true;break;}match=&r;}
            if(ambiguous||!match||!match->present||state.Current(match->name)!=match){++it;continue;}
            const auto row=rows_.find({state.account,match->name});
            if(row==rows_.end()||row->second.bindingId!=match->id||row->second.bindingRevision!=match->bindingRevision||!(row->second.evidence==match->evidence)){++it;continue;}
            if(it->second>row->second.historicalLogin){row->second.historicalLogin=it->second;++loginRevision_;}
            it=pendingPortable_.erase(it);Dirty();
        }
    }
    // Historical login information is not evidence of logout or API activity.
    bool ImportLogin(const Key& k,int64_t stamp){
        if(k.first.empty()||k.second.empty()||stamp<=0||stamp<=LastLogin(k)||Active(k))return false;
        rows_[k].historicalLogin=stamp;++loginRevision_;Dirty();return true;
    }
    bool SpecMatches(const Key& k,uint32_t spec)const{auto it=rows_.find(k);return it==rows_.end()||!it->second.spec||it->second.spec==spec;}
    int64_t LastSeen(const Key& k)const{auto i=rows_.find(k);return i==rows_.end()?0:i->second.lastSeen;}
    bool Fresh(const Key& k,const std::string& scope,int64_t success,int64_t now,bool specSignal=true)const{
        if(success<=0||success>now)return false;
        auto i=rows_.find(k);if(i==rows_.end())return true;
        const auto& e=i->second;auto it=e.scopes.find(scope);const Scope s=it==e.scopes.end()?Scope{}:it->second;
        if(Active(k)){
            const auto interval=RefreshEngine::ActivitySeconds(k.second.empty(),scope);
            const bool specDue=specSignal&&e.specChanged>s.specCovered&&e.specChanged>=e.login;
            return !specDue&&now-std::max(e.login,success)<interval;
        }
        return e.lastSeen<=Covered(e,s,success);
    }
    Eligibility Eligible(const Key& k,const std::string& scope,int64_t success,int64_t externalAttempt,int64_t now,
                         bool specSignal=true)const{
        auto i=rows_.find(k);const Entry empty{};const auto& e=i==rows_.end()?empty:i->second;
        auto it=e.scopes.find(scope);const Scope s=it==e.scopes.end()?Scope{}:it->second;
        const auto attempt=std::max(externalAttempt,s.attempt);
        Eligibility result;
        if(Active(k)){
            const bool missing=success<=0||success>now;
            const auto interval=RefreshEngine::ActivitySeconds(k.second.empty(),scope);
            const auto base=missing?e.login:std::max(e.login,success);
            const auto normalAt=missing&&k.second.empty()?now:base+interval+
                (missing?0:RefreshEngine::Offset(k.first+"/"+k.second+"/"+scope));
            result={missing||base==e.login?WaitReason::LoginGrace:WaitReason::ActiveInterval,normalAt,now>=normalAt};
            if(!missing&&now>=base+interval&&now<normalAt)result.reason=WaitReason::Stagger;
            const bool changed=specSignal&&e.specChanged>s.specCovered&&e.specChanged>=e.login;
            if(changed){
                const auto specAt=std::max(e.specChanged+RefreshEngine::SpecializationDebounceSeconds,s.success<e.specChanged?int64_t(0):s.success+RefreshEngine::CharacterSeconds);
                if(specAt<normalAt)result={WaitReason::SpecDebounce,specAt,now>=specAt};
            }
        }else if(success<=0||success>now||e.lastSeen>Covered(e,s,success)){
            const auto at=e.lastSeen>0?e.lastSeen+RefreshEngine::LogoutGraceSeconds:now;
            result={WaitReason::LogoutGrace,at,now>=at};
        }
        if(result.reason==WaitReason::Current)return result;
        if(const auto retryAt=RefreshEngine::RetryUntil(success,attempt,now)){
            if(retryAt>=result.at)return {WaitReason::Retry,retryAt,false};
            // A longer propagation or activity interval remains the binding constraint.
            result.due=false;
        }
        if(result.due)result.reason=WaitReason::Due;
        return result;
    }
    bool Due(const Key& k,const std::string& scope,int64_t success,int64_t externalAttempt,int64_t now,
             bool specSignal=true,bool manual=false)const{
        return manual||Eligible(k,scope,success,externalAttempt,now,specSignal).due;
    }
    Inspection Inspect(const Key& k,const std::string& scope,int64_t success,int64_t now,bool specSignal=true,int64_t externalAttempt=0)const{
        Inspection out;out.active=Active(k);out.fresh=Fresh(k,scope,success,now,specSignal);out.success=success;
        out.eligibility=Eligible(k,scope,success,externalAttempt,now,specSignal);out.attempt=externalAttempt;
        auto i=rows_.find(k);if(i!=rows_.end()){
            out.login=std::max(i->second.login,i->second.historicalLogin);out.lastSeen=i->second.lastSeen;
            auto s=i->second.scopes.find(scope);
            if(s!=i->second.scopes.end()){out.attempt=std::max(externalAttempt,s->second.attempt);out.error=s->second.error;}
        }
        if(out.attempt<success)out.error.clear();
        return out;
    }
    void Failure(const Key& k,const std::string& scope,const Ticket& t,const std::string& message){
        auto row=rows_.find(k);if(row==rows_.end())return;
        auto it=row->second.scopes.find(scope);if(it==row->second.scopes.end())return;
        if(it->second.attemptSerial==t.serial&&it->second.attempt==t.started)it->second.error=message;
    }
    Ticket Begin(const Key& k,const std::string& scope,int64_t now){
        auto& e=rows_[k];auto& s=e.scopes[scope];s.attempt=now;s.attemptSerial=++nextTicket_;s.error.clear();Dirty();
        return {now,e.lastSeen,e.login,e.specChanged,Active(k),0,s.attemptSerial};
    }
    void Success(const Key& k,const std::string& scope,const Ticket& t,int64_t success,bool specVerified=true){
        auto row=rows_.find(k);if(row==rows_.end()||!t.serial)return;
        auto scopeRow=row->second.scopes.find(scope);if(scopeRow==row->second.scopes.end())return;
        auto& e=row->second;auto& s=scopeRow->second;
        if(s.success>success||s.attempt>t.started||s.attemptSerial!=t.serial)return;
        s.success=success;s.attempt=0;s.attemptSerial=0;s.error.clear();
        const auto verifiedStart=t.verificationStarted>0?std::min(t.started,t.verificationStarted):t.started;
        // Only an offline, post-grace request that did not overlap newer
        // activity covers the logout. Early manual pulls remain useful but dirty.
        if(!t.active&&!Active(k)&&e.lastSeen==t.seen&&e.login==t.login&&
           RefreshEngine::CanReconcileLogout(t.seen,verifiedStart))s.covered=std::max(s.covered,t.seen);
        if(specVerified&&e.specChanged==t.specChanged&&verifiedStart>=t.specChanged)s.specCovered=t.specChanged;
        Dirty();
    }
    void Cancel(const Key& k,const std::string& scope,const Ticket& t){
        auto row=rows_.find(k);if(row==rows_.end())return;
        auto it=row->second.scopes.find(scope);if(it==row->second.scopes.end())return;
        // Only the owning attempt may clear its own cooldown. Never undo
        // verified success or the coverage watermark.
        if(it->second.attempt==t.started&&it->second.attemptSerial==t.serial){
            it->second.attempt=0;it->second.attemptSerial=0;it->second.error.clear();Dirty();
        }
    }
    // Stage all old owners before claiming new addresses; swaps are
    // order-independent. Verification never crosses a changed binding.
    void FenceBindings(const std::string& account,const CharacterIdentity::Bindings& bindings){
        CharacterIdentity::Id activeId=0;
        CharacterIdentity::Evidence activeEvidence;
        bool activeDetached=false,changed=false;
        for(auto it=rows_.begin();it!=rows_.end();){
            const auto& key=it->first;auto& entry=it->second;
            if(key.first!=account||key.second.empty()){++it;continue;}
            const auto b=bindings.find(key.second);
            if(b!=bindings.end()&&entry.bindingId==b->second.id&&entry.bindingRevision==b->second.revision&&entry.evidence==b->second.evidence){++it;continue;}
            if(active_==key){activeId=entry.bindingId;activeEvidence=entry.evidence;activeDetached=true;}
            if(entry.bindingId>0&&CharacterIdentity::Normalize(entry.evidence)){
                entry.scopes.clear();
                detached_[{account,entry.bindingId}]={key.second,std::move(entry)};
            }else RetainUnresolvedLogin(key,std::max(entry.login,entry.historicalLogin));
            it=rows_.erase(it);changed=true;
        }
        if(activeDetached)active_={};
        for(const auto& [name,b]:bindings){
            const Key key{account,name};
            if(rows_.count(key))continue;
            Entry entry;
            const auto prior=detached_.find({account,b.id});
            if(prior!=detached_.end()&&prior->second.entry.evidence==b.evidence){
                entry=std::move(prior->second.entry);detached_.erase(prior);
            }
            entry.scopes.clear();entry.bindingId=b.id;entry.bindingRevision=b.revision;entry.evidence=b.evidence;
            rows_.emplace(key,std::move(entry));changed=true;
            if(activeDetached&&activeId==b.id&&activeEvidence==b.evidence)active_=key;
        }
        if(changed){++loginRevision_;Dirty();}
    }
    bool DirtyState()const{return dirty_;}
    uint64_t Revision()const{return revision_;}
    void Saved(uint64_t rev){if(rev==revision_)dirty_=false;}
    Json Save()const{
        Json out=Json::array();
        for(const auto& [k,e]:rows_){
            Json scopes=Json::object();
            for(const auto& [name,s]:e.scopes)scopes[name]={{"success",s.success},{"attempt",s.attempt},{"covered",s.covered},{"spec_covered",s.specCovered}};
            out.push_back({{"account",k.first},{"character",k.second},{"last_seen",e.lastSeen},{"login",e.login},{"spec",e.spec},{"spec_changed",e.specChanged},{"scopes",scopes},{"binding_id",e.bindingId},{"binding_revision",e.bindingRevision},{"created",e.evidence.created},{"profession",e.evidence.profession},{"race",e.evidence.race},{"historical_login",e.historicalLogin}});
        }
        Json detached=Json::array(),unresolved=Json::array();
        for(const auto& [ref,d]:detached_)detached.push_back({{"account",ref.account},{"character",d.name},
            {"binding_id",ref.id},{"binding_revision",d.entry.bindingRevision},{"last_seen",d.entry.lastSeen},
            {"login",d.entry.login},{"spec",d.entry.spec},{"spec_changed",d.entry.specChanged},
            {"created",d.entry.evidence.created},{"profession",d.entry.evidence.profession},{"race",d.entry.evidence.race},
            {"historical_login",d.entry.historicalLogin}});
        for(const auto& [k,stamp]:unresolvedLogins_)unresolved.push_back({{"account",k.first},{"character",k.second},{"login",stamp}});
        return {{"version",2},{"entries",out},{"detached",detached},{"unresolved_logins",unresolved},{"legacy_entries",legacyEntries_},{"pending_portable",EncodePortableLogins(pendingPortable_)}};
    }
    void Load(const Json& j,bool isolateLegacy=false){
        Tracker next;
        const auto version=j.at("version").get<int>();
        if((version!=1&&version!=2)||!j.at("entries").is_array())throw std::runtime_error("Invalid activity data");
        for(const auto& row:j.at("entries")){
            Key k{row.at("account").get<std::string>(),row.at("character").get<std::string>()};
            if(k.first.empty()||next.rows_.count(k))throw std::runtime_error("Invalid activity identity");
            Entry e;e.lastSeen=row.at("last_seen").get<int64_t>();e.login=row.at("login").get<int64_t>();
            e.spec=row.value("spec",0u);e.specChanged=row.value("spec_changed",int64_t(0));
            e.bindingId=row.value("binding_id",int64_t(0));e.bindingRevision=row.value("binding_revision",int64_t(0));
            e.historicalLogin=row.value("historical_login",int64_t(0));
            e.evidence={row.value("created",std::string{}),row.value("profession",std::string{}),row.value("race",std::string{})};
            if(e.bindingId<0||e.bindingRevision<0||e.historicalLogin<0||
               (version==2&&e.bindingId>0&&!CharacterIdentity::Normalize(e.evidence)))
                throw std::runtime_error("Invalid activity identity binding");
            if(e.lastSeen<0||e.login<0||e.specChanged<0)throw std::runtime_error("Invalid activity timestamp");
            for(auto it=row.at("scopes").begin();it!=row.at("scopes").end();++it){
                Scope s{it.value().at("success").get<int64_t>(),it.value().at("attempt").get<int64_t>(),
                    it.value().at("covered").get<int64_t>(),it.value().value("spec_covered",int64_t(0)),0,{}};
                if(s.success<0||s.attempt<0||s.covered<0||s.specCovered<0)throw std::runtime_error("Invalid activity scope");
                e.scopes[it.key()]=s;
            }
            // v1 binding IDs were overwritten without fencing activity.
            // Keep the original row, not just the historical login.
            if(version==1&&isolateLegacy&&!k.second.empty()){
                next.legacyEntries_.push_back(row);
                next.RetainUnresolvedLogin(k,e.login);next.Dirty();
            }else next.rows_[k]=std::move(e);
        }
        if(version==2){
            next.legacyEntries_=j.at("legacy_entries");
            if(!next.legacyEntries_.is_array()||!j.at("detached").is_array()||!j.at("unresolved_logins").is_array())
                throw std::runtime_error("Invalid activity archives");
            for(const auto& row:j.at("detached")){
                CharacterIdentity::Ref ref{row.at("account").get<std::string>(),row.at("binding_id").get<int64_t>()};
                Entry e;e.bindingId=ref.id;e.bindingRevision=row.at("binding_revision").get<int64_t>();
                e.lastSeen=row.at("last_seen").get<int64_t>();e.login=row.at("login").get<int64_t>();
                e.spec=row.at("spec").get<uint32_t>();e.specChanged=row.at("spec_changed").get<int64_t>();
                e.historicalLogin=row.value("historical_login",int64_t(0));
                e.evidence={row.at("created").get<std::string>(),row.at("profession").get<std::string>(),row.at("race").get<std::string>()};
                const auto name=row.at("character").get<std::string>();
                if(!ref.Valid()||name.empty()||e.bindingRevision<0||e.lastSeen<0||e.login<0||e.specChanged<0||e.historicalLogin<0||
                   !CharacterIdentity::Normalize(e.evidence)||!next.detached_.emplace(ref,Detached{name,std::move(e)}).second)
                    throw std::runtime_error("Invalid detached activity");
            }
            if(j.contains("pending_portable"))next.pendingPortable_=DecodePortableLogins(j.at("pending_portable"));
            for(const auto& row:j.at("unresolved_logins")){
                Key k{row.at("account").get<std::string>(),row.at("character").get<std::string>()};
                const auto stamp=row.at("login").get<int64_t>();
                if(k.second.empty()||stamp<=0||!next.unresolvedLogins_.emplace(k,stamp).second)
                    throw std::runtime_error("Invalid unresolved login");
            }
        }
        next.loginRevision_=loginRevision_+1;
        *this=std::move(next); // no active identity survives restart
    }
};
// Portable backups contain only historical logins, never API verification or
// request state. A restored history cannot make cached data appear fresh.
inline Json EncodeLoginHistory(const std::map<Key,int64_t>& logins){
    Json entries=Json::array();
    for(const auto& [k,stamp]:logins)
        if(!k.first.empty()&&!k.second.empty()&&stamp>0)
            entries.push_back({{"account",k.first},{"character",k.second},{"login",stamp}});
    return {{"format","alter_ego_character_logins"},{"version",1},{"entries",entries}};
}
inline std::map<Key,int64_t> DecodeLoginHistory(const Json& j){
    if(j.at("format")!="alter_ego_character_logins"||j.at("version")!=1||!j.at("entries").is_array())
        throw std::runtime_error("Invalid login history");
    std::map<Key,int64_t> out;
    for(const auto& row:j.at("entries")){
        Key k{row.at("account").get<std::string>(),row.at("character").get<std::string>()};
        if(!row.at("login").is_number_integer())throw std::runtime_error("Invalid login timestamp");
        auto stamp=row.at("login").get<int64_t>();
        if(k.first.empty()||k.second.empty()||stamp<=0||!out.emplace(k,stamp).second)
            throw std::runtime_error("Invalid or duplicate login identity");
    }
    return out;
}
inline void MergeLoginHistory(std::map<Key,int64_t>& target,const std::map<Key,int64_t>& incoming){
    for(const auto& [key,stamp]:incoming)target[key]=std::max(target[key],stamp);
}
// Keep unknown timestamps last regardless of direction; newest first is the default.
inline bool LoginBefore(int64_t a,int64_t b,bool newestFirst){
    if(a<=0||b<=0)return a>0&&b<=0;
    return newestFirst?a>b:a<b;
}
inline std::atomic<bool>& SpecSignal(){static std::atomic<bool> enabled{true};return enabled;}
inline Tracker& State(){static Tracker state;return state;}
inline std::mutex& Mutex(){static std::mutex mutex;return mutex;}
inline int64_t Now(){return static_cast<int64_t>(std::time(nullptr));}
inline void Observe(const std::string& account,const std::string& name,uint32_t spec,bool gameplay,int64_t now=Now()){
    std::lock_guard<std::mutex> lock(Mutex());State().Observe(account,name,spec,gameplay,now);
}
inline Key ActiveKey(){std::lock_guard<std::mutex> lock(Mutex());return State().ActiveKey();}
inline bool Fresh(const Key& k,const std::string& scope,int64_t success,bool specSignal=true){
    std::lock_guard<std::mutex> lock(Mutex());return State().Fresh(k,scope,success,Now(),specSignal);
}
inline bool Due(const Key& k,const std::string& scope,int64_t success,int64_t attempt=0,bool specSignal=true){
    std::lock_guard<std::mutex> lock(Mutex());return State().Due(k,scope,success,attempt,Now(),specSignal);
}
inline Ticket Begin(const Key& k,const std::string& scope){
    std::lock_guard<std::mutex> lock(Mutex());return State().Begin(k,scope,Now());
}
inline void Cancel(const Key& k,const std::string& scope,const Ticket& t){
    std::lock_guard<std::mutex> lock(Mutex());State().Cancel(k,scope,t);
}
class CancelOnShutdown {
    Key key_;
    std::vector<std::pair<std::string,Ticket>> tickets_;
public:
    explicit CancelOnShutdown(Key key):key_(std::move(key)){}
    Ticket BeginScope(const std::string& scope){
        auto ticket=Begin(key_,scope);tickets_.emplace_back(scope,ticket);return ticket;
    }
    ~CancelOnShutdown(){
        if(Background::Workers().Stopping())
            for(const auto& [scope,ticket]:tickets_)Cancel(key_,scope,ticket);
    }
};
inline void Success(const Key& k,const std::string& scope,const Ticket& t,int64_t stamp,bool specVerified=true){
    std::lock_guard<std::mutex> lock(Mutex());State().Success(k,scope,t,stamp,specVerified);
}
}
