#include "../src/SkinSearchCore.h"
#include <cassert>
int main() {
 using namespace Skinventory;
 assert(Search::Matches("Volcanic Stormcaller Sword",Search::Words("storm sword")));
 assert(Search::Matches("Volcanic Stormcaller Sword",Search::Words("stromcaller")));
 assert(!Search::Matches("Volcanic Stormcaller Sword",Search::Words("axe")));
 PriceInfo a,b;
 a.item_id=5;a.tradeable=true;a.sell_price=100;a.sell_quantity=2;
 b.item_id=4;b.tradeable=true;b.sell_price=50;b.sell_quantity=0;
 assert(BetterPrice(a,b));assert(!BetterPrice(b,a));
 b.sell_quantity=1;assert(BetterPrice(b,a));assert(!BetterPrice(a,b));
 a.sell_price=50;assert(BetterPrice(b,a));
}
