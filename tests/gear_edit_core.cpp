#include "../src/GearEditCore.h"
#include <cassert>
#include <cstdint>
#include <map>
struct Slot {
    std::vector<uint32_t> infusion_ids;
    std::vector<std::string> infusion_attributes;
    bool infusion_snapshot = false;
    std::string infusion, sigil, sigil2;
    uint32_t sigil_id = 0, sigil2_id = 0, item_id=0, stat_id=0, rune_id=0;
    std::string stat_name,rune,weapon_type;
};
struct Build {
    std::string name,notes,rune_name,relic_name;
    uint32_t rune_id=0,relic_id=0;
    std::map<std::string,Slot> gear;
    std::vector<uint32_t> infusion_totals;
    bool infusion_summary_present=false;
    int infusion_ar_total=0;
    std::map<std::string,int> infusion_stat_totals;
};
int case_gear_edit_core(int, char**) {
    using namespace AlterEgo::GearEdit;
    Slot src, dst;
    src.infusion_ids = {42}; src.infusion_attributes = {"Power"};
    CopyInfusions(dst, src, InfusionCapacity("Ring1", false));
    assert(dst.infusion_ids == std::vector<uint32_t>({42,42,42}));
    assert(dst.infusion_attributes == std::vector<std::string>({"Power","Power","Power"}));
    src.infusion_ids = {42,43}; src.infusion_attributes = {"","Precision"};
    CopyInfusions(dst, src, 3);
    assert(dst.infusion_ids == std::vector<uint32_t>({42,43,42}));
    CopyInfusions(dst, src, InfusionCapacity("Amulet", false));
    assert(dst.infusion_ids.empty());
    src.sigil_id = 1; src.sigil = "Force";
    CopySigils(dst,src,2); assert(dst.sigil_id == 1 && dst.sigil2_id == 1);
    src.sigil2_id = 2; src.sigil2 = "Accuracy";
    CopySigils(dst,src,2); assert(dst.sigil2_id == 2);
    CopySigils(dst,src,1); assert(dst.sigil2_id == 0);
    assert(InfusionCapacity("WeaponB2",false,true) == 0);
    Build parent;
    assert(!HasRecordedAquaticGear(parent.gear));
    parent.gear["HelmAquatic"].infusion_snapshot=true;
    assert(!HasRecordedAquaticGear(parent.gear));
    parent.gear["WeaponAquaticA"].weapon_type="Harpoon";
    assert(HasRecordedAquaticGear(parent.gear));
    parent.gear.erase("WeaponAquaticA");parent.gear["WeaponAquaticB"].infusion_ids={42};
    assert(HasRecordedAquaticGear(parent.gear));
    parent.name="Loadout";parent.notes="Original";parent.gear["Helm"].infusion_ids={42};
    parent.gear["WeaponA2"].sigil_id=1;
    auto child=parent;child.gear["Helm"].infusion_ids={99};child.gear.erase("WeaponA2");
    child.relic_id=7;child.infusion_ar_total=150;
    // Discarding a child leaves all parent fields intact, including the offhand.
    assert(parent.gear.at("Helm").infusion_ids.front()==42&&parent.gear.count("WeaponA2"));
    assert(parent.relic_id==0);
    parent.notes="New notes";
    ApplyEquipmentDraft(parent,child);
    assert(parent.gear.at("Helm").infusion_ids.front()==99&&!parent.gear.count("WeaponA2"));
    assert(parent.relic_id==7&&parent.infusion_ar_total==150);
    assert(parent.name=="Loadout"&&parent.notes=="New notes");
    return 0;
}
