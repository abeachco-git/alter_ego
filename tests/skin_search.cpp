#include "../src/SkinSearchCore.h"
#include "../src/CommercePriceCore.h"
#include "../src/WikiArtworkCore.h"
#include "../src/WikiFreshness.h"
#include "../src/SkinFilters.h"
#include "../src/SplitPanelCore.h"
#include "../src/CatalogRowCore.h"
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
 // A fractional font size must not introduce cumulative clipper/cursor drift.
 assert(AlterEgo::UI::CatalogRowPitch(13.f)==22.f);
 assert(AlterEgo::UI::CatalogRowPitch(16.f)==22.f);
 assert(AlterEgo::UI::CatalogRowPitch(16.25f)==23.f);
 assert(AlterEgo::UI::CatalogRowPitch(23.75f)==30.f);
 for(float font:{13.f,16.25f,23.75f}){
     const auto pitch=AlterEgo::UI::CatalogRowPitch(font);
     float y=17.f;
     for(int i=0;i<1000;++i)y=std::floor(y+pitch);
     assert(y==17.f+1000*pitch);
 }
 // Grid line has one 36px tile line and exactly 3px spacing;
 // no extra dummy/newline. Verify alignment after long scroll seeks.
 const auto gridPitch=AlterEgo::UI::CatalogGridPitch(36.f,3.f);
 assert(gridPitch==39.f);
 for(int cols:{1,2,5,10}){
     float y=17.f;
     for(int row=0;row<1000;++row){
         // SameLine restores the preceding tile's Y for every column.
         for(int col=0;col<cols;++col)assert(std::floor(y+36.f+3.f)==y+gridPitch);
         y=std::floor(y+36.f+3.f);
     }
     assert(y==17.f+1000*gridPitch);
 }
 assert(AlterEgo::UI::CatalogCenteredScroll(0,39.f,400.f)==0.f);
 assert(AlterEgo::UI::CatalogCenteredScroll(100,39.f,400.f)==3719.5f);
 assert(AlterEgo::UI::CatalogCenteredScroll(100,23.f,400.f)==2111.5f);
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
 assert(AlterEgo::Search::MatchesFields(Search::Words("  NECRO  fractal "),{"Fractal Power","Necromancer"}));
 assert(!AlterEgo::Search::MatchesFields(Search::Words("necro raid"),{"Fractal Power","Necromancer"}));
 assert(Search::Matches("Superior Scholar Rune",Search::Words("schloar rune")));
 assert(!Search::Matches("Superior Scholar Rune",Search::Words("schloar rune"),false));
 assert(Search::Matches("Éclair",Search::Words("Éclair")));
 assert(!Search::Matches("Éclair",Search::Words("Écliar")));
 PriceInfo a,b;
 a.item_id=5;a.tradeable=true;a.sell_price=100;a.sell_quantity=2;
 b.item_id=4;b.tradeable=true;b.sell_price=50;b.sell_quantity=0;
 assert(BetterPrice(a,b));assert(!BetterPrice(b,a));
 b.sell_quantity=1;assert(BetterPrice(b,a));assert(!BetterPrice(a,b));
 a.sell_price=50;assert(BetterPrice(b,a));
 // Exercise the same batch-selection function used at both dispatch and
 // post-claim recheck. Empty output previously caused every fetch to vanish.
 {
     const int64_t now=7200,ttl=3600;
     std::unordered_map<uint32_t,PriceInfo> cache;
     const std::vector<uint32_t> batch{1,2,3,4,5,0};
     assert((SelectPriceRequests(batch,cache,false,now,ttl)==std::vector<uint32_t>{1,2,3,4,5}));
     assert((SelectPriceRequests({1},cache,true,now,ttl)==std::vector<uint32_t>{1}));
     cache[1].fetched_at_unix=now;cache[2].fetched_at_unix=now-ttl;
     cache[3].fetched_at_unix=now+1;cache[4].fetched_at_unix=0;
     cache[5].fetched_at_unix=now-ttl+1;
     assert((SelectPriceRequests(batch,cache,false,now,ttl)==std::vector<uint32_t>{2,3,4}));
     assert((SelectPriceRequests(batch,cache,true,now,ttl)==std::vector<uint32_t>{1,2,3,4,5}));
     assert(SelectPriceRequests({1,5},cache,false,now,ttl).empty());
     const auto planned=SelectPriceRequests({2},cache,false,now,ttl);
     assert((planned==std::vector<uint32_t>{2}));
     cache[2].fetched_at_unix=now; // another owner publishes before claim
     assert(SelectPriceRequests(planned,cache,false,now,ttl).empty());
     assert((SelectPriceRequests(planned,cache,true,now,ttl)==std::vector<uint32_t>{2}));
     std::vector<uint32_t> bulk;for(uint32_t id=100;id<300;++id)bulk.push_back(id);
     assert(SelectPriceRequests(bulk,cache,false,now,ttl)==bulk);
 }
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
    {
        namespace W=AlterEgo::WikiArtwork;
        const auto q=W::Query(200,R"({"query":{"pages":{"7":{"pageid":7,"images":[{"title":"File:Other.jpg"},{"title":"File:Abaddon Pistol.png"},{"title":"File:Abaddon Pistol.jpg"},{"title":"File:Abaddon Pistol icon.png"}]}}}})");
        assert(W::BestFile(q,"Abaddon Pistol")=="File:Abaddon Pistol.jpg");
        const auto armor=W::Query(200,R"({"query":{"pages":{"7":{"pageid":7,"images":[{"title":"File:Condition Damage.png"},{"title":"File:Accursed Leggings.png"},{"title":"File:Accursed armor human female front.jpg"},{"title":"File:Accursed armor human female back.jpg"}]}}}})");
        const auto g=W::ReadGallery("{{Armor infobox\n| set = [[Accursed armor]]\n}}\n<gallery>\nFile:Accursed armor human female front.jpg|Front\n</gallery>");
        assert(g.set=="Accursed armor"&&g.files.size()==1);
        assert(W::ReadGallery("{{Armor gallery|set=Accursed armor}}").set=="Accursed armor");
        auto files=W::RankedFiles(armor,"Accursed Leggings",g);
        assert(files.front()=="File:Accursed armor human female front.jpg");
        assert(std::find(files.begin(),files.end(),"File:Condition Damage.png")==files.end());
        auto onlyStat=W::Query(200,R"({"query":{"pages":{"7":{"pageid":7,"images":[{"title":"File:Condition Damage.png"},{"title":"File:Gold coin.png"}]}}}})");
        assert(W::BestFile(onlyStat,"Accursed Leggings").empty());
        const auto gallery=W::ReadGallery("{{Skin infobox\n|gallery1 = Exact.jpg\n}}");
        assert(gallery.files.size()==1&&gallery.files[0]=="File:Exact.jpg");
        const auto full=W::Query(200,R"({"query":{"pages":{"7":{"pageid":7,"title":"File:Accursed armor human female front.jpg","imageinfo":[{"url":"https://wiki.guildwars2.com/images/a/ab/Accursed_armor.jpg","width":420,"height":802,"sha1":"abc123"}]}}}})");
        auto source=W::ImageSource(full,"Accursed Leggings",100);
        assert(source&&W::ValidSource(*source)&&!W::Due(*source,100));
        auto restored=W::ReadSource(W::SourceJson(*source));assert(W::SameRevision(restored,*source));
        assert(W::Due(restored,100+AlterEgo::WikiFreshness::ArtworkInterval));
        restored.version=1;assert(W::Due(restored,101));
        assert(!W::SafeRevision("../x")&&!W::PreviewSize(64,64)&&W::PreviewSize(40,400));
        auto tiny=full;tiny["query"]["pages"]["7"]["imageinfo"][0]["width"]=64;tiny["query"]["pages"]["7"]["imageinfo"][0]["height"]=64;
        assert(!W::ImageSource(tiny,"Accursed Leggings",100));
        const auto missing=W::Query(200,R"({"query":{"pages":{"-1":{"missing":""}}}})");
        assert(W::ImageUrl(missing).empty()&&W::BestFile(missing,"Missing").empty());
        const auto img=W::Query(200,R"({"query":{"pages":{"7":{"pageid":7,"imageinfo":[{"url":"https://wiki.guildwars2.com/images/a/ab/Abaddon_Pistol.jpg"}]}}}})");
        assert(!W::ImageUrl(img).empty());
        for(int status:{0,403,429,503}){bool failed=false;try{W::Query(status,"{}");}catch(...){failed=true;}assert(failed);}
        bool failed=false;try{W::Query(200,R"({"error":{"code":"maxlag"}})");}catch(...){failed=true;}assert(failed);
        failed=false;try{W::BestFile(W::Query(200,R"({"continue":{"imcontinue":"more"},"query":{"pages":{"7":{"pageid":7}}}})"),"Missing");}catch(...){failed=true;}assert(failed);
        using AlterEgo::WikiFreshness::ArtworkRemaining;
        assert(ArtworkRemaining(100,101,false)==899&&ArtworkRemaining(100,1000,false)==0);
        assert(ArtworkRemaining(100,1000,true)>0&&ArtworkRemaining(100,99,true)==0);
    }
    return 0;
}
