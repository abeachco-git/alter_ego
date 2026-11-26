#include "../src/SkinSearchCore.h"
#include "../src/CommercePriceCore.h"
#include "../src/SkinFilters.h"
#include "../src/SplitPanelCore.h"
#include "../src/LibraryModeFilter.h"
#include <cassert>
int case_skin_search(int, char**) {
 using namespace Skinventory;
 using namespace AlterEgo::LibraryFilter;
 const auto label=[](const std::string& s){return s;};
 const std::vector<std::string> both{"Raid","Fractal"};
 assert(Matches(std::set<std::string>{"Raid","PvP"},both,std::string("PvE"),label));
 assert(!Matches(std::set<std::string>{"PvP"},both,std::string("PvP"),label));
 assert(Matches(std::set<std::string>{"Other"},{},std::string("Other"),label));
 assert(!Matches(std::set<std::string>{"Other"},{},std::string("PvE"),label));
 assert(Matches(std::set<std::string>{},both,std::string("PvE"),label));
 assert(Legacy(4)==std::set<std::string>{"Raid"});
 assert(Legacy(0).empty()&&Legacy(99).empty());
 SkinInfo heavy;heavy.id=1;heavy.name="Heavy Helm";heavy.type="Armor";heavy.weight_class="Heavy";heavy.subtype="Helm";
 SkinInfo medium=heavy;medium.id=2;medium.name="Medium Helm";medium.weight_class="Medium";
 SkinInfo light=heavy;light.id=3;light.name="Light Helm";light.weight_class="Light";
 SkinInfo bow=heavy;bow.id=4;bow.name="Bow";bow.type="Weapon";bow.subtype="ShortBow";
 SkinInfo unnamed=heavy;unnamed.id=5;unnamed.name.clear();
 std::vector<SkinInfo> rows{heavy,medium,light,bow,unnamed,heavy};
 std::unordered_set<uint32_t> owned{1,5};
 SkinFilters filters;filters.category={"Armor"};filters.weight={"Heavy","Medium"};
 auto result=FilterSkins(rows,owned,filters,{},true,true,true);
 assert(result.scope.total==2&&result.scope.owned==1&&result.displayed.size()==2);
 result=FilterSkins(rows,owned,filters,Search::Words("medium"),true,false,true);
 assert(result.scope.total==2&&result.scope.owned==1&&result.searchMatches==1&&result.displayed.size()==1);
 filters={};result=FilterSkins(rows,owned,filters,{},true,true,true);
 assert(result.scope.total==4&&result.scope.owned==1);
 auto facets=CountFacets(rows,owned,true);assert(facets.category.at("Armor").total==3&&facets.weight.at("Heavy").total==1);
 filters.weapon={"Short Bow"};assert(filters.Matches(bow)&&!filters.Matches(heavy));
 filters.weight={"Heavy"};assert(filters.Matches(bow)&&filters.Matches(heavy)&&!filters.Matches(light));
 filters.category={"Armor"};filters.Prune();assert(filters.weapon.empty());
 assert(AlterEgo::UI::FitSplitWidth(420,1000,24,200,240)==420);
 assert(AlterEgo::UI::FitSplitWidth(420,300,24,200,240)==138);
 assert(AlterEgo::UI::FitSplitWidth(420,1000,24,200,240)==420);
 assert(SkinCategorySubtype("Weapon","ShortBow")=="Short Bow");
 assert(SkinCategorySubtype("Weapon","Shortbow")=="Short Bow");
 assert(SkinCategorySubtype("Weapon","Short Bow")=="Short Bow");
 assert(SkinCategorySubtype("Weapon","Speargun")=="Harpoon Gun");
 assert(SkinCategorySubtype("Weapon","Harpoon Gun")=="Harpoon Gun");
 assert(SkinCategorySubtype("Weapon","LongBow")=="Longbow");
 assert(SkinCategorySubtype("Weapon","Longbow")=="Longbow");
 assert(SkinCategorySubtype("Weapon","Harpoon")=="Spear");
 assert(SkinCategorySubtype("Weapon","Spear")=="Spear");
 assert(SkinCategorySubtype("Armor","Helm")=="Helm");
 assert(SkinCategorySubtype("Weapon","Axe")=="Axe");
 assert(Search::Matches("Volcanic Stormcaller Sword",Search::Words("storm sword")));
 assert(Search::Matches("Volcanic Stormcaller Sword",Search::Words("stromcaller")));
 assert(!Search::Matches("Volcanic Stormcaller Sword",Search::Words("axe")));
 PriceInfo a,b;
 a.item_id=5;a.tradeable=true;a.sell_price=100;a.sell_quantity=2;
 b.item_id=4;b.tradeable=true;b.sell_price=50;b.sell_quantity=0;
 assert(BetterPrice(a,b));assert(!BetterPrice(b,a));
 b.sell_quantity=1;assert(BetterPrice(b,a));assert(!BetterPrice(a,b));
 a.sell_price=50;assert(BetterPrice(b,a));
 const std::string record=R"({"id":1,"buys":{"unit_price":100,"quantity":20},"sells":{"unit_price":120,"quantity":3}})";
 auto prices=ParseCommercePrices(200,"["+record+"]",{1},100);
 assert(prices.size()==1&&prices.at(1).buy_price==100&&prices.at(1).sell_price==120);
 assert(ParseCommercePrices(200,"["+record+"]",{1,2},100).empty());
 prices=ParseCommercePrices(206,"["+record+"]",{1,2},100);
 assert(prices.size()==2&&prices.at(1).tradeable&&!prices.at(2).tradeable);
 assert(ParseCommercePrices(206,"[]",{1,2},100).empty());
 assert(ParseCommercePrices(206,"["+record+"]",{1},100).empty());
 assert(ParseCommercePrices(200,"["+record+","+record+"]",{1},100).empty());
 assert(ParseCommercePrices(200,"["+record+"]",{2},100).empty());
 assert(ParseCommercePrices(429,"[]",{1},100).empty());
 assert(ParseCommercePrices(404,R"({"text":"Not found"})",{1},100).empty());
 assert(!ParseCommercePrices(404,R"({"text":"all ids provided are invalid"})",{1},100).at(1).tradeable);
 assert(EstimateInstantSale(250,3420)==726750);
 assert(EstimateInstantSale(1,1)==0);
 assert(!EstimateInstantSale(9223372036854775807LL,2));
    return 0;
}
