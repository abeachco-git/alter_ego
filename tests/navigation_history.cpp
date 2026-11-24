#include "../src/NavigationHistory.h"
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
    h.Clear();assert(h.Empty());return 0;
}
