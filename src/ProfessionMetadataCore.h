#pragma once
// Schema-pinned public profession metadata; no UI or network dependencies.
#include "PublicCatalogCache.h"
#include <array>
#include <unordered_map>
#include <tuple>
#include "ProfessionMetadataTypes.h"
namespace AlterEgo {
namespace ProfessionMetadata {
using Json=nlohmann::json;
inline constexpr const char* Schema="2019-12-19T00:00:00.000Z";
inline const std::array<std::string,9>& Names(){
    static const std::array<std::string,9> names={"Guardian","Warrior","Engineer","Ranger","Thief","Elementalist","Mesmer","Necromancer","Revenant"};
    return names;
}
inline uint32_t Code(const std::string& profession){
    const auto& names=Names();const auto it=std::find(names.begin(),names.end(),profession);
    return it==names.end()?0:static_cast<uint32_t>(it-names.begin()+1);
}
inline std::string Key(const std::string& profession){return "public_profession_"+profession;}
inline uint32_t Number(const Json& value,bool allowZero=false){
    if(!value.is_number_integer()||(!value.is_number_unsigned()&&value.get<int64_t>()<0))
        throw std::runtime_error("Invalid profession number");
    const auto id=value.get<uint64_t>();
    if(id>UINT32_MAX||(!allowZero&&!id))throw std::runtime_error("Invalid profession number");
    return static_cast<uint32_t>(id);
}
inline std::string Text(const Json& row,const char* key){
    if(!row.contains(key)||row.at(key).is_null())return {};
    return row.at(key).get<std::string>();
}
struct WeaponSkill{uint32_t id=0;std::string slot,offhand,attunement;};
struct Weapon{
    uint32_t specialization=0;
    std::vector<std::string> flags;
    std::vector<WeaponSkill> skills;
};
struct Record{
    std::string id;
    uint32_t code=0;
    ProfessionInfo info;
    // Ordered raw pairs preserve legitimate aliases and legacy mapping order.
    std::vector<std::pair<uint16_t,uint32_t>> palettes;
    std::map<std::string,Weapon> weaponRows;
    std::map<std::string,ProfessionWeaponData> weapons;
    std::unordered_map<uint16_t,uint32_t> paletteToSkill;
    std::unordered_map<uint32_t,uint16_t> skillToPalette;
    std::set<uint16_t> ambiguousPalettes;
    std::set<uint32_t> ambiguousSkills;
    std::set<std::tuple<std::string,std::string,std::string,int>> ambiguousWeaponSlots;
};
inline void Project(Record& r){
    if(!Code(r.id)||r.code!=Code(r.id)||r.info.name.empty()||r.info.specialization_ids.empty()||
       r.palettes.empty()||r.weaponRows.empty())throw std::runtime_error("Incomplete profession");
    std::set<uint32_t> specs;
    for(auto id:r.info.specialization_ids)if(!id||!specs.insert(id).second)throw std::runtime_error("Invalid specialization membership");
    r.paletteToSkill.clear();r.skillToPalette.clear();r.ambiguousPalettes.clear();r.ambiguousSkills.clear();r.ambiguousWeaponSlots.clear();r.weapons.clear();
    for(const auto& [palette,skill]:r.palettes){
        if(!skill)throw std::runtime_error("Invalid skill mapping");
        const auto forward=r.paletteToSkill.find(palette);
        if(forward!=r.paletteToSkill.end()&&forward->second!=skill)r.ambiguousPalettes.insert(palette);
        const auto reverse=r.skillToPalette.find(skill);
        if(reverse!=r.skillToPalette.end()&&reverse->second!=palette)r.ambiguousSkills.insert(skill);
        // Preserve the existing API-order projection. Contextual decoding of
        // underwater/legend aliases is not part of this metadata migration.
        r.paletteToSkill[palette]=skill;r.skillToPalette[skill]=palette;
    }
    for(const auto& [name,w]:r.weaponRows){
        if(name.empty()||w.flags.empty()||w.skills.empty())throw std::runtime_error("Incomplete weapon metadata");
        ProfessionWeaponData d;d.specialization=w.specialization;
        for(const auto& flag:w.flags){
            if(flag=="Mainhand")d.mainhand=true;
            else if(flag=="Offhand")d.offhand=true;
            else if(flag=="TwoHand")d.two_handed=true;
            else if(flag.empty())throw std::runtime_error("Empty weapon flag");
        }
        bool hasSlot=false;
        for(const auto& skill:w.skills){
            if(!skill.id||skill.slot.empty())throw std::runtime_error("Invalid weapon skill");
            const auto& slot=skill.slot;
            if(slot.size()!=8||slot.compare(0,7,"Weapon_")!=0||slot[7]<'1'||slot[7]>'5')continue;
            const int index=slot[7]-'1';hasSlot=true;
            auto put=[&](uint32_t& projected,const std::string& attunement,const std::string& offhand){
                if(projected&&projected!=skill.id)r.ambiguousWeaponSlots.emplace(name,attunement,offhand,index);
                projected=skill.id;
            };
            if(!skill.attunement.empty())put(d.attunement_skills[skill.attunement][index],skill.attunement,{});
            else if(!skill.offhand.empty()&&index==2)put(d.dual_wield[skill.offhand],{},skill.offhand);
            else put(d.skills[index],{},{});
        }
        if(!hasSlot)throw std::runtime_error("Missing weapon slots");
        r.weapons.emplace(name,std::move(d));
    }
}
inline Record Parse(const std::string& expected,const Json& raw){
    if(!raw.is_object()||raw.at("id").get<std::string>()!=expected)throw std::runtime_error("Unexpected profession identity");
    Record r;r.id=expected;r.code=Number(raw.at("code"));
    r.info.name=raw.at("name").get<std::string>();r.info.icon_url=Text(raw,"icon");r.info.icon_big_url=Text(raw,"icon_big");
    if(!raw.at("specializations").is_array()||!raw.at("skills_by_palette").is_array()||!raw.at("weapons").is_object())
        throw std::runtime_error("Missing profession scope");
    for(const auto& id:raw.at("specializations"))r.info.specialization_ids.push_back(Number(id));
    for(const auto& pair:raw.at("skills_by_palette")){
        if(!pair.is_array()||pair.size()!=2)throw std::runtime_error("Invalid palette pair");
        const auto palette=Number(pair[0],true);if(palette>UINT16_MAX)throw std::runtime_error("Palette out of range");
        r.palettes.emplace_back(static_cast<uint16_t>(palette),Number(pair[1]));
    }
    for(const auto& [name,row]:raw.at("weapons").items()){
        Weapon w;
        if(row.contains("specialization")&&!row.at("specialization").is_null())w.specialization=Number(row.at("specialization"),true);
        if(!row.at("flags").is_array()||!row.at("skills").is_array())throw std::runtime_error("Incomplete weapon");
        for(const auto& flag:row.at("flags"))w.flags.push_back(flag.get<std::string>());
        for(const auto& skill:row.at("skills"))w.skills.push_back({Number(skill.at("id")),skill.at("slot").get<std::string>(),Text(skill,"offhand"),Text(skill,"attunement")});
        r.weaponRows.emplace(name,std::move(w));
    }
    Project(r);return r;
}
struct Stamps{int64_t success=0,attempt=0;};
inline bool Due(const Stamps& s,int64_t now,bool manual){return PublicCatalog::Due(s.success,s.attempt,now,manual,PublicCatalog::BalanceMetadataAge);}
inline Stamps ReadStamps(Database::Connection& db,const std::string& prof){const auto key=Key(prof);return {PublicCatalog::Fresh(db,key),PublicCatalog::Fresh(db,key+"_attempt")};}
// Call within the cache transaction. Existing snapshots survive any failure.
inline void Store(Database::Connection& db,const Record& record,int64_t stamp){
    auto r=record;Project(r);
    if(stamp<=0||stamp<ReadStamps(db,r.id).success)throw std::runtime_error("Older profession snapshot");
    auto del=db.Prepare("DELETE FROM public_professions WHERE id=?");del.Bind(1,r.id);del.Execute();
    auto p=db.Prepare("INSERT INTO public_professions VALUES(?,?,?,?,?,?)");
    p.Bind(1,r.id);p.Bind(2,r.code);p.Bind(3,r.info.name);p.Bind(4,r.info.icon_url);p.Bind(5,r.info.icon_big_url);p.Bind(6,std::string(Schema));p.Execute();
    auto spec=db.Prepare("INSERT INTO public_profession_specs VALUES(?,?,?)");int ordinal=0;
    for(auto id:r.info.specialization_ids){spec.Bind(1,r.id);spec.Bind(2,ordinal++);spec.Bind(3,id);spec.Execute();spec.Reset();}
    auto pal=db.Prepare("INSERT INTO public_profession_palettes VALUES(?,?,?,?)");ordinal=0;
    for(const auto& [palette,skill]:r.palettes){pal.Bind(1,r.id);pal.Bind(2,ordinal++);pal.Bind(3,static_cast<int>(palette));pal.Bind(4,skill);pal.Execute();pal.Reset();}
    auto weapon=db.Prepare("INSERT INTO public_profession_weapons VALUES(?,?,?)");
    auto flag=db.Prepare("INSERT INTO public_profession_weapon_flags VALUES(?,?,?,?)");
    auto skill=db.Prepare("INSERT INTO public_profession_weapon_skills VALUES(?,?,?,?,?,?,?)");
    for(const auto& [name,w]:r.weaponRows){
        weapon.Bind(1,r.id);weapon.Bind(2,name);weapon.Bind(3,w.specialization);weapon.Execute();weapon.Reset();ordinal=0;
        for(const auto& f:w.flags){flag.Bind(1,r.id);flag.Bind(2,name);flag.Bind(3,ordinal++);flag.Bind(4,f);flag.Execute();flag.Reset();}
        ordinal=0;
        for(const auto& s:w.skills){skill.Bind(1,r.id);skill.Bind(2,name);skill.Bind(3,ordinal++);skill.Bind(4,s.id);skill.Bind(5,s.slot);skill.Bind(6,s.offhand);skill.Bind(7,s.attunement);skill.Execute();skill.Reset();}
    }
    PublicCatalog::Mark(db,Key(r.id),stamp);
    auto clearAttempt=db.Prepare("DELETE FROM reference_freshness WHERE dataset=?");
    clearAttempt.Bind(1,Key(r.id)+"_attempt");clearAttempt.Execute();
}
inline std::optional<Record> Read(Database::Connection& db,const std::string& id){
    auto p=db.Prepare("SELECT code,name,icon,icon_big,schema_version FROM public_professions WHERE id=?");p.Bind(1,id);
    if(!p.Step()||p.Text(4)!=Schema)return std::nullopt;
    Record r;r.id=id;r.code=static_cast<uint32_t>(p.Integer(0));r.info.name=p.Text(1);r.info.icon_url=p.Text(2);r.info.icon_big_url=p.Text(3);
    auto spec=db.Prepare("SELECT spec FROM public_profession_specs WHERE profession=? ORDER BY ordinal");spec.Bind(1,id);
    while(spec.Step())r.info.specialization_ids.push_back(static_cast<uint32_t>(spec.Integer(0)));
    auto pal=db.Prepare("SELECT palette,skill FROM public_profession_palettes WHERE profession=? ORDER BY ordinal");pal.Bind(1,id);
    while(pal.Step())r.palettes.emplace_back(static_cast<uint16_t>(pal.Integer(0)),static_cast<uint32_t>(pal.Integer(1)));
    auto w=db.Prepare("SELECT weapon,specialization FROM public_profession_weapons WHERE profession=?");w.Bind(1,id);
    while(w.Step())r.weaponRows[w.Text(0)].specialization=static_cast<uint32_t>(w.Integer(1));
    auto f=db.Prepare("SELECT weapon,flag FROM public_profession_weapon_flags WHERE profession=? ORDER BY weapon,ordinal");f.Bind(1,id);
    while(f.Step())r.weaponRows.at(f.Text(0)).flags.push_back(f.Text(1));
    auto s=db.Prepare("SELECT weapon,skill,slot,offhand,attunement FROM public_profession_weapon_skills WHERE profession=? ORDER BY weapon,ordinal");s.Bind(1,id);
    while(s.Step())r.weaponRows.at(s.Text(0)).skills.push_back({static_cast<uint32_t>(s.Integer(1)),s.Text(2),s.Text(3),s.Text(4)});
    Project(r);return r;
}
} // namespace ProfessionMetadata
} // namespace AlterEgo
