#include "../src/AchievementNamesCore.h"
#include <cassert>
#include <map>
using namespace AlterEgo::AchievementNames;

template<class F> static bool rejects(F f) {
    try { f(); return false; } catch (const std::exception&) { return true; }
}
int case_achievement_names(int,char**) {
    const auto ids=ParseIds(Json::array({3,1,2}));
    assert(ids==std::vector<uint32_t>({1,2,3}));
    const std::map<uint32_t,std::string> names{{1,"Known"},{2,""},{4,"Historical"}};
    assert(Plan(ids,names,false)==std::vector<uint32_t>({2,3}));
    assert(Plan(ids,names,true)==ids);
    assert(rejects([]{ParseIds(Json::array());}));
    assert(rejects([]{ParseIds(Json::array({1,1}));}));
    assert(rejects([]{ParseIds(Json::array({-1}));}));
    assert(rejects([]{ParseIds(Json::array({4294967296ull}));}));
    assert(rejects([]{ParseIds(Json::array({1.5}));}));
    const auto good=Json::array({{{"id",2},{"name","Second"}},{{"id",1},{"name","First"}}});
    ValidateBatch({1,2},good);
    assert(rejects([&]{ValidateBatch({1,2,3},good);}));
    assert(rejects([&]{ValidateBatch({1,3},good);}));
    auto bad=good;bad[1]["id"]=2;assert(rejects([&]{ValidateBatch({1,2},bad);}));
    bad=good;bad[1]["name"]="";assert(rejects([&]{ValidateBatch({1,2},bad);}));
    assert(Due(0,100,0,false));
    assert(!Due(0,100,200,true));
    assert(!Due(100,100+RefreshInterval-1,0,true));
    assert(Due(100,100+RefreshInterval,0,true));
    assert(!Due(100,100+RefreshInterval,0,false));
    assert(Due(200,100,0,true));
    return 0;
}
