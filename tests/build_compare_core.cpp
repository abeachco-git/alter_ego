#include "../src/BuildCompareCore.h"
#include <cassert>
int main() {
    AlterEgo::BuildComparison c;
    c.Add("Skills", "Heal", "100", "100", "Skill (name available)", "#100 (name missing)");
    c.Add("Gear", "Helm stat", "161", "168", "Berserker's", "Viper's");
    assert(c.Rows().size() == 2);
    assert(c.DifferenceCount() == 1);
    assert(c.Rows()[0].matches && !c.Rows()[1].matches);
}
