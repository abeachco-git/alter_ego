#pragma once
#include "RefreshEngine.h"
#include "BackgroundWorkers.h"
#include <nlohmann/json.hpp>
#include <tuple>
#include <ctime>
namespace AlterEgo::CharacterActivity {
using Json=nlohmann::json;
using Key=std::pair<std::string,std::string>;
struct Scope {
    int64_t success=0,attempt=0,covered=0,specCovered=0;
};
struct Entry {
    int64_t lastSeen=0,login=0,specChanged=0;
    uint32_t spec=0;
    std::map<std::string,Scope> scopes;
};
struct Ticket {int64_t started=0,seen=0,login=0,specChanged=0;bool active=false;int64_t verificationStarted=0;};
class Tracker {
    std::map<Key,Entry> rows_;
    Key active_;
    int64_t missingSince_=0;
    bool dirty_=false;
    uint64_t revision_=0;
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
            if(oldAccount!=account)rows_[{account,{}}].login=now;
            Dirty();
        }else if(spec&&rows_[k].spec&&spec!=rows_[k].spec){
            rows_[k].spec=spec;rows_[k].specChanged=now;Dirty();
        }else if(spec&&!rows_[k].spec){rows_[k].spec=spec;Dirty();}
        Touch(k,now);Touch({account,{}},now);
    }
    void Finish(int64_t now){End(missingSince_?std::min(now,missingSince_):now);missingSince_=0;}
    Key ActiveKey()const{return active_;}
    bool SpecMatches(const Key& k,uint32_t spec)const{auto it=rows_.find(k);return it==rows_.end()||!it->second.spec||it->second.spec==spec;}
    int64_t LastSeen(const Key& k)const{auto i=rows_.find(k);return i==rows_.end()?0:i->second.lastSeen;}
    bool Fresh(const Key& k,const std::string& scope,int64_t success,int64_t now,bool specSignal=true)const{
        if(success<=0||success>now)return false;
        auto i=rows_.find(k);if(i==rows_.end())return true;
        const auto& e=i->second;auto it=e.scopes.find(scope);const Scope s=it==e.scopes.end()?Scope{}:it->second;
        if(Active(k)){
            const auto interval=k.second.empty()?RefreshEngine::BrowseSeconds:RefreshEngine::CharacterSeconds;
            const bool specDue=specSignal&&e.specChanged>s.specCovered&&e.specChanged>=e.login;
            return !specDue&&now-std::max(e.login,success)<interval;
        }
        return e.lastSeen<=Covered(e,s,success);
    }
    bool Due(const Key& k,const std::string& scope,int64_t success,int64_t externalAttempt,int64_t now,
             bool specSignal=true,bool manual=false)const{
        if(manual)return true;
        auto i=rows_.find(k);const Entry empty{};const auto& e=i==rows_.end()?empty:i->second;
        auto it=e.scopes.find(scope);const Scope s=it==e.scopes.end()?Scope{}:it->second;
        const auto attempt=std::max(externalAttempt,s.attempt);
        if(attempt>0&&attempt<=now&&attempt>=success&&now-attempt<RefreshEngine::FailureSeconds)return false;
        if(Active(k)){
            const bool changed=specSignal&&e.specChanged>s.specCovered&&e.specChanged>=e.login;
            if(changed&&now-e.specChanged>=8&&(s.success<e.specChanged||now-s.success>=RefreshEngine::CharacterSeconds))return true;
            const auto interval=k.second.empty()?RefreshEngine::BrowseSeconds:RefreshEngine::CharacterSeconds;
            if(success<=0||success>now){
                // First-time character demand is still subject to the login
                // propagation window. Explicit manual requests bypass it.
                // Account-level storage has its own initial-load behavior.
                return k.second.empty()||now-e.login>=interval;
            }
            return now-std::max(e.login,success)>=interval+RefreshEngine::Offset(k.first+"/"+k.second+"/"+scope);
        }
        if(!RefreshEngine::LogoutReady(e.lastSeen,now))return false;
        return success<=0||success>now||e.lastSeen>Covered(e,s,success);
    }
    Ticket Begin(const Key& k,const std::string& scope,int64_t now){
        auto& e=rows_[k];e.scopes[scope].attempt=now;Dirty();
        return {now,e.lastSeen,e.login,e.specChanged,Active(k)};
    }
    void Success(const Key& k,const std::string& scope,const Ticket& t,int64_t success,bool specVerified=true){
        auto& e=rows_[k];auto& s=e.scopes[scope];
        if(s.success>success||s.attempt>t.started)return;
        s.success=success;s.attempt=0;
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
        if(it->second.attempt==t.started){it->second.attempt=0;Dirty();}
    }
    bool DirtyState()const{return dirty_;}
    uint64_t Revision()const{return revision_;}
    void Saved(uint64_t rev){if(rev==revision_)dirty_=false;}
    Json Save()const{
        Json out=Json::array();
        for(const auto& [k,e]:rows_){
            Json scopes=Json::object();
            for(const auto& [name,s]:e.scopes)scopes[name]={{"success",s.success},{"attempt",s.attempt},{"covered",s.covered},{"spec_covered",s.specCovered}};
            out.push_back({{"account",k.first},{"character",k.second},{"last_seen",e.lastSeen},{"login",e.login},{"spec",e.spec},{"spec_changed",e.specChanged},{"scopes",scopes}});
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
            if(e.lastSeen<0||e.login<0||e.specChanged<0)throw std::runtime_error("Invalid activity timestamp");
            for(auto it=row.at("scopes").begin();it!=row.at("scopes").end();++it){
                Scope s{it.value().at("success").get<int64_t>(),it.value().at("attempt").get<int64_t>(),
                    it.value().at("covered").get<int64_t>(),it.value().value("spec_covered",int64_t(0))};
                if(s.success<0||s.attempt<0||s.covered<0||s.specCovered<0)throw std::runtime_error("Invalid activity scope");
                e.scopes[it.key()]=s;
            }
            next.rows_[k]=std::move(e);
        }
        *this=std::move(next); // no active identity survives restart
    }
};
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
