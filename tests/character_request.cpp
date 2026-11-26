#include "../src/AccountCommitGate.h"
#include <thread>
#include <future>
#include "../src/AccountIdentity.h"
#include "../src/CharacterRequest.h"
#include "../src/GameMemoryCore.h"
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
    using namespace AlterEgo::GameMemory;
    const unsigned char code[]={0x68,0x65,0x6c,0x6c,0x6f};
    const unsigned char changed[]={0x68,0x65,0x6c,0x6c,0x6e};
    assert(CodeFingerprint(code,sizeof(code))==0xa430d84680aabd0bull);
    assert(CodeFingerprint(code,sizeof(code))!=CodeFingerprint(changed,sizeof(changed)));
    assert(CodeFingerprint(code,sizeof(code))!=CodeFingerprint(code,sizeof(code)-1));
    assert(PlausibleCharacter(1,1,0,0));
    assert(PlausibleCharacter(80,9,4,1));
    assert(!PlausibleCharacter(0,1,0,0));
    assert(!PlausibleCharacter(81,1,0,0));
    assert(!PlausibleCharacter(80,0,0,0));
    assert(!PlausibleCharacter(80,10,0,0));
    assert(!PlausibleCharacter(80,1,5,0));
    assert(!PlausibleCharacter(80,1,0,2));
    assert(EligibleTarget(true,false,true,true,"Hero","Other","A.B","A.B"));
    assert(EligibleTarget(true,false,true,true,"Hero","Other","",""));
    assert(!EligibleTarget(true,false,true,true,"Hero","Other","A.B","A_B"));
    assert(!EligibleTarget(false,false,true,true,"Hero","Other","A.B","A.B"));
    assert(!EligibleTarget(true,true,true,true,"Hero","Other","A.B","A.B"));
    assert(!EligibleTarget(true,false,false,true,"Hero","Other","A.B","A.B"));
    assert(!EligibleTarget(true,false,true,false,"Hero","Other","A.B","A.B"));
    assert(!EligibleTarget(true,false,true,true,"Hero","Hero","A.B","A.B"));
    // Replacing a credential cannot interleave between identity check and commit.
    std::mutex credential;
    unsigned epoch=1;
    std::promise<void> entered,release,changing;
    auto released=release.get_future();
    std::thread writer([&]{
        assert(AlterEgo::AccountCommit::Run(credential,[&]{return epoch==1;},[&]{
            entered.set_value();released.wait();assert(epoch==1);return true;
        }));
    });
    entered.get_future().wait();
    std::thread replacement([&]{changing.set_value();std::lock_guard<std::mutex> lock(credential);++epoch;});
    changing.get_future().wait();release.set_value();writer.join();replacement.join();
    assert(!AlterEgo::AccountCommit::Run(credential,[&]{return epoch==1;},[]{assert(false);return true;}));
    assert(AlterEgo::AccountCommit::Run(credential,[&]{return epoch==2;},[]{return true;}));
    // A queued request must retain its admitted generation, even if the
    // same account is unlinked and linked again before the worker starts.
    const auto queuedEpoch=epoch;
    {std::lock_guard<std::mutex> lock(credential);++epoch;}
    assert(!AlterEgo::AccountCommit::Run(credential,[&]{return queuedEpoch==epoch;},[]{assert(false);return true;}));
    // Snapshot identity is captured together with its contents. A subsequent
    // selection change cannot redirect an admitted persistence operation.
    std::string selected="A.B";
    std::map<std::string,std::set<int>> persisted;
    std::set<int> owned{42,43};
    const auto captured=std::make_pair(selected,owned);
    selected="A_B";owned={99};
    persisted[captured.first]=captured.second;
    assert(persisted.at("A.B")==std::set<int>({42,43}));
    assert(!persisted.count("A_B"));
    // The same gate covers successful inventory data AND failed attempts.
    // An obsolete response cannot advance retry timestamps or replace rows.
    const auto inventoryEpoch=epoch;
    int rows=42,attempt=10;
    {std::lock_guard<std::mutex> lock(credential);++epoch;}
    assert(!AlterEgo::AccountCommit::Run(credential,[&]{return inventoryEpoch==epoch;},[&]{
        rows=99;attempt=20;return true;
    }));
    assert(!AlterEgo::AccountCommit::Run(credential,[&]{return inventoryEpoch==epoch;},[&]{
        attempt=21;return true;
    }));
    assert(rows==42&&attempt==10);
    assert(AlterEgo::AccountCommit::Run(credential,[&]{return epoch==inventoryEpoch+1;},[&]{
        rows=7;attempt=22;return true;
    }));
    assert(rows==7&&attempt==22);
    return 0;
}
