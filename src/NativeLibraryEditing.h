#pragma once
#include "NativeLibrary.h"
#include <unordered_map>

namespace AlterEgo::NativeLibrary {
using Composable::Json;

// Index a single staged snapshot for candidate and usage lookups. Candidate
// equality is verified independently of the fingerprint. The index never
// owns component data or becomes a second source of truth.
class EditIndex {
    const Composable::Document& doc_;
    std::map<std::string,std::unordered_map<std::string,std::vector<std::string>>> hashes_;
    std::map<std::pair<std::string,std::string>,std::vector<std::string>> users_;
public:
    explicit EditIndex(const Composable::Document& doc):doc_(doc) {
        for(const auto& [kind,parts]:doc.components)
            for(const auto& part:parts) hashes_[kind][ExactFingerprint(part.data)].push_back(part.id);
        for(const auto& l:doc.loadouts)
            for(const auto& [kind,id]:l.refs)users_[{kind,id}].push_back(l.id);
    }
    const std::vector<std::string>& Users(const std::string& kind,const std::string& id)const {
        static const std::vector<std::string> empty;
        auto i=users_.find({kind,id});return i==users_.end()?empty:i->second;
    }
    std::vector<Match> Matches(const std::string& kind,const Json& data,
                              const std::string& profession={})const {
        std::vector<Match> out;
        const auto group=hashes_.find(kind);
        if(group==hashes_.end())return out;
        const auto bucket=group->second.find(ExactFingerprint(data));
        if(bucket==group->second.end())return out;
        for(const auto& id:bucket->second) {
            const auto* part=Composable::Find(doc_,kind,id);
            if(!part||!ExactContent(part->data,data))continue;
            const auto& users=Users(kind,id);
            // Do not share profession-sensitive skills across professions.
            if(kind=="skills") {
                bool compatible=!profession.empty()&&!users.empty();
                for(const auto& owner:users) {
                    auto l=std::find_if(doc_.loadouts.begin(),doc_.loadouts.end(),
                        [&](const auto& item){return item.id==owner;});
                    if(l==doc_.loadouts.end()||Composable::ProfessionOf(doc_,*l)!=profession)compatible=false;
                }
                if(!compatible)continue;
            }
            out.push_back({id,part->name,static_cast<std::int64_t>(users.size())});
        }
        return out;
    }
};

// Explicit consolidation only: exact saved payload, never semantic similarity.
inline bool ConsolidateExact(Composable::Document& doc,const std::string& kind,
    const std::string& source,const std::string& survivor,std::string& error) {
    const auto* a=Composable::Find(doc,kind,source);
    const auto* b=Composable::Find(doc,kind,survivor);
    if(source==survivor||!a||!b||!ExactContent(a->data,b->data))
        return Composable::Fail(error,"Components are not exact duplicates.");
    if(kind=="skills") {
        std::string profession;
        bool hasSource=false,hasSurvivor=false;
        for(const auto& l:doc.loadouts) {
            auto ref=l.refs.find(kind);
            if(ref==l.refs.end()||(ref->second!=source&&ref->second!=survivor))continue;
            auto p=Composable::ProfessionOf(doc,l);
            if(p.empty()||(!profession.empty()&&p!=profession))
                return Composable::Fail(error,"Skills profession is ambiguous.");
            profession=p;
            hasSource|=ref->second==source;hasSurvivor|=ref->second==survivor;
        }
        if(!hasSource||!hasSurvivor)
            return Composable::Fail(error,"Skills profession is unverified.");
    }
    return Composable::Transaction(doc,[&](Composable::Document& next,std::string& err) {
        for(auto& l:next.loadouts) {
            auto ref=l.refs.find(kind);
            if(ref!=l.refs.end()&&ref->second==source)ref->second=survivor;
        }
        return Composable::RemoveUnreferenced(next,kind,source,err);
    });
}

// Overlay only changed fields from a typed aggregate editor. Untouched
// defaults do not erase missing values or unknown imported extensions.
inline Json PatchDraft(const Json& stored,const Json& baseline,const Json& edited) {
    if(ExactContent(baseline,edited))return stored;
    if(baseline.is_object()&&edited.is_object()&&stored.is_object()) {
        Json result=stored;
        for(auto i=baseline.begin();i!=baseline.end();++i)
            if(!edited.contains(i.key()))result.erase(i.key());
        for(auto i=edited.begin();i!=edited.end();++i) {
            const auto old=baseline.find(i.key());
            if(old==baseline.end())result[i.key()]=i.value();
            else if(!ExactContent(*old,i.value())) {
                const auto existing=stored.find(i.key());
                result[i.key()]=existing==stored.end()?i.value():PatchDraft(*existing,*old,i.value());
            }
        }
        return result;
    }
    if(stored.is_array()&&baseline.is_array()&&edited.is_array()&&
       baseline.size()==edited.size()&&stored.size()==baseline.size()) {
        Json result=stored;
        for(size_t i=0;i<edited.size();++i)
            if(!ExactContent(baseline[i],edited[i]))result[i]=PatchDraft(stored[i],baseline[i],edited[i]);
        return result;
    }
    return edited;
}

// New or edited complete loadout. Preserve unchanged identities; exact
// reuse wins over modifying existing records; shared records are copied.
// Caller owns a disposable transaction draft; no live state is mutated.
inline bool PutLoadout(Composable::Document& doc,const Json& raw,std::string& error,
                       const Composable::ComponentNameLookup& lookup={}) {
    if(!raw.is_object()||!raw.contains("id")||!raw.at("id").is_string()||
       raw.at("id").get<std::string>().empty())
        return Composable::Fail(error,"Loadout requires a stable ID.");
    const auto id=raw.at("id").get<std::string>();
    auto* existing=Composable::FindLoadout(doc,id);
    Composable::Loadout next;
    Json prior;
    std::map<std::string,Json> oldParts;
    if(existing) {
        next=*existing;
        if(!Composable::Resolve(doc,*existing,prior,error))return false;
        oldParts=Composable::Split(prior);
    } else next.id=id;
    next.metadata=Composable::Metadata(raw);
    const auto profession=raw.value("profession",std::string());
    const auto parts=Composable::Split(raw);
    const EditIndex index(doc);
    for(const auto& [kind,data]:parts) {
        const auto ref=next.refs.find(kind);
        auto* previous=ref==next.refs.end()?nullptr:Composable::Find(doc,kind,ref->second);
        if(existing&&oldParts.count(kind)&&ExactContent(oldParts.at(kind),data)) {
            if(previous)for(auto it=previous->data.begin();it!=previous->data.end();++it)
                if(it.key()!="gear")next.metadata.erase(it.key());
            continue;
        }
        if(previous&&ExactContent(previous->data,data))continue;
        const auto candidates=index.Matches(kind,data,profession);
        if(!candidates.empty()) {next.refs[kind]=candidates.front().id;continue;}
        if(previous&&index.Users(kind,previous->id).size()==1) {
            previous->data=data;
            continue;
        }
        std::string componentId=kind+":"+id;
        for(unsigned i=1;Composable::Find(doc,kind,componentId);++i)
            componentId=kind+":"+id+":"+std::to_string(i);
        doc.components[kind].push_back({componentId,
            Composable::UniqueComponentName(doc,kind,Composable::SuggestComponentName(kind,data,lookup)),data});
        next.refs[kind]=componentId;
    }
    if(existing)*existing=std::move(next);
    else doc.loadouts.push_back(std::move(next));
    Json assembled;
    const auto* saved=Composable::FindLoadout(doc,id);
    if(!Composable::Validate(doc,error)||!saved||!Composable::Resolve(doc,*saved,assembled,error))return false;
    return ExactContent(assembled,raw)||Composable::Fail(error,"Exact edited Loadout round-trip mismatch.");
}
} // namespace AlterEgo::NativeLibrary
