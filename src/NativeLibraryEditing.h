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
        for(const auto& set:doc.equipment_sets)
            for(const auto& [kind,id]:set.refs)users_[{kind,id}].push_back(set.id);
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

// Derive Skills context from verified owners, not a picker hint.
inline std::string VerifiedSkillsProfession(const Composable::Document& doc,
                                              const std::string& id) {
    std::string profession;
    for (const auto& l : doc.loadouts) {
        const auto ref=l.refs.find("skills");
        if(ref==l.refs.end()||ref->second!=id)continue;
        const auto p=Composable::ProfessionOf(doc,l);
        if(p.empty()||(!profession.empty()&&p!=profession))return {};
        profession=p;
    }
    return profession;
}

// An explicit choice in Copy & Edit changes only one reference. Check
// source identity, exact payload and profession again inside the transaction.
inline bool ReuseEditedForLoadout(Composable::Document& doc,
    const std::string& loadoutId,const std::string& kind,
    const std::string& source,const std::string& candidate,const Json& draft,
    std::string& error) {
    const auto* l=Composable::FindLoadout(doc,loadoutId);
    if(!l||!l->refs.count(kind)||l->refs.at(kind)!=source||source==candidate)
        return Composable::Fail(error,"The source component changed.");
    const auto matches=EditIndex(doc).Matches(kind,draft,Composable::ProfessionOf(doc,*l));
    if(std::none_of(matches.begin(),matches.end(),[&](const Match& m){return m.id==candidate;}))
        return Composable::Fail(error,"The selected component is no longer an exact compatible match.");
    return Composable::Assign(doc,loadoutId,kind,candidate,error);
}

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
        for(auto& set:next.equipment_sets) {
            auto ref=set.refs.find(kind);
            if(ref!=set.refs.end()&&ref->second==source)ref->second=survivor;
        }
        return Composable::RemoveUnreferenced(next,kind,source,err);
    },error);
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
    const auto profession=raw.value("profession",std::string());
    // A complete editor must obey the same profession guard as a private
    // Base Build editor. Otherwise unchanged Skills can remain shared with
    // another profession, even when their exact payload is empty.
    if(existing&&Composable::ProfessionOf(doc,*existing)!=profession)
        return Composable::Fail(error,"Cannot change profession of an existing Loadout; create a new Loadout instead.");
    next.metadata=Composable::Metadata(raw);
    const auto parts=Composable::Split(raw);
    const EditIndex index(doc);
    if(!next.equipment_set_id.empty()) {
        for(const auto& [kind,data]:parts)if(Composable::SetOwns(doc,next,kind)&&
            (!oldParts.count(kind)||!ExactContent(oldParts.at(kind),data)))
            return Composable::Fail(error,"Detach the linked equipment set before editing gear.");
        const auto refs=Composable::EffectiveRefs(doc,next);
        for(const auto& [kind,id]:refs)if(Composable::IsEquipmentKind(kind)) {
            const auto* part=Composable::Find(doc,kind,id);
            if(part)for(auto it=part->data.begin();it!=part->data.end();++it)
                if(it.key()!="gear")next.metadata.erase(it.key());
        }
    }
    for(const auto& [kind,data]:parts) {
        if(Composable::SetOwns(doc,next,kind))continue;
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
// Save a named collection of references, never a second copy of equipment.
inline bool PutEquipmentSet(Composable::Document& doc,const std::string& id,
    const std::string& name,const Json& raw,std::string& error,
    const Composable::ComponentNameLookup& lookup={},const std::set<std::string>& kinds={}) {
    if(id.empty()||Composable::ComponentNameKey(name).empty()||Composable::FindEquipmentSet(doc,id))
        return Composable::Fail(error,"Invalid equipment set identity or name.");
    Composable::EquipmentSet set{id,name,{}};
    const auto parts=Composable::Split(raw);
    const EditIndex index(doc);
    for(const auto& [kind,data]:parts) {
        if(!Composable::IsEquipmentKind(kind)||(!kinds.empty()&&!kinds.count(kind)))continue;
        const auto matches=index.Matches(kind,data);
        if(!matches.empty()) {set.refs[kind]=matches.front().id;continue;}
        std::string componentId=kind+":"+id;
        for(unsigned n=1;Composable::Find(doc,kind,componentId);++n)
            componentId=kind+":"+id+":"+std::to_string(n);
        doc.components[kind].push_back({componentId,
            Composable::UniqueComponentName(doc,kind,Composable::SuggestComponentName(kind,data,lookup)),data});
        set.refs[kind]=componentId;
    }
    doc.equipment_sets.push_back(std::move(set));
    return Composable::Validate(doc,error);
}
inline bool ApplyEquipmentSet(Composable::Document& doc,const std::string& loadoutId,
    const std::string& setId,std::string& error) {
    return Composable::Transaction(doc,[&](Composable::Document& next,std::string& err) {
        auto* loadout=Composable::FindLoadout(next,loadoutId);
        const auto* set=Composable::FindEquipmentSet(next,setId);
        if(!loadout||!set)return Composable::Fail(err,"Equipment set or Loadout not found.");
        loadout->refs=Composable::EffectiveRefs(next,*loadout);
        for(const auto& [kind,id]:set->refs)loadout->refs.erase(kind);
        loadout->equipment_set_id=setId;
        Json resolved;
        return Composable::Resolve(next,*loadout,resolved,err);
    },error);
}
inline bool DetachEquipmentSet(Composable::Document& doc,const std::string& loadoutId,std::string& error) {
    return Composable::Transaction(doc,[&](Composable::Document& next,std::string& err) {
        auto* l=Composable::FindLoadout(next,loadoutId);
        if(!l||l->equipment_set_id.empty())return Composable::Fail(err,"Loadout is not linked.");
        l->refs=Composable::EffectiveRefs(next,*l);
        l->equipment_set_id.clear();
        return true;
    },error);
}
inline bool SaveSetFromLoadout(Composable::Document& doc,const std::string& loadoutId,
    const std::string& newId,const std::string& name,std::string& error,
    const std::set<std::string>& kinds={}) {
    return Composable::Transaction(doc,[&](Composable::Document& next,std::string& err) {
        auto* l=Composable::FindLoadout(next,loadoutId);
        if(!l||newId.empty()||Composable::FindEquipmentSet(next,newId)||Composable::ComponentNameKey(name).empty())
            return Composable::Fail(err,"Invalid new equipment set.");
        Composable::EquipmentSet set{newId,name,{}};
        const auto* old=Composable::FindEquipmentSet(next,l->equipment_set_id);
        l->refs=Composable::EffectiveRefs(next,*l);
        for(const auto& [kind,id]:l->refs)
            if(Composable::IsEquipmentKind(kind)&&(kinds.empty()?(!old||old->refs.count(kind)):kinds.count(kind)))set.refs[kind]=id;
        for(const auto& [kind,id]:set.refs)l->refs.erase(kind);
        next.equipment_sets.push_back(std::move(set));
        l->equipment_set_id=newId;
        return true;
    },error);
}
inline bool AssignSetComponent(Composable::Document& doc,const std::string& setId,
    const std::string& kind,const std::string& id,std::string& error) {
    return Composable::Transaction(doc,[&](Composable::Document& next,std::string& err) {
        auto* set=Composable::FindEquipmentSet(next,setId);
        if(!set||!Composable::IsEquipmentKind(kind)||(!id.empty()&&!Composable::Find(next,kind,id)))
            return Composable::Fail(err,"Equipment set or component not found.");
        if(id.empty()) {
            const auto prior=set->refs.find(kind);
            if(prior==set->refs.end())return true;
            if(set->refs.size()==1)return Composable::Fail(err,"Equipment set must include at least one category.");
            for(auto& l:next.loadouts)if(l.equipment_set_id==setId)l.refs[kind]=prior->second;
            set->refs.erase(prior);
        } else {
            set->refs[kind]=id;
            for(auto& l:next.loadouts)if(l.equipment_set_id==setId)l.refs.erase(kind);
        }
        for(const auto& l:next.loadouts)if(l.equipment_set_id==setId) {
            Json raw;if(!Composable::Resolve(next,l,raw,err))return false;
        }
        return true;
    },error);
}
inline std::vector<Match> ExactEquipmentSets(const Composable::Document& doc,const Json& raw) {
    std::vector<Match> result;
    const auto parts=Composable::Split(raw);
    for(const auto& set:doc.equipment_sets) {
        bool match=!set.refs.empty();
        for(const auto& [kind,id]:set.refs) {
            const auto* part=Composable::Find(doc,kind,id);
            auto expected=parts.find(kind);
            if(!part||expected==parts.end()||!ExactContent(part->data,expected->second)){match=false;break;}
        }
        if(match)result.push_back({set.id,set.name,static_cast<std::int64_t>(Composable::EquipmentSetUsers(doc,set.id).size())});
    }
    return result;
}
} // namespace AlterEgo::NativeLibrary
