#include "../src/VaultAssistance.h"
#include <cassert>
#include <algorithm>
int case_vault_assistance(int, char**) {
    using namespace AlterEgo::VaultAssist;
    auto a = Match("Complete 3 Events","PvE");
    assert(std::any_of(a.begin(), a.end(), [](const auto& option) { return option.link == "[&BL8MAAA=]"; }));
    assert(std::any_of(a.begin(), a.end(), [](const auto& option) { return option.link == "[&BP8MAAA=]"; }));
    assert(Match("Complete 3 Events","WvW").empty());
    assert(Match("Salvage 10 Items","PvE").empty());
    assert(Match("Complete 10 Events in Queensdale","PvE").empty());
    assert(!Match("Gather 25 Crafting Resources","PvE").empty());
    a = Match("Complete 1 Event in Cantha","PvE");
    assert(!a.empty());
    for (const auto& option : a) assert(option.map != "The Silverwastes");
    assert(Match("Complete a Renown Heart in Janthir","PvE").empty());
    a = Match("Defeat the Inquest Golem Mark II World Boss or Complete 10 Events in Mount Maelstrom","PvE");
    assert(a.size() == 1);
    assert(NextSeconds(a[0], 119 * 60 + 30) == 30);
    assert(NextSeconds(a[0], 120 * 60) == 0);
    assert(NextSeconds(a[0], 120 * 60 + 1) == 179 * 60 + 59);
    a = Match("Complete a Meta-Event or Events in Cantha or 15 Events in Maguuma Jungle","PvE");
    assert(!a.empty());
    Sort(a, 119 * 60);
    assert(a[0].label == "Kaineng Blackout");
    assert(NextSeconds(a.back(),0) == -1);
    a = Match("Defeat Tequatl the Sunless or Complete Events in Sparkfly Fen","PvE");
    assert(NextSeconds(a[0], 23 * 3600) == 3600);
    a = Meta("castora");
    auto event = std::find_if(a.begin(),a.end(),[](const auto& d){return d.label=="Shackles of the Ancients";});
    assert(event!=a.end() && NextSeconds(*event,70*60)==5*60);
    assert(NextSeconds(a.back(),119*60)==60);
    assert(Countdown(61) == "in 2m");
    assert(Countdown(0) == "Scheduled now");
    assert(Countdown(-1) == "Variable start");
    return 0;
}
