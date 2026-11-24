#include "../src/CharacterRequest.h"
#include <cassert>
int case_character_request(int,char**) {
    using namespace AlterEgo::CharacterRequest;
    ReplyGate gate;
    gate.Arm("A.B","/core");
    assert(!gate.Claim("A_B","/core",true,false));
    assert(!gate.Claim("A.B","/equipment",true,false));
    assert(gate.Claim("A.B","/core",true,false));
    assert(!gate.Claim("A.B","/core",true,false));
    gate.Arm("","/core");
    assert(!gate.Claim("A.B","/core",true,false));
    assert(gate.Claim("A.B","/core",true,true));
    gate.Arm("A.B","/core");
    assert(!gate.Claim("","/core",false,false));
    assert(gate.Claim("","/core",false,true));
    Roster r{{"A.B",{"Hero"}},{"Empty",{}}};
    assert(!VerifiedAbsent(r,"A.B","Hero"));
    assert(VerifiedAbsent(r,"A.B","Deleted"));
    assert(VerifiedAbsent(r,"Empty","Deleted"));
    assert(!VerifiedAbsent(r,"A_B","Deleted"));
    assert(!VerifiedAbsent(r,"","Deleted"));
    return 0;
}
