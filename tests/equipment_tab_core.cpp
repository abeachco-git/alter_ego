#include "../src/EquipmentTabCore.h"
#include <cassert>
#include <map>
#include <string>
#include <vector>
struct EquipItem { int tab = 0; };
struct Character {
    std::vector<EquipItem> equipment;
    std::map<int,std::string> equipment_tab_names;
    int active_equipment_tab = 0;
};
int main() {
    using AlterEgo::EquipmentTabCore::Count;
    Character ch;
    assert(Count(ch) == 1);
    ch.equipment = {{1},{2}};
    assert(Count(ch) == 2);
    ch.equipment.clear();
    ch.equipment_tab_names = {{1,"Armor"},{2,"Raid"}};
    assert(Count(ch) == 2);
    ch.equipment_tab_names.clear();
    ch.active_equipment_tab = 3;
    assert(Count(ch) == 3);
    ch.equipment = {{1},{2}};
    ch.active_equipment_tab = 1;
    assert(Count(ch) == 2);
    ch.equipment_tab_names = {{4,"WvW"}};
    assert(Count(ch) == 4);
}
