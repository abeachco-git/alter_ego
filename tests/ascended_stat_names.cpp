#include "../src/AscendedStatNames.h"
#include "../src/BuildCompareCore.h"
#include <cassert>
#include <string>
#include <vector>

int case_ascended_stat_names(int, char**) {
    using AlterEgo::NamedStats::CanonicalStatName;
    using AlterEgo::NamedStats::SameCanonicalStat;
    const struct { const char* stat; const char* weapon; const char* armor; } names[] = {
        {"Apothecary", "Ebonmane", "Veldrunner"},
        {"Assassin", "Soros", "Saphir"},
        {"Berserker", "Zojja", "Zojja"},
        {"Bringer", "Giftbringer", "Giftbringer"},
        {"Carrion", "Occam", "Occam"},
        {"Cavalier", "Angchu", "Angchu"},
        {"Celestial", "Wupwup", "Wupwup"},
        {"Cleric", "Theodosus", "Tateos"},
        {"Commander", "Tizlak", "Tizlak"},
        {"Crusader", "Ossa", "Ossa"},
        {"Dire", "Mathilde", "Morbach"},
        {"Diviner", "Steelstar", "Steelstar"},
        {"Dragon", "Suun", "Suun"},
        {"Giver", "Tixx", "Tixx"},
        {"Grieving", "The Twins", "The Twins"},
        {"Harrier", "Zehtuka", "Zehtuka"},
        {"Knight", "Beigarth", "Beigarth"},
        {"Magi", "Hronk", "Hronk"},
        {"Marauder", "Svaard", "Svaard"},
        {"Marshal", "Nadijeh", "Nadijeh"},
        {"Minstrel", "Maklain", "Maklain"},
        {"Nomad", "Ventari", "Ventari"},
        {"Plaguedoctor", "Nerashi", "Nerashi"},
        {"Rabid", "Grizzlemouth", "Ferratus"},
        {"Rampager", "Coalforge", "Forgemaster"},
        {"Ritualist", "Togo", "Togo"},
        {"Sentinel", "Tonn", "Wei Qi"},
        {"Seraph", "Thackeray", "Thackeray"},
        {"Settler", "Leftpaw", "Leftpaw"},
        {"Shaman", "Zintl", "Zintl"},
        {"Sinister", "Verata", "Verata"},
        {"Soldier", "Chorben", "Ahamid"},
        {"Trailblazer", "Pahua", "Pahua"},
        {"Valkyrie", "Stonecleaver", "Gobrech"},
        {"Vigilant", "Laranthir", "Laranthir"},
        {"Viper", "Yassith", "Yassith"},
        {"Wanderer", "Ruka", "Ruka"},
        {"Zealot", "Keeper", "Keeper"}
    };
    for (const auto& row : names) {
        assert(CanonicalStatName(row.stat) == row.stat);
        assert(CanonicalStatName(std::string(row.stat) + "'s") == row.stat);
        for (const auto& alias : {row.weapon, row.armor}) {
            assert(CanonicalStatName(alias) == row.stat);
            assert(CanonicalStatName(std::string(alias) + "'s") == row.stat);
            assert(CanonicalStatName(std::string(alias) + "'s Sword") == row.stat);
            assert(CanonicalStatName(std::string(alias) + "\xE2\x80\x99s Gloves") == row.stat);
            assert(SameCanonicalStat(std::string(alias) + "'s",
                                     std::string(row.stat) + "'s"));
        }
    }
    assert(CanonicalStatName("The Twins' Axe") == "Grieving");
    assert(CanonicalStatName("  YASSITH'S   ") == "Viper");
    assert(CanonicalStatName("unknown prefix") == "");
    assert(CanonicalStatName("Viperwolf's Gloves") == "");
    assert(CanonicalStatName("Zojjas Warhammer") == "");
    const std::vector<std::string> attrs{"Power", "Precision", "Ferocity"};
    using AlterEgo::CompareStatDefinitions;
    using AlterEgo::StatEquivalence;
    assert(CompareStatDefinitions(1, 2, "Zojja's", "Berserker's", attrs, attrs) ==
           StatEquivalence::Matches);
    assert(CompareStatDefinitions(1, 2, "Yassith's", "Viper's", attrs, attrs) ==
           StatEquivalence::Matches);
    assert(CompareStatDefinitions(1, 2, "Zojja's", "Yassith's", attrs, attrs) ==
           StatEquivalence::Different);
    assert(CompareStatDefinitions(1, 2, "Zojja's", "Berserker's", {}, {}) ==
           StatEquivalence::NotVerified);
    assert(CompareStatDefinitions(1, 1, "Zojja's", "Berserker's", {}, {}) ==
           StatEquivalence::Matches);
    return 0;
}
