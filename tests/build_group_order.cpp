#include "../src/BuildGroupOrder.h"
#include <cassert>
#include <string>
#include <vector>
struct Build { std::string id; std::string profession; };
std::string Ids(const std::vector<Build>& v) {
    std::string text;
    for (const auto& b : v) text += b.id + ",";
    return text;
}
int case_build_group_order(int, char**) {
    using namespace AlterEgo::BuildGroupOrder;
    std::vector<Build> v = {
        {"N1","Necromancer"}, {"E1","Engineer"}, {"N2","Necromancer"},
        {"G1","Guardian"}, {"E2","Engineer"}
    };
    assert((Groups(v) == std::vector<std::string>{"Necromancer","Engineer","Guardian"}));
    assert(!MoveGroup(v, "Necromancer", "Necromancer", false));
    assert(!MoveGroup(v, "Invalid", "Engineer", true));
    assert(MoveGroup(v, "Engineer", "Necromancer", false));
    assert(Ids(v) == "E1,E2,N1,N2,G1,");
    assert(MoveGroup(v, "Engineer", "Guardian", true));
    assert(Ids(v) == "N1,N2,G1,E1,E2,");
    assert(!MoveWithinGroup(v, "N1", "G1", true));
    assert(!MoveWithinGroup(v, "N2", "N2", true));
    assert(MoveWithinGroup(v, "N1", "N2", true));
    assert(Ids(v) == "N2,N1,G1,E1,E2,");
    assert(MoveWithinGroup(v, "E2", "E1", false));
    assert(Ids(v) == "N2,N1,G1,E2,E1,");
    assert(!MoveWithinGroup(v, "missing", "E1", true));
    assert(v.size() == 5);
    return 0;
}
