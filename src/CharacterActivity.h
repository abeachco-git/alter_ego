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
};
struct Ticket {int64_t started=0,seen=0,login=0,specChanged=0;bool active=false;int64_t verificationStarted=0;uint64_t serial=0;};
using RefreshEngine::WaitReason;
using RefreshEngine::Eligibility;
struct Inspection { bool active=false, fresh=false; int64_t login=0,lastSeen=0,attempt=0,success=0; Eligibility eligibility; std::string error; };
class Tracker {
    std::map<Key,Entry> rows_;
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
    void Finish(int64_t now){End(missingSince_?std::min(now,missingSince_):now);missingSince_=0;}
    Key ActiveKey()const{return active_;}
    uint64_t LoginRevision()const{return loginRevision_;}
    int64_t LastLogin(const Key& k)const{auto i=rows_.find(k);return i==rows_.end()?0:i->second.login;}
    std::map<Key,int64_t> LoginTimes()const{
        std::map<Key,int64_t> out;for(const auto& [k,e]:rows_)if(!k.second.empty()&&e.login>0)out.emplace(k,e.login);return out;
    }
    // Historical login information is not evidence of logout or API activity.
    bool ImportLogin(const Key& k,int64_t stamp){
        if(k.first.empty()||k.second.empty()||stamp<=0||stamp<=LastLogin(k)||Active(k))return false;
        rows_[k].login=stamp;++loginRevision_;Dirty();return true;
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
            out.login=i->second.login;out.lastSeen=i->second.lastSeen;
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
        auto& e=rows_[k];auto& s=e.scopes[scope];
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
    // Name-keyed historical login records remain intact for Part 7C.
    // Only verification/attempt coverage belongs to this binding.
    void FenceBindings(const std::string& account,const CharacterIdentity::Bindings& bindings){
        for(auto& [key,e]:rows_){
            if(key.first!=account||key.second.empty())continue;
            const auto it=bindings.find(key.second);
            const auto id=it==bindings.end()?int64_t(0):it->second.id;
            const auto revision=it==bindings.end()?int64_t(0):it->second.revision;
            if(e.bindingId==id&&e.bindingRevision==revision&&id)continue;
            if(!e.scopes.empty()||e.bindingId!=id||e.bindingRevision!=revision){
                e.scopes.clear();e.bindingId=id;e.bindingRevision=revision;Dirty();
            }
        }
        for(const auto& [name,b]:bindings){
            auto& e=rows_[{account,name}];
            if(e.bindingId!=b.id||e.bindingRevision!=b.revision){
                e.scopes.clear();e.bindingId=b.id;e.bindingRevision=b.revision;Dirty();
            }
        }
    }
    bool DirtyState()const{return dirty_;}
    uint64_t Revision()const{return revision_;}
    void Saved(uint64_t rev){if(rev==revision_)dirty_=false;}
    Json Save()const{
        Json out=Json::array();
        for(const auto& [k,e]:rows_){
            Json scopes=Json::object();
            for(const auto& [name,s]:e.scopes)scopes[name]={{"success",s.success},{"attempt",s.attempt},{"covered",s.covered},{"spec_covered",s.specCovered}};
            out.push_back({{"account",k.first},{"character",k.second},{"last_seen",e.lastSeen},{"login",e.login},{"spec",e.spec},{"spec_changed",e.specChanged},{"scopes",scopes},{"binding_id",e.bindingId},{"binding_revision",e.bindingRevision}});
        }
        return {{"version",1},{"entries",out}};
    }
    void Load(const Json& j){
        Tracker next;
        if(j.at("version")!=1||!j.at("entries").is_array())throw std::runtime_error("Invalid activity data");
        for(const auto& row:j.at("entries")){
            Key k{row.at("account").get<std::string>(),row.at("character").get<std::string>()};
            if(k.first.empty()||next.rows_.count(k))throw std::runtime_error("Invalid activity identity");
            Entry e;e.lastSeen=row.at("last_seen").get<int64_t>();e.login=row.at("login").get<int64_t>();
            e.spec=row.value("spec",0u);e.specChanged=row.value("spec_changed",int64_t(0));
            e.bindingId=row.value("binding_id",int64_t(0));e.bindingRevision=row.value("binding_revision",int64_t(0));
            if(e.bindingId<0||e.bindingRevision<0)throw std::runtime_error("Invalid activity identity binding");
            if(e.lastSeen<0||e.login<0||e.specChanged<0)throw std::runtime_error("Invalid activity timestamp");
            for(auto it=row.at("scopes").begin();it!=row.at("scopes").end();++it){
                Scope s{it.value().at("success").get<int64_t>(),it.value().at("attempt").get<int64_t>(),
                    it.value().at("covered").get<int64_t>(),it.value().value("spec_covered",int64_t(0)),0,{}};
                if(s.success<0||s.attempt<0||s.covered<0||s.specCovered<0)throw std::runtime_error("Invalid activity scope");
                e.scopes[it.key()]=s;
            }
            next.rows_[k]=std::move(e);
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
