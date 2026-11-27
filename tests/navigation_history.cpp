#include "../src/NavigationHistory.h"
#include "../src/EditorLifecycle.h"
#include "../src/RecordSelectionCore.h"
#include <vector>
#include <algorithm>
#include <utility>
#include <cassert>
#include <string>
#include <tuple>
struct NavTestState {
    std::string page,id,filter;
    bool SameDestination(const NavTestState& o)const{return page==o.page&&id==o.id;}
};
int case_navigation_history(int,char**){
    AlterEgo::Navigation::History<NavTestState> h;
    assert(!h.CanMove(-1)&&!h.CanMove(1));
    h.Observe({"library","loadout-a",""},false);
    h.Observe({"library","loadout-a","PvE"},false);
    h.Observe({"component","relic-x",""},true);
    assert(h.CanMove(-1)&&!h.CanMove(1));
    assert(h.Move(-1)->id=="loadout-a"&&h.Current()->filter=="PvE");
    assert(h.Move(1)->id=="relic-x");
    h.Move(-1);h.Observe({"library","loadout-a","WvW"},true);
    assert(h.CanMove(1));
    h.Observe({"characters","hero",""},true);
    assert(!h.CanMove(1)&&h.Move(-1)->filter=="WvW");
    h.Observe({"library","deleted-fallback",""},false);
    assert(h.CanMove(1)&&h.Move(1)->id=="hero");
    for(int i=0;i<100;++i)h.Observe({"skins",std::to_string(i),""},true);
    int count=0;while(h.Move(-1))++count;assert(count==63);
    h.Clear();assert(h.Empty());
    // A Compare destination carries the selected account/name pair separately
    // from the last resolved target (including Automatic), saved selection,
    // template tabs and display preferences. History must retain each snapshot.
    {
        struct CompareSnapshot {
            std::string id,kind,second,compareTarget,compareAccount,account;
            int buildTab=0,equipTab=0;
            bool manual=false,showMatches=false,exactItems=false;
            bool SameDestination(const CompareSnapshot& o)const {
                return std::tie(id,kind,second,buildTab,equipTab)==
                       std::tie(o.id,o.kind,o.second,o.buildTab,o.equipTab);
            }
        };
        AlterEgo::Navigation::History<CompareSnapshot> compare;
        CompareSnapshot a;
        a.id="Hero";a.kind="account-a";a.second="saved-a";a.manual=true;
        a.compareTarget=a.id;a.compareAccount=a.kind;a.account="account-a";
        a.buildTab=1;a.equipTab=2;a.showMatches=true;
        auto b=a;b.kind="account-b";b.compareAccount=b.kind;b.account=b.kind;
        b.exactItems=true;
        compare.Observe(a,true);compare.Observe(b,true);
        assert(compare.CanMove(-1));
        const auto* back=compare.Move(-1);
        assert(back&&back->id=="Hero"&&back->kind=="account-a"&&back->compareAccount=="account-a");
        assert(back->compareTarget=="Hero"&&back->account=="account-a"&&back->second=="saved-a");
        assert(back->manual&&back->showMatches&&!back->exactItems&&back->buildTab==1&&back->equipTab==2);
        const auto* forward=compare.Move(1);
        assert(forward&&forward->id=="Hero"&&forward->kind=="account-b"&&forward->compareAccount=="account-b");
        assert(forward->compareTarget=="Hero"&&forward->account=="account-b"&&forward->exactItems);
        auto automatic=b;automatic.id.clear();automatic.kind.clear();automatic.second.clear();automatic.manual=false;
        compare.Observe(automatic,true);
        // Automatic resolution changes are observation updates, not navigation.
        automatic.compareTarget="Other";automatic.compareAccount="account-a";
        compare.Observe(automatic,false);
        back=compare.Move(-1);
        assert(back&&back->id=="Hero"&&back->kind=="account-b"&&back->manual);
        forward=compare.Move(1);
        assert(forward&&forward->id.empty()&&forward->kind.empty()&&!forward->manual&&forward->second.empty());
        assert(forward->compareTarget=="Other"&&forward->compareAccount=="account-a");
        compare.Move(-1);
        b.second="saved-b";compare.Observe(b,true);
        assert(!compare.CanMove(1));
        back=compare.Move(-1);
        assert(back&&back->second=="saved-a"&&back->kind=="account-b");
    }
    using namespace AlterEgo::RecordSelection;
    struct Row{std::string id,name;};
    std::vector<Row> rows={{"a","Duplicate"},{"b","Duplicate"},{"c","Other"}};
    const RevisionTarget target{"b","Duplicate",42};
    assert(ById(rows,target.id)==1&&target.Current(42)&&!target.Current(43));
    std::reverse(rows.begin(),rows.end());rows.erase(rows.begin());
    assert(ById(rows,target.id)==0);
    rows[0].name="Renamed";assert(ById(rows,target.id)==0);
    rows.erase(rows.begin());assert(ById(rows,target.id)==-1);
    std::vector<std::pair<std::string,std::string>> chars={{"account-a","Hero"},{"account-b","Hero"}};
    const auto key=chars[1];assert(IndexOf(chars,key,[](const auto& row){return row;})==1);
    std::reverse(chars.begin(),chars.end());assert(IndexOf(chars,key,[](const auto& row){return row;})==0);
    using namespace AlterEgo::EditorLifecycle;
    assert(RequestClose(false,true)==CloseDecision::KeepOpen);
    assert(RequestClose(true,false)==CloseDecision::Close);
    assert(RequestClose(true,true)==CloseDecision::ConfirmDiscard);
    assert(RequestClose(true,true,true)==CloseDecision::KeepOpen);
    assert(!CanSave(true,false)&&CanSave(true,true)&&CanSave(true,false,true));
    assert(!CanSave(false,true)&&!CanSave(true,true,false,true));
    assert(!CanReplaceEditor(true)&&!CanReplaceEditor(false,true)&&CanReplaceEditor(false));
    // A parent close must account for a dirty child, even if the parent itself is clean.
    assert(RequestClose(true,false||true)==CloseDecision::ConfirmDiscard);
    return 0;
}
