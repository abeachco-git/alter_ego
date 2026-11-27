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
    const auto categories=Json::array({{{"id",1},{"name","Category"},{"achievements",Json::array({5,6})}},
        {{"id",2},{"name","Ungrouped"},{"achievements",Json::array()}}});
    const auto groups=Json::array({{{"id","g"},{"name","Group"},{"categories",Json::array({1})}}});
    ValidateGroupCatalog(groups,categories);
    ValidateCatalogIdentities<uint32_t>(Json::array({2,1}),categories);
    ValidateCatalogIdentities<std::string>(Json::array({"g"}),groups);
    assert(rejects([&]{ValidateCatalogIdentities<uint32_t>(Json::array({1,3}),categories);}));
    assert(rejects([&]{ValidateCatalogIdentities<uint32_t>(Json::array({1}),categories);}));
    assert(rejects([&]{ValidateCatalogIdentities<std::string>(Json::array({"other"}),groups);}));
    assert(rejects([&]{ValidateGroupCatalog(groups,Json::array());}));
    auto brokenGroups=groups;brokenGroups[0]["categories"]=Json::array({3});
    assert(rejects([&]{ValidateGroupCatalog(brokenGroups,categories);}));
    auto brokenCategories=categories;brokenCategories[1]["id"]=1;
    assert(rejects([&]{ValidateGroupCatalog(groups,brokenCategories);}));
    brokenCategories=categories;brokenCategories[0]["achievements"]=Json::array({5,5});
    assert(rejects([&]{ValidateGroupCatalog(groups,brokenCategories);}));
    assert(Due(0,100,0,false));
    assert(!Due(0,100,200,true));
    assert(!Due(100,100+RefreshInterval-1,0,true));
    assert(Due(100,100+RefreshInterval,0,true));
    assert(!Due(100,100+RefreshInterval,0,false));
    assert(Due(200,100,0,true));
    return 0;
}
