#include "../src/AccountIdentity.h"
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
    using namespace AlterEgo::Identity;
    assert(MatchesReply({"A.B"},{"A.B"},true,false));
    assert(!MatchesReply({"A.B"},{"A_B"},true,true));
    assert(!MatchesReply({"A.B"},{""},true,true));
    assert(!MatchesReply({""},{""},false,true));
    assert(!MatchesReply({"A.B"},{""},false,false));
    assert(MatchesReply({"A.B"},{""},false,true));
    assert(ExactIds({1,2,3},{3,2,1}));
    assert(!ExactIds({1,2,3},{1,2,2}));
    assert(!ExactIds({1,2,3},{1,2}));
    assert(!ExactIds({1,2,3},{4,5,6}));
    const Character hero{{"A.B"},"Hero"}, other{{"A_B"},"Hero"};
    assert(!(hero==other));
    return 0;
}
