#pragma once
#include <array>
#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <vector>
namespace AlterEgo {
namespace ProfessionMetadata {
inline const std::array<std::string,9>& Names(){
    static const std::array<std::string,9> names={"Guardian","Warrior","Engineer","Ranger","Thief","Elementalist","Mesmer","Necromancer","Revenant"};
    return names;
}
inline uint32_t Code(const std::string& profession){
    const auto& names=Names();const auto it=std::find(names.begin(),names.end(),profession);
    return it==names.end()?0:static_cast<uint32_t>(it-names.begin()+1);
}
} // namespace ProfessionMetadata
    struct ProfessionWeaponData {
        bool mainhand = false;
        bool offhand = false;
        bool two_handed = false;
        uint32_t specialization = 0; // 0 = core, else elite spec ID
        uint32_t skills[5] = {0};    // Weapon_1..Weapon_5 skill IDs (default/no qualifier)
        // Thief dual wield: offhand weapon type -> Weapon_3 skill ID
        std::map<std::string, uint32_t> dual_wield;
        // Elementalist attunements: attunement name -> 5 skill IDs
        std::map<std::string, std::array<uint32_t, 5>> attunement_skills;
    };


    struct ProfessionInfo {
        std::string name;
        std::string icon_url;
        std::string icon_big_url;
        std::vector<uint32_t> specialization_ids; // /v2/professions[].specializations (core + elite)
    };


}
