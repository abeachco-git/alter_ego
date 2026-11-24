#pragma once
#include "NativeLibraryEditing.h"
#include "NativeLibraryStore.h"

namespace AlterEgo::NativeLibrary {
constexpr const char* kLibraryFormat="alter_ego_native_library";
constexpr int kLibraryVersion=1;
struct PortableLibrary {
    Snapshot data;
    bool loadouts=false, fashion=false;
    bool legacy=false;
};

inline Json ExportPortableLibrary(const Snapshot& source,bool builds,bool fashion) {
    Json result={{"format",kLibraryFormat},{"version",kLibraryVersion},
        {"collections",{{"loadouts",builds},{"fashion",fashion}}}};
    if(builds)result["loadouts"]=Composable::ToJson(source.loadouts);
    std::set<std::string> associated;
    if(builds)for(const auto& l:source.loadouts.loadouts)
        if(!l.fashion_id.empty())associated.insert(l.fashion_id);
    if(fashion||builds) {
        result["fashion"]=Json::array();
        for(const auto& look:source.fashion)
            if(fashion||associated.count(look.id))result["fashion"].push_back(Fashion::ToJson(look));
    }
    return result;
}
inline bool ParsePortableLibrary(const Json& value,PortableLibrary& out,std::string& error) {
    try {
        PortableLibrary next;
        if(!value.is_object())return Composable::Fail(error,"Expected a library object.");
        if(value.value("format",std::string())==kLibraryFormat) {
            if(value.value("version",0)!=kLibraryVersion)
                return Composable::Fail(error,"Unsupported library version.");
            const auto& collections=value.at("collections");
            next.loadouts=collections.at("loadouts").get<bool>();
            next.fashion=collections.at("fashion").get<bool>();
            if(next.loadouts&&!Composable::FromJson(value.at("loadouts"),next.data.loadouts,error))return false;
        } else if(value.contains("builds")&&value.at("builds").is_array()) {
            next.legacy=true;next.loadouts=true;
            if(!SplitLegacyExact(value,next.data.loadouts,error))return false;
        } else if(value.contains("looks")&&value.at("looks").is_array()) {
            next.legacy=true;next.fashion=true;
        } else return Composable::Fail(error,"Unsupported library format.");
        if(!next.loadouts&&!next.fashion)return Composable::Fail(error,"No collections.");
        const auto key=next.legacy?"looks":"fashion";
        std::set<std::string> ids;
        if(value.contains(key)) {
            if(!value.at(key).is_array())return Composable::Fail(error,"Invalid Fashion collection.");
            for(const auto& row:value.at(key)) {
                Fashion::Look look;
                if(!Fashion::FromJson(row,look)||look.name.empty()||!ids.insert(look.id).second)
                    return Composable::Fail(error,"Invalid or duplicate Fashion record.");
                next.data.fashion.push_back(std::move(look));
            }
        } else if(next.fashion)return Composable::Fail(error,"Fashion collection is missing.");
        for(const auto& l:next.data.loadouts.loadouts)
            if(!l.fashion_id.empty()&&!ids.count(l.fashion_id))
                return Composable::Fail(error,"Loadout refers to missing Fashion dependency.");
        out=std::move(next);return true;
    }catch(const std::exception& e){error=e.what();return false;}
}

struct ImportCounts { size_t loadoutsAdded=0,loadoutsSkipped=0,fashionAdded=0,fashionSkipped=0,fashionSimilar=0; };
inline bool MergePortableLibrary(const Snapshot& current,const PortableLibrary& input,
        bool builds,bool fashion,bool replace,Snapshot& output,ImportCounts& counts,std::string& error) {
    if((builds&&!input.loadouts)||(fashion&&!input.fashion))
        return Composable::Fail(error,"Selected collection is absent.");
    Snapshot next=current;counts={};
    auto imported=input.data.loadouts;
    // Fashion dependencies always accompany selected Loadouts. In merge mode
    // a conflicting ID is remapped, never silently substituted with another look.
    std::set<std::string> dependencies;
    if(builds)for(const auto& l:imported.loadouts)
        if(!l.fashion_id.empty())dependencies.insert(l.fashion_id);
    if(fashion&&replace) {
        // Preserve linked looks when the corresponding loadouts are not replaced.
        std::set<std::string> protectedIds;
        if(!(builds&&replace))for(const auto& l:next.loadouts.loadouts)
            if(!l.fashion_id.empty())protectedIds.insert(l.fashion_id);
        next.fashion.erase(std::remove_if(next.fashion.begin(),next.fashion.end(),
            [&](const auto& l){return !protectedIds.count(l.id);}),next.fashion.end());
    }
    std::map<std::string,std::string> remap;
    for(auto look:input.data.fashion) {
        if(!fashion&&!dependencies.count(look.id))continue;
        const auto original=look.id;
        auto exists=[&](const std::string& id){return std::find_if(next.fashion.begin(),next.fashion.end(),
            [&](const auto& row){return row.id==id;});};
        auto local=exists(original);
        if(local!=next.fashion.end()) {
            if(ExactContent(Fashion::ToJson(*local),Fashion::ToJson(look))||!dependencies.count(original)) {
                remap[original]=original;++counts.fashionSkipped;continue;
            }
            for(unsigned i=1;exists(look.id)!=next.fashion.end();++i)
                look.id=original+":import:"+std::to_string(i);
        }
        if(std::any_of(next.fashion.begin(),next.fashion.end(),
            [&](const auto& row){return row.wardrobe_link==look.wardrobe_link;}))++counts.fashionSimilar;
        remap[original]=look.id;next.fashion.push_back(std::move(look));++counts.fashionAdded;
    }
    if(builds) {
        for(auto& l:imported.loadouts)
            if(!l.fashion_id.empty())l.fashion_id=remap.at(l.fashion_id);
        if(input.legacy&&!replace) {
            // Legacy records have no intentionally independent component IDs.
            // Reuse exact existing data rather than mint duplicate components.
            for(const auto& l:imported.loadouts) {
                if(Composable::FindLoadout(next.loadouts,l.id)){++counts.loadoutsSkipped;continue;}
                Json raw;
                if(!Composable::Resolve(imported,l,raw,error)||!PutLoadout(next.loadouts,raw,error))return false;
                Composable::FindLoadout(next.loadouts,l.id)->fashion_id=l.fashion_id;
                ++counts.loadoutsAdded;
            }
        } else {
            Composable::Document merged;
            if(!Composable::Merge(next.loadouts,imported,replace,merged,counts.loadoutsAdded,counts.loadoutsSkipped,error))return false;
            next.loadouts=std::move(merged);
        }
    }
    std::set<std::string> lookIds;
    for(const auto& l:next.fashion)lookIds.insert(l.id);
    for(const auto& l:next.loadouts.loadouts)
        if(!l.fashion_id.empty()&&!lookIds.count(l.fashion_id))
            return Composable::Fail(error,"Import would invalidate an existing Fashion association.");
    output=std::move(next);return true;
}
} // namespace AlterEgo::NativeLibrary
