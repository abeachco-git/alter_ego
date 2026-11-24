#include "../src/GearEditCore.h"
#include <cassert>
#include <cstdint>
struct Slot {
    std::vector<uint32_t> infusion_ids;
    std::vector<std::string> infusion_attributes;
    bool infusion_snapshot = false;
    std::string infusion, sigil, sigil2;
    uint32_t sigil_id = 0, sigil2_id = 0;
};
int main() {
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
}
