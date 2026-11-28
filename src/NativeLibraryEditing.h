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
        if(group==hashes_.end()||(!profession.empty()&&!Composable::KindApplies(kind,profession)))return out;
        const auto bucket=group->second.find(ExactFingerprint(data));
        if(bucket==group->second.end())return out;
        for(const auto& id:bucket->second) {
            const auto* part=Composable::Find(doc_,kind,id);
            if(!part||!ExactContent(part->data,data))continue;
            const auto& users=Users(kind,id);
            // Do not share profession-sensitive skills across professions.
            if(kind=="skills") {
                const auto declared=Composable::StoredSkillsProfession(data);
                const auto expected=profession.empty()?declared:profession;
                if(expected.empty()||(!declared.empty()&&declared!=expected)||
                   Composable::VerifiedSkillsProfession(doc_,id)!=expected)continue;
            }
            out.push_back({id,part->name,static_cast<std::int64_t>(users.size())});
        }
        return out;
    }
};

// Native callers use the same persisted/legacy ownership evidence.
using Composable::VerifiedSkillsProfession;

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
        const auto first=Composable::VerifiedSkillsProfession(doc,source);
        const auto second=Composable::VerifiedSkillsProfession(doc,survivor);
        if(first.empty()||first!=second)
            return Composable::Fail(error,"Skills profession is unverified or incompatible.");
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
                // Companion arrays are positional, not whole-list replacements
                // when the recorded baseline and stored lengths differ.
                if(existing!=stored.end()&&(i.key()=="pets"||i.key()=="legend_codes")&&
                   existing->is_array()&&old->is_array()&&i.value().is_array()&&
                   i.value().size()>=old->size()){
                    Json patched=*existing;
                    for(size_t slot=0;slot<i.value().size();++slot){
                        if(slot<old->size()&&ExactContent((*old)[slot],i.value()[slot]))continue;
                        // Newly added nulls are spacing for a later explicit
                        // choice, not edits to earlier stored positions.
                        if(slot>=old->size()&&i.value()[slot].is_null()&&slot<patched.size())continue;
                        while(patched.size()<=slot)patched.push_back(nullptr);
                        patched[slot]=i.value()[slot];
                    }
                    result[i.key()]=std::move(patched);
                }else result[i.key()]=existing==stored.end()?i.value():PatchDraft(*existing,*old,i.value());
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
        if(Composable::IsOptionalKind(kind)&&data.empty()&&ref==next.refs.end())continue;
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
        doc.components[kind].push_back(Composable::GeneratedComponent(doc,kind,componentId,data,lookup));
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
        if(kind=="aquatic"&&data.empty())continue;
        const auto matches=index.Matches(kind,data);
        if(!matches.empty()) {set.refs[kind]=matches.front().id;continue;}
        std::string componentId=kind+":"+id;
        for(unsigned n=1;Composable::Find(doc,kind,componentId);++n)
            componentId=kind+":"+id+":"+std::to_string(n);
        doc.components[kind].push_back(Composable::GeneratedComponent(doc,kind,componentId,data,lookup));
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
inline bool CreateEquipmentSet(Composable::Document& doc,const std::string& id,const std::string& name,
    const std::map<std::string,std::string>& refs,std::string& error) {
    return Composable::Transaction(doc,[&](Composable::Document& next,std::string& err){
        if(id.empty()||Composable::FindEquipmentSet(next,id)||Composable::ComponentNameKey(name).empty()||refs.empty())
            return Composable::Fail(err,"Equipment set needs a unique ID, name and at least one category.");
        for(const auto& [kind,ref]:refs)
            if(!Composable::IsEquipmentKind(kind)||!Composable::Find(next,kind,ref))
                return Composable::Fail(err,"Invalid equipment set component.");
        if(Composable::InfusionReferenceMode(next,refs)==InfusionMode::Conflict)
            return Composable::Fail(err,"Choose compatible Infusions and Aquatic components; aggregate and slot modes cannot coexist.");
        next.equipment_sets.push_back({id,name,refs});
        return true;
    },error);
}
// Complete membership changes are atomic: transfer removed categories from
// the ORIGINAL set, then remove direct references for newly included categories.
inline bool SaveEquipmentSetDraft(Composable::Document& doc,const std::string& setId,
    const std::string& name,const std::map<std::string,std::string>& refs,std::string& error) {
    return Composable::Transaction(doc,[&](Composable::Document& next,std::string& err){
        auto* set=Composable::FindEquipmentSet(next,setId);
        if(!set||Composable::ComponentNameKey(name).empty()||refs.empty())
            return Composable::Fail(err,"Equipment set needs a name and at least one category.");
        for(const auto& [kind,id]:refs)
            if(!Composable::IsEquipmentKind(kind)||!Composable::Find(next,kind,id))
                return Composable::Fail(err,"Invalid equipment set component.");
        for(auto& l:next.loadouts)if(l.equipment_set_id==setId){
            for(const auto& [kind,id]:set->refs)if(!refs.count(kind))l.refs[kind]=id;
            for(const auto& [kind,id]:refs)l.refs.erase(kind);
        }
        set->name=name;set->refs=refs;
        for(const auto& l:next.loadouts)if(l.equipment_set_id==setId){
            Json raw;if(!Composable::Resolve(next,l,raw,err))return false;
        }
        return true;
    },error);
}
// A Loadout reference draft is independent of typed component payload edits.
inline bool SaveLoadoutReferenceDraft(Composable::Document& doc,const std::string& loadoutId,
    const std::string& setId,const std::string& fashionId,
    const std::map<std::string,std::string>& refs,std::string& error) {
    return Composable::Transaction(doc,[&](Composable::Document& next,std::string& err){
        auto* l=Composable::FindLoadout(next,loadoutId);
        if(!l||(!setId.empty()&&!Composable::FindEquipmentSet(next,setId)))
            return Composable::Fail(err,"Loadout or equipment set not found.");
        for(const auto& [kind,id]:refs) {
            if(!Composable::IsKind(kind)||!Composable::Find(next,kind,id))
                return Composable::Fail(err,"Invalid Loadout component.");
            const auto old=l->refs.find(kind);
            if((old==l->refs.end()||old->second!=id)&&!Composable::AssignCompatible(next,*l,kind,id,err))return false;
        }
        l->equipment_set_id=setId;l->fashion_id=fashionId;l->refs=refs;
        if(!Composable::Validate(next,err))return false;
        Json raw;return Composable::Resolve(next,*l,raw,err);
    },error);
}
// Inherited replacement selections are draft-only. They are never persisted
// as conflicting direct references. A shared replacement changes the set's
// reference, NOT the payload of either component.
inline bool SaveParentReferenceDraft(Composable::Document& doc,const std::string& loadoutId,
    const std::string& setId,const std::string& fashionId,
    const std::map<std::string,std::string>& refs,
    const std::map<std::string,std::string>& inherited,
    const std::set<std::string>& sharedReferences,std::string& error){
    if(inherited.empty()){
        if(!sharedReferences.empty())return Composable::Fail(error,"Shared reference consent has no replacement.");
        return SaveLoadoutReferenceDraft(doc,loadoutId,setId,fashionId,refs,error);
    }
    return Composable::Transaction(doc,[&](Composable::Document& next,std::string& err){
        if(!SaveLoadoutReferenceDraft(next,loadoutId,setId,fashionId,refs,err))return false;
        auto* l=Composable::FindLoadout(next,loadoutId);
        auto* set=Composable::FindEquipmentSet(next,setId);
        if(!l||!set)return Composable::Fail(err,"Inherited replacement requires a linked set.");
        for(const auto& kind:sharedReferences)if(!inherited.count(kind))
            return Composable::Fail(err,"Shared reference consent has no replacement.");
        for(const auto& [kind,id]:inherited){
            if(!Composable::IsEquipmentKind(kind)||!set->refs.count(kind)||
                set->refs.at(kind)==id||!Composable::Find(next,kind,id))
                return Composable::Fail(err,"Invalid inherited replacement: "+kind);
        }
        const bool sharedSet=Composable::EquipmentSetUsers(next,setId).size()>1;
        // Apply explicitly shared selections first; a private fork retains
        // those same selections plus its private replacements.
        for(const auto& [kind,id]:inherited)if(sharedReferences.count(kind))
            set->refs[kind]=id;
        const bool needsFork=sharedSet&&std::any_of(inherited.begin(),inherited.end(),
            [&](const auto& change){return !sharedReferences.count(change.first);});
        if(needsFork){
            auto copy=*set;
            const auto prefix=setId+":"+loadoutId+":references";
            copy.id=prefix;
            for(unsigned n=1;Composable::FindEquipmentSet(next,copy.id);++n)
                copy.id=prefix+":"+std::to_string(n);
            copy.name+=" - "+l->metadata.value("name",loadoutId);
            l->equipment_set_id=copy.id;
            next.equipment_sets.push_back(std::move(copy));
            set=Composable::FindEquipmentSet(next,l->equipment_set_id);
        }
        for(const auto& [kind,id]:inherited)if(!sharedReferences.count(kind))
            set->refs[kind]=id;
        if(!Composable::ValidateNoNewInfusionConflicts(doc,next,err))return false;
        for(const auto& owner:next.loadouts)if(owner.id==loadoutId||owner.equipment_set_id==setId){
            Json raw;if(!Composable::Resolve(next,owner,raw,err))return false;
        }
        return true;
    },error);
}
// Review and commit use exactly the same patch/candidate computation.
// This is a disposable projection of the selected reference graph.
struct UnifiedChange {
    std::string kind,oldId,name;
    Json data;
    std::vector<Match> matches;
    size_t owners=0,loadouts=0,setUsers=0;
};
inline std::vector<UnifiedChange> UnifiedChanges(const Composable::Document& doc,
    const Composable::Loadout& l,const Json& before,const Json& raw){
    std::vector<UnifiedChange> changes;
    const auto prior=Composable::Split(before),edited=Composable::Split(raw);
    const auto refs=Composable::EffectiveRefs(doc,l);
    const EditIndex index(doc);
    const auto profession=Composable::ProfessionOf(doc,l);
    for(const auto& [kind,data]:edited){
        if(ExactContent(prior.at(kind),data))continue;
        const auto r=refs.find(kind);
        const auto* part=r==refs.end()?nullptr:Composable::Find(doc,kind,r->second);
        UnifiedChange change;
        change.kind=kind;
        if(part){
            change.oldId=part->id;change.name=part->name;
            change.owners=index.Users(kind,part->id).size();
            change.loadouts=Composable::EffectiveComponentUsers(doc,kind,part->id).size();
        }
        if(Composable::SetOwns(doc,l,kind))
            change.setUsers=Composable::EquipmentSetUsers(doc,l.equipment_set_id).size();
        change.data=part?PatchDraft(part->data,prior.at(kind),data):data;
        if(kind=="skills"&&data.contains("profession"))change.data["profession"]=data.at("profession");
        change.matches=index.Matches(kind,change.data,profession);
        changes.push_back(std::move(change));
    }
    return changes;
}
// Save the edited projection over the selected reference graph. Only changed
// component kinds are touched; absent categories and unknown fields remain
// absent/preserved. Private inherited changes fork a shared set, not its
// untouched components. Shared mutation requires a per-kind explicit choice.
inline bool PutUnifiedLoadout(Composable::Document& doc,const Json& raw,
    const std::set<std::string>& sharedKinds,std::string& error,
    const Composable::ComponentNameLookup& lookup={}){
    return Composable::Transaction(doc,[&](Composable::Document& next,std::string& err){
        const auto id=raw.value("id",std::string());
        auto* l=Composable::FindLoadout(next,id);
        Json before;
        if(!l||!Composable::Resolve(next,*l,before,err))return false;
        if(Composable::ProfessionOf(next,*l)!=raw.value("profession",std::string()))
            return Composable::Fail(err,"Cannot change profession of a referenced Loadout.");
        const auto changes=UnifiedChanges(next,*l,before,raw);
        l->metadata=PatchDraft(l->metadata,Composable::Metadata(before),Composable::Metadata(raw));
        for(const auto& change:changes){
            const auto& kind=change.kind;
            const auto& data=change.data;
            const auto& oldId=change.oldId;
            auto* part=oldId.empty()?nullptr:Composable::Find(next,kind,oldId);
            const bool inherited=Composable::SetOwns(next,*l,kind);
            const bool shared=part&&sharedKinds.count(kind);
            if(shared){
                if(kind=="base_build"&&part->data.value("profession",std::string())!=data.value("profession",std::string()))
                    return Composable::Fail(err,"Shared Base Build profession cannot change.");
                part->data=data;
                continue;
            }
            std::string chosen;
            for(const auto& match:change.matches){
                const auto* existing=Composable::Find(next,kind,match.id);
                if(existing&&ExactContent(existing->data,data)){chosen=match.id;break;}
            }
            const bool setShared=inherited&&Composable::EquipmentSetUsers(next,l->equipment_set_id).size()>1;
            const bool originallyShared=Composable::EffectiveComponentUsers(next,kind,oldId).size()>1;
            if(chosen.empty()&&part&&!setShared&&!originallyShared&&change.owners==1){
                part->data=data;chosen=oldId;
            }
            if(chosen.empty()){
                chosen=kind+":"+id+":edit";
                for(unsigned n=1;Composable::Find(next,kind,chosen);++n)chosen=kind+":"+id+":edit:"+std::to_string(n);
                next.components[kind].push_back(Composable::GeneratedComponent(next,kind,chosen,data,lookup));
            }
            if(inherited){
                if(chosen==oldId)continue;
                if(setShared){
                    auto copy=*Composable::FindEquipmentSet(next,l->equipment_set_id);
                    copy.id=l->equipment_set_id+":"+id+":edit";
                    const auto prefix=copy.id;
                    for(unsigned n=1;Composable::FindEquipmentSet(next,copy.id);++n)copy.id=prefix+":"+std::to_string(n);
                    copy.name+=" - "+l->metadata.value("name",id);
                    l->equipment_set_id=copy.id;
                    next.equipment_sets.push_back(std::move(copy));
                }
                auto* set=Composable::FindEquipmentSet(next,l->equipment_set_id);
                set->refs[kind]=chosen;
            }else l->refs[kind]=chosen;
        }
        Json assembled;
        if(!Composable::Resolve(next,*l,assembled,err)||!ExactContent(assembled,raw))
            return Composable::Fail(err,"Edited Loadout cannot round-trip without changing recorded fields.");
        // A shared edit must also resolve every affected owner before commit.
        for(const auto& owner:next.loadouts){
            Json projection;if(!Composable::Resolve(next,owner,projection,err))return false;
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
