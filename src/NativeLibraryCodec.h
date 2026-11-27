#pragma once
// Internal relational adapter. Known fields use typed columns and child rows.
// JSON exists only at the edit/interchange boundary, and in exception columns
// for unknown fields or unexpected representations that must round-trip.
#include "NativeLibrary.h"
#include <variant>

namespace AlterEgo::NativeLibrary::Codec {

using Json = Composable::Json;
using Database::Connection;
using Database::Statement;
using Value = std::variant<std::string,std::int64_t,std::nullptr_t>;
using Keys = std::vector<Value>;
enum class Type { Integer, Text, Boolean };
struct Field { const char* key; const char* column; Type type; };
using Fields = std::vector<Field>;

inline const Fields rootFields = {
    {"profession","profession",Type::Text},
    {"rune_name","rune_name",Type::Text},{"rune_id","rune_id",Type::Integer},
    {"relic_name","relic_name",Type::Text},{"relic_id","relic_id",Type::Integer},
    {"infusion_summary_present","infusion_summary_present",Type::Boolean},
    {"infusion_ar_total","infusion_ar_total",Type::Integer}};
inline const Fields specFields = {{"id","spec_id",Type::Integer}};
inline const Fields barFields = {{"heal","heal",Type::Integer},{"elite","elite",Type::Integer}};
inline const Fields gearFields = {
    {"item_id","item_id",Type::Integer},{"stat_id","stat_id",Type::Integer},
    {"stat_name","stat_name",Type::Text},{"weapon_type","weapon_type",Type::Text},
    {"rune","rune_name",Type::Text},{"rune_id","rune_id",Type::Integer},
    {"sigil","sigil_name",Type::Text},{"sigil_id","sigil_id",Type::Integer},
    {"sigil2","sigil2_name",Type::Text},{"sigil2_id","sigil2_id",Type::Integer},
    {"infusion","infusion_name",Type::Text},{"infusion_snapshot","infusion_snapshot",Type::Boolean}};
inline const Fields requirementFields = {
    {"item_id","item_id",Type::Integer},{"count","count",Type::Integer},
    {"attribute","attribute",Type::Text}};
inline const Fields metadataFields = {
    {"name","name",Type::Text},{"notes","notes",Type::Text},
    {"source_character","source_character",Type::Text},{"source_url","source_url",Type::Text},
    {"chat_link","chat_link",Type::Text},{"ae2_code","ae2_code",Type::Text},
    {"game_mode","game_mode",Type::Text},{"import_warning","import_warning",Type::Text},
    {"created","created",Type::Integer}};

inline std::int64_t Number(size_t n) { return static_cast<std::int64_t>(n); }
inline bool FitsInteger(const Json& j) {
    if (j.is_number_unsigned())
        return j.get<std::uint64_t>() <= static_cast<std::uint64_t>(
            std::numeric_limits<std::int64_t>::max());
    return j.is_number_integer();
}
inline bool Matches(const Json& j, Type type) {
    switch (type) {
        case Type::Integer: return FitsInteger(j);
        case Type::Text: return j.is_string();
        case Type::Boolean: return j.is_boolean();
    }
    return false;
}
inline void Bind(Statement& q, int& index, const Value& value) {
    if (std::holds_alternative<std::nullptr_t>(value)) q.BindNull(index++);
    else if (const auto* s = std::get_if<std::string>(&value)) q.Bind(index++,*s);
    else q.Bind(index++,std::get<std::int64_t>(value));
}
inline void BindKeys(Statement& q, const Keys& keys) {
    int i=1; for (const auto& value : keys) Bind(q,i,value);
}
inline void Run(Connection& db, const std::string& sql, const Keys& keys) {
    auto q=db.Prepare(sql.c_str()); BindKeys(q,keys); q.Execute();
}
inline std::string Select(const Fields& fields) {
    std::string out;
    for (const auto& f:fields) { if(!out.empty()) out+=","; out+=f.column; }
    if(!out.empty()) out+=",";
    return out+"extension_json";
}
inline void BindFields(Statement& q, int& i, Json& extra, const Fields& fields) {
    for (const auto& f:fields) {
        auto it=extra.find(f.key);
        if (it==extra.end() || !Matches(*it,f.type)) { q.BindNull(i++); continue; }
        if (f.type==Type::Text) q.Bind(i++,it->get<std::string>());
        else if (f.type==Type::Boolean) q.Bind(i++,std::int64_t(it->get<bool>()));
        else q.Bind(i++,it->get<std::int64_t>());
        extra.erase(it);
    }
}
inline Json DecodeFields(Statement& q, int first, const Fields& fields) {
    Json out=Json::parse(q.Text(first+static_cast<int>(fields.size())));
    if(!out.is_object()) throw Database::Error(SQLITE_CORRUPT,"Invalid extension object");
    int i=first;
    for(const auto& f:fields) {
        if(!q.IsNull(i)) {
            if(out.contains(f.key)) throw Database::Error(SQLITE_CORRUPT,"Conflicting extension field");
            if(f.type==Type::Text) out[f.key]=q.Text(i);
            else if(f.type==Type::Boolean) out[f.key]=q.Integer(i)!=0;
            else out[f.key]=q.Integer(i);
        }
        ++i;
    }
    return out;
}
// Identifiers are internal literals only; all data values are bound.
inline void InsertRecord(Connection& db, const std::string& table,
                         const std::string& keyColumns, const Keys& keys,
                         const Fields& fields, Json extra,
                         const std::string& trailingColumns = {},
                         const Keys& trailing = {}) {
    // Fixed shapes, not just syntactically valid identifiers.
    const bool allowed=
        (table=="component_payloads"&&keyColumns=="kind,component_id,flags"&& &fields==&rootFields)||
        (table=="build_specializations"&&keyColumns=="kind,component_id,ordinal,traits_present"&& &fields==&specFields)||
        (table=="skill_bars"&&keyColumns=="kind,component_id,bar,utilities_present"&& &fields==&barFields)||
        (table=="gear_slots"&&keyColumns=="kind,component_id,slot,flags"&& &fields==&gearFields)||
        (table=="infusion_requirements"&&keyColumns=="kind,component_id,ordinal"&& &fields==&requirementFields)||
        (table=="loadouts"&&keyColumns=="id,ordinal,fashion_id,modes_present,equipment_set_id"&& &fields==&metadataFields);
    if(!allowed||(!trailingColumns.empty()&&(table!="gear_slots"||trailingColumns!="raw_slot_json")))
        throw Database::Error(SQLITE_MISUSE,"Unapproved relational SQL shape");
    std::string sql="INSERT INTO "+table+"("+keyColumns;
    for(const auto& f:fields) sql+=","+std::string(f.column);
    sql+=",extension_json";
    if(!trailingColumns.empty()) sql+=","+trailingColumns;
    sql+=") VALUES(";
    const auto count=keys.size()+fields.size()+1+trailing.size();
    for(size_t i=0;i<count;++i) sql+=(i?",?":"?");
    sql+=")";
    auto q=db.Prepare(sql.c_str());
    int i=1; for(const auto& v:keys) Bind(q,i,v);
    BindFields(q,i,extra,fields);
    q.Bind(i++,extra.dump());
    for(const auto& v:trailing) Bind(q,i,v);
    q.Execute();
}
inline Json Pull(Json& parent, const char* key,
                 const std::function<bool(const Json&)>& predicate) {
    auto it=parent.find(key);
    if(it==parent.end() || !predicate(*it)) return nullptr;
    Json value=std::move(*it);
    parent.erase(it);
    return value;
}
inline Json PullObject(Json& j,const char* key) {
    return Pull(j,key,[](const Json& v){return v.is_object();});
}
inline Json PullArray(Json& j,const char* key,const std::function<bool(const Json&)>& element) {
    return Pull(j,key,[&](const Json& v){
        return v.is_array()&&std::all_of(v.begin(),v.end(),element);
    });
}
inline Json Integers(Json& j,const char* key) {return PullArray(j,key,FitsInteger);}
inline Json Strings(Json& j,const char* key) {
    return PullArray(j,key,[](const Json& v){return v.is_string();});
}
inline Json Objects(Json& j,const char* key) {
    return PullArray(j,key,[](const Json& v){return v.is_object();});
}
inline Keys Append(Keys keys, Value value) { keys.push_back(std::move(value)); return keys; }

inline void WriteIntegers(Connection& db,const std::string& sql,Keys keys,const Json& array) {
    if(array.is_null()) return;
    auto q=db.Prepare(sql.c_str());
    for(size_t n=0;n<array.size();++n) {
        int i=1; for(const auto& v:keys) Bind(q,i,v);
        q.Bind(i++,Number(n)); q.Bind(i,array[n].get<std::int64_t>());
        q.Execute(); q.Reset();
    }
}
inline void WriteStrings(Connection& db,const std::string& sql,Keys keys,const Json& array) {
    if(array.is_null()) return;
    auto q=db.Prepare(sql.c_str());
    for(size_t n=0;n<array.size();++n) {
        int i=1; for(const auto& v:keys) Bind(q,i,v);
        q.Bind(i++,Number(n)); q.Bind(i,array[n].get<std::string>());
        q.Execute(); q.Reset();
    }
}
inline Json ReadList(Connection& db,const std::string& sql,const Keys& keys,Type type) {
    auto q=db.Prepare(sql.c_str()); BindKeys(q,keys);
    Json array=Json::array();
    while(q.Step()) {
        if(q.Integer(0)!=Number(array.size()))
            throw Database::Error(SQLITE_CORRUPT,"Invalid stored list ordering");
        if(type==Type::Text) array.push_back(q.Text(1));
        else array.push_back(q.Integer(1));
    }
    return array;
}

enum RootFlag : std::int64_t { Specs=1, Pets=2, Legends=4, Weapons=8, Gear=16,
                               Requirements=32, Stats=64 };
inline std::int64_t Present(const Json& j,std::int64_t bit) {return j.is_null()?0:bit;}

inline void WritePayload(Connection& db,const std::string& kind,const Composable::Component& c) {
    const Keys key={kind,c.id};
    Json root=c.data;
    auto specs=Objects(root,"specializations");
    auto pets=Integers(root,"pets"), legends=Integers(root,"legend_codes"),
         weapons=Integers(root,"weapons");
    auto terrestrial=PullObject(root,"skills"), aquatic=PullObject(root,"aquatic_skills");
    auto gear=PullObject(root,"gear");
    auto req=Objects(root,"infusion_totals");
    auto stats=Pull(root,"infusion_stat_totals",[](const Json& v) {
        return v.is_object()&&std::all_of(v.begin(),v.end(),FitsInteger);
    });
    auto flags=Present(specs,Specs)|Present(pets,Pets)|Present(legends,Legends)|
        Present(weapons,Weapons)|Present(gear,Gear)|Present(req,Requirements)|Present(stats,Stats);
    InsertRecord(db,"component_payloads","kind,component_id,flags",Append(key,flags),
                 rootFields,std::move(root));
    if(!specs.is_null()) for(size_t i=0;i<specs.size();++i) {
        Json spec=specs[i]; auto traits=Integers(spec,"traits");
        const auto sk=Append(key,Number(i));
        InsertRecord(db,"build_specializations","kind,component_id,ordinal,traits_present",
                     Append(sk,Present(traits,1)),specFields,std::move(spec));
        WriteIntegers(db,"INSERT INTO build_traits VALUES(?,?,?,?,?)",sk,traits);
    }
    for(const auto& entry:std::vector<std::pair<std::string,Json>>{
        {"pets",pets},{"legend_codes",legends},{"weapons",weapons}})
        WriteIntegers(db,"INSERT INTO build_choices VALUES(?,?,?,?,?)",Append(key,entry.first),entry.second);
    for(auto& entry:std::vector<std::pair<std::string,Json>>{
        {"skills",std::move(terrestrial)},{"aquatic_skills",std::move(aquatic)}}) {
        if(entry.second.is_null()) continue;
        auto utilities=Integers(entry.second,"utilities"); auto bk=Append(key,entry.first);
        InsertRecord(db,"skill_bars","kind,component_id,bar,utilities_present",
                     Append(bk,Present(utilities,1)),barFields,std::move(entry.second));
        WriteIntegers(db,"INSERT INTO utility_skills VALUES(?,?,?,?,?)",bk,utilities);
    }
    if(!gear.is_null()) for(auto slot=gear.begin();slot!=gear.end();++slot) {
        auto gk=Append(key,slot.key());
        if(!slot.value().is_object()) {
            InsertRecord(db,"gear_slots","kind,component_id,slot,flags",Append(gk,std::int64_t(0)),
                         gearFields,Json::object(),"raw_slot_json",{slot.value().dump()});
            continue;
        }
        Json item=slot.value(); auto ids=Integers(item,"infusion_ids"),
            attrs=Strings(item,"infusion_attributes");
        InsertRecord(db,"gear_slots","kind,component_id,slot,flags",
                     Append(gk,Present(ids,1)|Present(attrs,2)),gearFields,std::move(item));
        WriteIntegers(db,"INSERT INTO gear_infusion_ids VALUES(?,?,?,?,?)",gk,ids);
        WriteStrings(db,"INSERT INTO gear_infusion_attributes VALUES(?,?,?,?,?)",gk,attrs);
    }
    if(!req.is_null()) for(size_t i=0;i<req.size();++i)
        InsertRecord(db,"infusion_requirements","kind,component_id,ordinal",Append(key,Number(i)),
                     requirementFields,req[i]);
    if(!stats.is_null()) for(auto it=stats.begin();it!=stats.end();++it)
        Run(db,"INSERT INTO infusion_stats VALUES(?,?,?,?)",
            {kind,c.id,it.key(),it.value().get<std::int64_t>()});
}

inline Json ReadPayload(Connection& db,const std::string& kind,const std::string& id) {
    const Keys key={kind,id};
    auto q=db.Prepare(("SELECT flags,"+Select(rootFields)+
        " FROM component_payloads WHERE kind=? AND component_id=?").c_str());
    BindKeys(q,key);
    if(!q.Step()) throw Database::Error(SQLITE_CORRUPT,"Component payload missing");
    const auto flags=q.Integer(0); Json root=DecodeFields(q,1,rootFields);
    if(q.Step()) throw Database::Error(SQLITE_CORRUPT,"Duplicate component payload");
    if(flags&Specs) {
        auto s=db.Prepare(("SELECT ordinal,traits_present,"+Select(specFields)+
            " FROM build_specializations WHERE kind=? AND component_id=? ORDER BY ordinal").c_str());
        BindKeys(s,key); Json specs=Json::array();
        while(s.Step()) {
            const auto ordinal=s.Integer(0);
            if(ordinal!=Number(specs.size())) throw Database::Error(SQLITE_CORRUPT,"Specialization order");
            auto spec=DecodeFields(s,2,specFields);
            if(s.Integer(1)) spec["traits"]=ReadList(db,
                "SELECT ordinal,selection FROM build_traits WHERE kind=? AND component_id=? AND spec_ordinal=? ORDER BY ordinal",
                Append(key,ordinal),Type::Integer);
            specs.push_back(std::move(spec));
        }
        root["specializations"]=std::move(specs);
    }
    for(const auto& field:std::vector<std::pair<std::string,std::int64_t>>{
        {"pets",Pets},{"legend_codes",Legends},{"weapons",Weapons}})
        if(flags&field.second) root[field.first]=ReadList(db,
            "SELECT ordinal,value FROM build_choices WHERE kind=? AND component_id=? AND field=? ORDER BY ordinal",
            Append(key,field.first),Type::Integer);
    {
        auto s=db.Prepare(("SELECT bar,utilities_present,"+Select(barFields)+
            " FROM skill_bars WHERE kind=? AND component_id=?").c_str()); BindKeys(s,key);
        while(s.Step()) {
            auto bar=s.Text(0); auto value=DecodeFields(s,2,barFields);
            if(s.Integer(1)) value["utilities"]=ReadList(db,
                "SELECT ordinal,skill_id FROM utility_skills WHERE kind=? AND component_id=? AND bar=? ORDER BY ordinal",
                Append(key,bar),Type::Integer);
            root[bar]=std::move(value);
        }
    }
    if(flags&Gear) {
        auto s=db.Prepare(("SELECT slot,flags,"+Select(gearFields)+
            ",raw_slot_json FROM gear_slots WHERE kind=? AND component_id=? ORDER BY slot").c_str());
        BindKeys(s,key); Json gear=Json::object();
        while(s.Step()) {
            const auto slot=s.Text(0); const int rawColumn=3+static_cast<int>(gearFields.size());
            if(!s.IsNull(rawColumn)) {gear[slot]=Json::parse(s.Text(rawColumn));continue;}
            auto item=DecodeFields(s,2,gearFields); const auto gf=s.Integer(1);
            if(gf&1) item["infusion_ids"]=ReadList(db,
                "SELECT ordinal,item_id FROM gear_infusion_ids WHERE kind=? AND component_id=? AND slot=? ORDER BY ordinal",
                Append(key,slot),Type::Integer);
            if(gf&2) item["infusion_attributes"]=ReadList(db,
                "SELECT ordinal,attribute FROM gear_infusion_attributes WHERE kind=? AND component_id=? AND slot=? ORDER BY ordinal",
                Append(key,slot),Type::Text);
            gear[slot]=std::move(item);
        }
        root["gear"]=std::move(gear);
    }
    if(flags&Requirements) {
        auto s=db.Prepare(("SELECT ordinal,"+Select(requirementFields)+
            " FROM infusion_requirements WHERE kind=? AND component_id=? ORDER BY ordinal").c_str());
        BindKeys(s,key); Json req=Json::array();
        while(s.Step()) {
            if(s.Integer(0)!=Number(req.size())) throw Database::Error(SQLITE_CORRUPT,"Infusion requirement order");
            req.push_back(DecodeFields(s,1,requirementFields));
        }
        root["infusion_totals"]=std::move(req);
    }
    if(flags&Stats) {
        auto s=db.Prepare("SELECT attribute,total FROM infusion_stats WHERE kind=? AND component_id=?");
        BindKeys(s,key); Json stats=Json::object();
        while(s.Step()) stats[s.Text(0)]=s.Integer(1);
        root["infusion_stat_totals"]=std::move(stats);
    }
    return root;
}

inline CharacterIdentity::Provenance ReadProvenance(Statement& q,int first){
    CharacterIdentity::Provenance p{q.Text(first),q.Text(first+1),
        {q.Text(first+2),q.Text(first+3),q.Text(first+4)}};
    if(!p.Valid())throw Database::Error(SQLITE_CORRUPT,"Invalid stored character provenance");
    return p;
}
inline void AppendProvenance(Keys& keys,const CharacterIdentity::Provenance& p){
    if(!p.Valid())throw Database::Error(SQLITE_CONSTRAINT,"Invalid character provenance");
    keys.insert(keys.end(),{p.account,p.name,p.evidence.created,p.evidence.profession,p.evidence.race});
}
inline void WriteLoadout(Connection& db,const Composable::Loadout& l,size_t ordinal) {
    Run(db,"DELETE FROM loadouts WHERE id=?",{l.id});
    Json metadata=l.metadata; metadata.erase("id");
    auto modes=Strings(metadata,"game_modes");
    std::optional<CharacterIdentity::Provenance> source;
    if(metadata.contains("source_identity")){
        source=CharacterIdentity::DecodeProvenance(metadata.at("source_identity"));
        if(!source)throw Database::Error(SQLITE_CONSTRAINT,"Invalid Loadout source identity");
        metadata.erase("source_identity");
    }
    Value fashion=l.fashion_id.empty()?Value(nullptr):Value(l.fashion_id);
    Value set=l.equipment_set_id.empty()?Value(nullptr):Value(l.equipment_set_id);
    InsertRecord(db,"loadouts","id,ordinal,fashion_id,modes_present,equipment_set_id",
                 {l.id,Number(ordinal),fashion,Present(modes,1),set},metadataFields,std::move(metadata));
    if(source){
        Keys keys{l.id};AppendProvenance(keys,*source);
        Run(db,"INSERT INTO loadout_source_identity VALUES(?,?,?,?,?,?)",keys);
    }
    WriteStrings(db,"INSERT INTO loadout_game_modes VALUES(?,?,?)",{l.id},modes);
    for(const auto& ref:l.refs)
        Run(db,"INSERT INTO loadout_components VALUES(?,?,?)",{l.id,ref.first,ref.second});
}

inline Composable::Loadout ReadLoadout(Connection& db,Statement& q,bool provenance=true) {
    Composable::Loadout l; l.id=q.Text(0); l.fashion_id=q.Text(1);
    l.equipment_set_id=q.Text(3);
    l.metadata=DecodeFields(q,4,metadataFields); l.metadata["id"]=l.id;
    if(provenance){
        auto p=db.Prepare("SELECT account,character,created,profession,race FROM loadout_source_identity WHERE loadout_id=?");
        BindKeys(p,{l.id});
        if(p.Step()){
            if(l.metadata.contains("source_identity"))throw Database::Error(SQLITE_CORRUPT,"Conflicting source identity");
            l.metadata["source_identity"]=CharacterIdentity::EncodeProvenance(ReadProvenance(p,0));
        }
    }
    if(q.Integer(2)) l.metadata["game_modes"]=ReadList(db,
        "SELECT ordinal,mode FROM loadout_game_modes WHERE loadout_id=? ORDER BY ordinal",{l.id},Type::Text);
    auto r=db.Prepare("SELECT kind,component_id FROM loadout_components WHERE loadout_id=?");
    BindKeys(r,{l.id}); while(r.Step()) l.refs.emplace(r.Text(0),r.Text(1));
    return l;
}

struct WardrobeSlot {
    const char* name;
    std::uint16_t DecodedWardrobeLink::*skin;
    std::uint16_t (DecodedWardrobeLink::*dyes)[4];
};
inline const std::array<WardrobeSlot,15> wardrobeSlots={{
    {"aquabreather",&DecodedWardrobeLink::aquabreather_skin,nullptr},
    {"backpack",&DecodedWardrobeLink::backpack_skin,&DecodedWardrobeLink::backpack_dyes},
    {"chest",&DecodedWardrobeLink::chest_skin,&DecodedWardrobeLink::chest_dyes},
    {"boots",&DecodedWardrobeLink::boots_skin,&DecodedWardrobeLink::boots_dyes},
    {"gloves",&DecodedWardrobeLink::gloves_skin,&DecodedWardrobeLink::gloves_dyes},
    {"helm",&DecodedWardrobeLink::helm_skin,&DecodedWardrobeLink::helm_dyes},
    {"leggings",&DecodedWardrobeLink::leggings_skin,&DecodedWardrobeLink::leggings_dyes},
    {"shoulders",&DecodedWardrobeLink::shoulders_skin,&DecodedWardrobeLink::shoulders_dyes},
    {"outfit",&DecodedWardrobeLink::outfit_id,&DecodedWardrobeLink::outfit_dyes},
    {"aquatic_a",&DecodedWardrobeLink::aquatic_weapon_a,nullptr},
    {"aquatic_b",&DecodedWardrobeLink::aquatic_weapon_b,nullptr},
    {"weapon_a_main",&DecodedWardrobeLink::weapon_a_main,nullptr},
    {"weapon_a_off",&DecodedWardrobeLink::weapon_a_off,nullptr},
    {"weapon_b_main",&DecodedWardrobeLink::weapon_b_main,nullptr},
    {"weapon_b_off",&DecodedWardrobeLink::weapon_b_off,nullptr}
}};
inline void WriteFashion(Connection& db,const Fashion::Look& l,size_t ordinal) {
    DecodedWardrobeLink w{};
    if(!Fashion::DecodeValid(l.wardrobe_link,w))
        throw Database::Error(SQLITE_CONSTRAINT,"Invalid Fashion wardrobe link");
    Run(db,"INSERT INTO fashion VALUES(?,?,?,?,?,?,?,?,?,?,?,?) ON CONFLICT(id) DO UPDATE SET "
        "ordinal=excluded.ordinal,name=excluded.name,notes=excluded.notes,weight=excluded.weight,"
        "origin=excluded.origin,source_character=excluded.source_character,source_tab=excluded.source_tab,"
        "complete=excluded.complete,created=excluded.created,visibility_flags=excluded.visibility_flags,extension_json=excluded.extension_json",
        {l.id,Number(ordinal),l.name,l.notes,l.weight,l.origin,l.source_character,
         std::int64_t(l.source_tab),std::int64_t(l.complete),std::int64_t(l.created),std::int64_t(w.visibility_flags),l.extension.dump()});
    Run(db,"DELETE FROM fashion_skins WHERE fashion_id=?",{l.id});
    Run(db,"DELETE FROM fashion_characters WHERE fashion_id=?",{l.id});
    Run(db,"DELETE FROM fashion_character_identity WHERE fashion_id=?",{l.id});
    const auto writeIdentity=[&](int64_t role,size_t ordinal,const CharacterIdentity::Provenance& p){
        Keys keys{l.id,role,Number(ordinal)};AppendProvenance(keys,p);
        Run(db,"INSERT INTO fashion_character_identity VALUES(?,?,?,?,?,?,?,?)",keys);
    };
    if(l.source_identity)writeIdentity(0,0,*l.source_identity);
    for(size_t i=0;i<l.associations.size();++i)writeIdentity(1,i,l.associations[i]);
    for(const auto& slot:wardrobeSlots) {
        const std::string name=slot.name;
        Run(db,"INSERT INTO fashion_skins VALUES(?,?,?)",{l.id,name,std::int64_t(w.*(slot.skin))});
        if(slot.dyes) for(size_t i=0;i<4;++i)
            Run(db,"INSERT INTO fashion_dyes VALUES(?,?,?,?)",
                {l.id,name,Number(i),std::int64_t((w.*(slot.dyes))[i])});
    }
    WriteStrings(db,"INSERT INTO fashion_characters VALUES(?,?,?)",{l.id},Json(l.characters));
}
inline Fashion::Look ReadFashion(Connection& db,Statement& q,bool provenance=true) {
    Fashion::Look l; l.id=q.Text(0);l.name=q.Text(1);l.notes=q.Text(2);
    l.weight=q.Text(3);l.origin=q.Text(4);l.source_character=q.Text(5);
    l.source_tab=static_cast<int>(q.Integer(6));l.complete=q.Integer(7)!=0;
    l.created=static_cast<std::time_t>(q.Integer(8));
    DecodedWardrobeLink w{};w.visibility_flags=static_cast<std::uint16_t>(q.Integer(9));
    l.extension=Json::parse(q.Text(10));
    if(!l.extension.is_object()) throw Database::Error(SQLITE_CORRUPT,"Invalid Fashion extension");
    auto s=db.Prepare("SELECT slot,skin_id FROM fashion_skins WHERE fashion_id=?");BindKeys(s,{l.id});
    size_t count=0;
    while(s.Step()) {
        const auto name=s.Text(0);
        auto slot=std::find_if(wardrobeSlots.begin(),wardrobeSlots.end(),
            [&](const WardrobeSlot& v){return name==v.name;});
        if(slot==wardrobeSlots.end()) throw Database::Error(SQLITE_CORRUPT,"Unknown Fashion slot");
        w.*(slot->skin)=static_cast<std::uint16_t>(s.Integer(1));++count;
        auto dyes=ReadList(db,"SELECT ordinal,dye_id FROM fashion_dyes WHERE fashion_id=? AND slot=? ORDER BY ordinal",
                           {l.id,name},Type::Integer);
        if(dyes.size()!=(slot->dyes?4u:0u)) throw Database::Error(SQLITE_CORRUPT,"Invalid Fashion dyes");
        if(slot->dyes) for(size_t i=0;i<4;++i) (w.*(slot->dyes))[i]=dyes[i].get<std::uint16_t>();
    }
    if(count!=wardrobeSlots.size()) throw Database::Error(SQLITE_CORRUPT,"Incomplete Fashion slots");
    l.wardrobe_link=ChatLink::EncodeWardrobe(w);
    l.characters=ReadList(db,"SELECT ordinal,character FROM fashion_characters WHERE fashion_id=? ORDER BY ordinal",
                          {l.id},Type::Text).get<std::vector<std::string>>();
    if(provenance){
        auto p=db.Prepare("SELECT role,ordinal,account,character,created,profession,race FROM fashion_character_identity WHERE fashion_id=? ORDER BY role,ordinal");
        BindKeys(p,{l.id});
        while(p.Step()){
            auto source=ReadProvenance(p,2);
            if(p.Integer(0)==0){
                if(l.source_identity||p.Integer(1)!=0)throw Database::Error(SQLITE_CORRUPT,"Invalid Fashion source");
                l.source_identity=std::move(source);
            }else{
                if(p.Integer(1)!=Number(l.associations.size())||!Fashion::AssociateVerified(l,source))
                    throw Database::Error(SQLITE_CORRUPT,"Invalid Fashion character association");
            }
        }
    }
    return l;
}

} // namespace AlterEgo::NativeLibrary::Codec
