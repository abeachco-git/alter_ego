#pragma once
// Typed character cache. JSON is the legacy/runtime codec, never a database payload.
#include "CacheDatabase.h"
#include "CharacterRequest.h"
#include <nlohmann/json.hpp>
#include <fstream>

namespace AlterEgo::Cache {
namespace CharacterRows {
using Json = nlohmann::json;
enum class FieldType { Text, Integer, Boolean };
struct Field { const char* path; FieldType type; };
inline constexpr Field kCharacter[] = {
    {"/account_name", FieldType::Text},
    {"/name", FieldType::Text},
    {"/race", FieldType::Text},
    {"/gender", FieldType::Text},
    {"/profession", FieldType::Text},
    {"/level", FieldType::Integer},
    {"/age", FieldType::Integer},
    {"/created", FieldType::Text},
    {"/last_modified", FieldType::Text},
    {"/data_refreshed", FieldType::Integer},
    {"/deaths", FieldType::Integer},
    {"/crafting_cached", FieldType::Boolean},
    {"/active_build_tab", FieldType::Integer},
    {"/active_equipment_tab", FieldType::Integer},
    {"/core_verified",FieldType::Integer},{"/builds_verified",FieldType::Integer},
    {"/tabs_verified",FieldType::Integer},{"/physical_verified",FieldType::Integer},
    {"/crafting_verified",FieldType::Integer}
};
inline constexpr Field kEquipment[] = {
    {"/id", FieldType::Integer},
    {"/slot", FieldType::Text},
    {"/skin", FieldType::Integer},
    {"/tab", FieldType::Integer},
    {"/stat_id", FieldType::Integer},
    {"/binding", FieldType::Text},
    {"/bound_to", FieldType::Text},
    {"/charges", FieldType::Integer},
    {"/physical_supplement",FieldType::Boolean}
};
inline constexpr Field kBuild[] = {
    {"/tab", FieldType::Integer},
    {"/is_active", FieldType::Boolean},
    {"/name", FieldType::Text},
    {"/profession", FieldType::Text},
    {"/specializations/0/id", FieldType::Integer},
    {"/specializations/0/traits/0", FieldType::Integer},
    {"/specializations/0/traits/1", FieldType::Integer},
    {"/specializations/0/traits/2", FieldType::Integer},
    {"/specializations/1/id", FieldType::Integer},
    {"/specializations/1/traits/0", FieldType::Integer},
    {"/specializations/1/traits/1", FieldType::Integer},
    {"/specializations/1/traits/2", FieldType::Integer},
    {"/specializations/2/id", FieldType::Integer},
    {"/specializations/2/traits/0", FieldType::Integer},
    {"/specializations/2/traits/1", FieldType::Integer},
    {"/specializations/2/traits/2", FieldType::Integer},
    {"/skills/heal", FieldType::Integer},
    {"/skills/utilities/0", FieldType::Integer},
    {"/skills/utilities/1", FieldType::Integer},
    {"/skills/utilities/2", FieldType::Integer},
    {"/skills/elite", FieldType::Integer},
    {"/aquatic_skills/heal", FieldType::Integer},
    {"/aquatic_skills/utilities/0", FieldType::Integer},
    {"/aquatic_skills/utilities/1", FieldType::Integer},
    {"/aquatic_skills/utilities/2", FieldType::Integer},
    {"/aquatic_skills/elite", FieldType::Integer},
    {"/pets/0", FieldType::Integer},
    {"/pets/1", FieldType::Integer},
    {"/pets/2", FieldType::Integer},
    {"/pets/3", FieldType::Integer},
    {"/legends/0", FieldType::Text},
    {"/legends/1", FieldType::Text},
    {"/legends/2", FieldType::Text},
    {"/legends/3", FieldType::Text},
    {"/weapons_known", FieldType::Boolean}
};
inline constexpr Field kCrafting[] = {
    {"/discipline",FieldType::Text},{"/rating",FieldType::Integer},{"/active",FieldType::Boolean}
};
template<size_t N> inline void BindFields(Database::Statement& q,int first,const Json& row,const Field (&fields)[N]) {
    if(!row.is_object())throw std::runtime_error("Invalid character record");
    for(const auto& field:fields) {
        const Json::json_pointer p(field.path);
        if(!row.contains(p)||row.at(p).is_null())q.BindNull(first++);
        else {
            const auto& value=row.at(p);
            if(field.type==FieldType::Text)q.Bind(first++,value.get<std::string>());
            else if(field.type==FieldType::Boolean) {
                if(!value.is_boolean())throw std::runtime_error("Invalid character boolean");
                q.Bind(first++,int64_t(value.get<bool>()));
            } else {
                if(!value.is_number_integer())throw std::runtime_error("Invalid character integer");
                q.Bind(first++,value.get<int64_t>());
            }
        }
    }
}
template<size_t N> inline Json ReadFields(Database::Statement& q,int first,const Field (&fields)[N]) {
    Json row=Json::object();
    for(const auto& field:fields) {
        if(!q.IsNull(first)) {
            const Json::json_pointer p(field.path);
            if(field.type==FieldType::Text)row[p]=q.Text(first);
            else if(field.type==FieldType::Boolean)row[p]=q.Integer(first)!=0;
            else row[p]=q.Integer(first);
        }
        ++first;
    }
    return row;
}
inline Json Array(const Json& row,const char* key) {
    auto it=row.find(key);
    if(it==row.end())return Json::array();
    if(!it->is_array())throw std::runtime_error("Invalid character collection");
    return *it;
}
inline Json Object(const Json& row,const char* key) {
    auto it=row.find(key);
    if(it==row.end())return Json::object();
    if(!it->is_object())throw std::runtime_error("Invalid character map");
    return *it;
}
inline int64_t Int(const Json& j) {
    if(!j.is_number_integer())throw std::runtime_error("Invalid character integer");
    return j.get<int64_t>();
}
inline void Store(Database::Connection& db,const Json& data,bool incremental=false) {
    if(!data.is_object()||!data.contains("characters"))throw std::runtime_error("Invalid character snapshot");
    const auto stamp=Int(data.at("last_updated"));
    if(stamp<0)throw std::runtime_error("Invalid character snapshot timestamp");
    auto old=db.Prepare("SELECT last_updated FROM character_snapshot WHERE singleton=1");
    const bool present=old.Step();
    const auto oldStamp=present?old.Integer(0):int64_t(0);
    old.Reset();
    if(!incremental && oldStamp>stamp)throw std::runtime_error("Older character snapshot");
    if(incremental && (!present || Array(data,"characters").size()!=1))throw std::runtime_error("Incremental snapshot requires one character and a native baseline");
    // Preserve roster semantics of the existing aggregate projection. The save
    // caller serializes capture and publication; rollback retains the old roster.
    if(!incremental)db.Exec("DELETE FROM character_rows");
    auto lookup=db.Prepare("SELECT character_id,data_refreshed FROM character_rows WHERE account_name=? AND name=?");
    auto remove=db.Prepare("DELETE FROM character_rows WHERE character_id=?");
    int64_t nextId=incremental?db.Scalar("SELECT coalesce(max(character_id),0) FROM character_rows"):0;
    auto character=db.Prepare("INSERT INTO character_rows(character_id,account_name,name,race,gender,profession,level,age,created,last_modified,data_refreshed,deaths,crafting_cached,active_build_tab,active_equipment_tab,core_verified,builds_verified,tabs_verified,physical_verified,crafting_verified) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
    auto crafting=db.Prepare("INSERT INTO character_crafting VALUES(?,?,?,?,?)");
    auto eq=db.Prepare("INSERT INTO character_equipment(character_id,ordinal,item_id,slot,skin,tab,stat_id,binding,bound_to,charges,physical_supplement) VALUES(?,?,?,?,?,?,?,?,?,?,?)");
    auto values=db.Prepare("INSERT INTO character_equipment_values VALUES(?,?,?,?,?)");
    auto attrs=db.Prepare("INSERT INTO character_equipment_attributes VALUES(?,?,?,?)");
    auto tabnames=db.Prepare("INSERT INTO character_equipment_tab_names VALUES(?,?,?)");
    auto bt=db.Prepare("INSERT INTO character_build_tabs(character_id,ordinal,tab,is_active,name,profession,spec0_id,spec0_trait0,spec0_trait1,spec0_trait2,spec1_id,spec1_trait0,spec1_trait1,spec1_trait2,spec2_id,spec2_trait0,spec2_trait1,spec2_trait2,bar0_heal,bar0_utility0,bar0_utility1,bar0_utility2,bar0_elite,bar1_heal,bar1_utility0,bar1_utility1,bar1_utility2,bar1_elite,pet0,pet1,pet2,pet3,legend0,legend1,legend2,legend3,weapons_known) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
    auto weapons=db.Prepare("INSERT INTO character_build_weapons VALUES(?,?,?,?)");
    int64_t id=0;
    for(auto ch:Array(data,"characters")) {
        if(!ch.is_object())throw std::runtime_error("Invalid character");
        if(!ch.contains("account_name"))ch["account_name"]="";
        if(incremental) {
            lookup.Bind(1,ch.at("account_name").get<std::string>());
            lookup.Bind(2,ch.at("name").get<std::string>());
            if(lookup.Step()) {
                id=lookup.Integer(0);
                if(!lookup.IsNull(1) && lookup.Integer(1)>ch.value("data_refreshed",int64_t(0)))
                    throw std::runtime_error("Older character record");
                lookup.Reset();
                remove.Bind(1,id);remove.Execute();remove.Reset();
            } else { lookup.Reset();id=++nextId; }
        } else ++id;
        for(const char* key:{"core_verified","builds_verified","tabs_verified","physical_verified","crafting_verified"})
            if(!ch.contains(key)||ch.at(key).is_null())ch[key]=0;
        character.Bind(1,id);BindFields(character,2,ch,kCharacter);character.Execute();character.Reset();
        int64_t n=0;
        for(const auto& row:Array(ch,"crafting")) {
            crafting.Bind(1,id);crafting.Bind(2,n++);BindFields(crafting,3,row,kCrafting);crafting.Execute();crafting.Reset();
        }
        n=0;
        for(const auto& row:Array(ch,"equipment")) {
            eq.Bind(1,id);eq.Bind(2,n);BindFields(eq,3,row,kEquipment);eq.Execute();eq.Reset();
            for(const char* kind:{"upgrades","infusions","dyes"}) {
                int64_t ordinal=0;
                for(const auto& v:Array(row,kind)) {
                    values.Bind(1,id);values.Bind(2,n);values.Bind(3,std::string(kind));values.Bind(4,ordinal++);values.Bind(5,Int(v));values.Execute();values.Reset();
                }
            }
            const auto attributes=Object(row,"attributes");
            for(const auto& [key,value]:attributes.items()) {
                attrs.Bind(1,id);attrs.Bind(2,n);attrs.Bind(3,key);attrs.Bind(4,Int(value));attrs.Execute();attrs.Reset();
            }
            ++n;
        }
        const auto names=Object(ch,"equipment_tab_names");
        for(const auto& [key,value]:names.items()) {
            size_t end=0;const auto tab=std::stoll(key,&end);
            if(end!=key.size())throw std::runtime_error("Invalid equipment tab");
            tabnames.Bind(1,id);tabnames.Bind(2,static_cast<int64_t>(tab));tabnames.Bind(3,value.get<std::string>());tabnames.Execute();tabnames.Reset();
        }
        n=0;
        for(auto row:Array(ch,"build_tabs")) {
            if(row.contains("weapons"))row["weapons_known"]=true;
            bt.Bind(1,id);bt.Bind(2,n);BindFields(bt,3,row,kBuild);bt.Execute();bt.Reset();
            int64_t ordinal=0;
            for(const auto& value:Array(row,"weapons")) {
                weapons.Bind(1,id);weapons.Bind(2,n);weapons.Bind(3,ordinal++);weapons.Bind(4,Int(value));weapons.Execute();weapons.Reset();
            }
            ++n;
        }
    }
    auto mark=db.Prepare("INSERT INTO character_snapshot VALUES(1,?) ON CONFLICT(singleton) DO UPDATE SET last_updated=excluded.last_updated");
    mark.Bind(1,incremental?std::max(stamp,oldStamp):stamp);mark.Execute();
}
inline std::optional<Json> Read(Database::Connection& db) {
    auto mark=db.Prepare("SELECT last_updated FROM character_snapshot WHERE singleton=1");
    if(!mark.Step())return std::nullopt;
    Json out={{"last_updated",mark.Integer(0)},{"characters",Json::array()}};
    auto chars=db.Prepare("SELECT character_id,account_name,name,race,gender,profession,level,age,created,last_modified,data_refreshed,deaths,crafting_cached,active_build_tab,active_equipment_tab,core_verified,builds_verified,tabs_verified,physical_verified,crafting_verified FROM character_rows ORDER BY character_id");
    auto crafting=db.Prepare("SELECT discipline,rating,active FROM character_crafting WHERE character_id=? ORDER BY ordinal");
    auto equipment=db.Prepare("SELECT ordinal,item_id,slot,skin,tab,stat_id,binding,bound_to,charges,physical_supplement FROM character_equipment WHERE character_id=? ORDER BY ordinal");
    auto values=db.Prepare("SELECT kind,value FROM character_equipment_values WHERE character_id=? AND equipment_ordinal=? ORDER BY kind,ordinal");
    auto attrs=db.Prepare("SELECT attribute,value FROM character_equipment_attributes WHERE character_id=? AND equipment_ordinal=? ORDER BY attribute");
    auto tabnames=db.Prepare("SELECT tab,name FROM character_equipment_tab_names WHERE character_id=? ORDER BY tab");
    auto builds=db.Prepare("SELECT ordinal,tab,is_active,name,profession,spec0_id,spec0_trait0,spec0_trait1,spec0_trait2,spec1_id,spec1_trait0,spec1_trait1,spec1_trait2,spec2_id,spec2_trait0,spec2_trait1,spec2_trait2,bar0_heal,bar0_utility0,bar0_utility1,bar0_utility2,bar0_elite,bar1_heal,bar1_utility0,bar1_utility1,bar1_utility2,bar1_elite,pet0,pet1,pet2,pet3,legend0,legend1,legend2,legend3,weapons_known FROM character_build_tabs WHERE character_id=? ORDER BY ordinal");
    auto weapons=db.Prepare("SELECT weapon_id FROM character_build_weapons WHERE character_id=? AND build_ordinal=? ORDER BY ordinal");
    while(chars.Step()) {
        const auto id=chars.Integer(0);
        auto ch=ReadFields(chars,1,kCharacter);
        // Zero means no independently verified timestamp. Preserve legacy
        // codec omission; the database itself always stores explicit zero.
        for(const char* key:{"core_verified","builds_verified","tabs_verified","physical_verified","crafting_verified"})
            if(ch.value(key,int64_t(0))==0)ch.erase(key);
        crafting.Bind(1,id);
        while(crafting.Step())ch["crafting"].push_back(ReadFields(crafting,0,kCrafting));
        crafting.Reset();
        ch["equipment"]=Json::array();
        equipment.Bind(1,id);
        while(equipment.Step()) {
            auto eq=ReadFields(equipment,1,kEquipment);
            values.Bind(1,id);values.Bind(2,equipment.Integer(0));
            while(values.Step())eq[values.Text(0)].push_back(values.Integer(1));
            values.Reset();
            attrs.Bind(1,id);attrs.Bind(2,equipment.Integer(0));
            while(attrs.Step())eq["attributes"][attrs.Text(0)]=attrs.Integer(1);
            attrs.Reset();
            ch["equipment"].push_back(std::move(eq));
        }
        equipment.Reset();
        tabnames.Bind(1,id);
        while(tabnames.Step())ch["equipment_tab_names"][std::to_string(tabnames.Integer(0))]=tabnames.Text(1);
        tabnames.Reset();
        ch["build_tabs"]=Json::array();
        builds.Bind(1,id);
        while(builds.Step()) {
            auto bt=ReadFields(builds,1,kBuild);
            if(bt.value("weapons_known",false))bt["weapons"]=Json::array();
            bt.erase("weapons_known");
            weapons.Bind(1,id);weapons.Bind(2,builds.Integer(0));
            while(weapons.Step())bt["weapons"].push_back(weapons.Integer(0));
            weapons.Reset();
            ch["build_tabs"].push_back(std::move(bt));
        }
        builds.Reset();
        out["characters"].push_back(std::move(ch));
    }
    return out;
}
} // namespace CharacterRows
inline bool WriteCharacters(const std::string& root,const CharacterRows::Json& data,bool incremental=false) {
    if(root.empty())return false;
    try {
        std::lock_guard<std::mutex> lock(Mutex());
        const auto path=Storage::Paths(root).Cache();
        std::filesystem::create_directories(path.parent_path());
        Database::Connection db(path.u8string(),Database::Connection::OpenMode::Create);
        Initialize(db);Database::Transaction tx(db);
        CharacterRows::Store(db,data,incremental);tx.Commit();return true;
    }catch(...){return false;}
}
// Reconcile only accounts for which the request owner received a verified roster.
 // Keep row IDs, ordering, unknown legacy owners and unrelated accounts intact.
inline bool DeleteAbsentCharacters(const std::string& root,const CharacterRequest::Roster& roster) {
    if(root.empty())return false;
    if(roster.empty())return true;
    try {
        std::lock_guard<std::mutex> lock(Mutex());
        const auto path=Storage::Paths(root).Cache();
        if(!std::filesystem::exists(path))return false;
        Database::Connection db(path.u8string(),Database::Connection::OpenMode::Existing);
        Initialize(db);Database::Transaction tx(db);
        if(db.Scalar("SELECT count(*) FROM character_snapshot")==0)return false;
        auto read=db.Prepare("SELECT character_id,name FROM character_rows WHERE account_name=?");
        auto remove=db.Prepare("DELETE FROM character_rows WHERE character_id=? AND account_name=?");
        for(const auto& [account,names]:roster) {
            if(account.empty())continue;
            std::vector<int64_t> absent;
            read.Bind(1,account);
            while(read.Step())if(!names.count(read.Text(1)))absent.push_back(read.Integer(0));
            read.Reset();
            for(const auto id:absent) {
                remove.Bind(1,id);remove.Bind(2,account);remove.Execute();remove.Reset();
            }
        }
        tx.Commit();return true;
    }catch(...){return false;}
}
// False means no native baseline; nullopt means inspection failed. Never
// interpret a corrupt or unreadable native cache as an empty one.
inline std::optional<bool> HasCharacterSnapshot(const std::string& root) {
    if(root.empty())return std::nullopt;
    try {
        std::lock_guard<std::mutex> lock(Mutex());
        const auto path=Storage::Paths(root).Cache();
        if(!std::filesystem::exists(path))return false;
        Database::Connection db(path.u8string(),Database::Connection::OpenMode::Existing);
        Initialize(db);
        return db.Scalar("SELECT count(*) FROM character_snapshot")!=0;
    }catch(...){return std::nullopt;}
}
inline std::optional<CharacterRows::Json> ReadCharacters(const std::string& root) {
    if(root.empty())return std::nullopt;
    try {
        std::lock_guard<std::mutex> lock(Mutex());
        const auto path=Storage::Paths(root).Cache();
        if(std::filesystem::exists(path)) {
            Database::Connection db(path.u8string(),Database::Connection::OpenMode::Existing);
            Initialize(db);Database::Transaction tx(db);
            auto out=CharacterRows::Read(db);tx.Commit();if(out)return out;
        }
        const auto legacy=Storage::ResolveCache(root,"characters.json");
        CharacterRows::Json data;
        {std::ifstream in(legacy);if(!in)return std::nullopt;in>>data;}
        std::filesystem::create_directories(path.parent_path());
        Database::Connection db(path.u8string(),Database::Connection::OpenMode::Create);
        Initialize(db);Database::Transaction tx(db);
        CharacterRows::Store(db,data);
        auto out=CharacterRows::Read(db);tx.Commit();
        // Native presence wins, including an empty roster. Retain the legacy
        // file on failed migration; remove only after the transaction commits.
        std::error_code ec;std::filesystem::remove(std::filesystem::u8path(legacy),ec);
        return out;
    }catch(...){return std::nullopt;}
}
} // namespace AlterEgo::Cache
