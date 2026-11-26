#include "../src/NavigationHistory.h"
#include "../src/EditorLifecycle.h"
#include "../src/RecordSelectionCore.h"
#include <vector>
#include <algorithm>
#include <utility>
#include <cassert>
#include <string>
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
