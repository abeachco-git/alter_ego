#pragma once

// One canonical definition for the GW2 stat combinations behind named ascended
// equipment.  These crafted names vary between armor and weapons.  Do not use
// string similarity to infer unknown stats: unmatched names remain unverified.
// Primary mapping: MetaBattle "How to Gear a Character" ascended weapon/armor
// table, cross-checked against GW2 itemstat names.
#include <cctype>
#include <string>

namespace AlterEgo {
namespace NamedStats {

inline std::string NormalizeKey(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (i + 2 < text.size() && c == 0xe2 &&
            static_cast<unsigned char>(text[i + 1]) == 0x80 &&
            (static_cast<unsigned char>(text[i + 2]) == 0x99 ||
             static_cast<unsigned char>(text[i + 2]) == 0x98)) {
            out.push_back('\'');
            i += 2;
        } else if (c < 128) {
            out.push_back(static_cast<char>(std::tolower(c)));
        } else {
            out.push_back(static_cast<char>(c));
        }
    }
    const auto first = out.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    out = out.substr(first, out.find_last_not_of(" \t\r\n") - first + 1);
    return out;
}

inline std::string CanonicalStatName(const std::string& name) {
    const auto key = NormalizeKey(name);
    if (key.empty()) return {};

    // The first column is an ascended weapon/armor prefix OR a stat name.
    // Every alias appears only once so ambiguities are rejected rather than
    // choosing a stat through a partial/substring match.
    static const struct { const char* alias; const char* stat; } entries[] = {
        {"apothecary", "Apothecary"}, {"ebonmane", "Apothecary"}, {"veldrunner", "Apothecary"},
        {"assassin", "Assassin"}, {"soros", "Assassin"}, {"saphir", "Assassin"},
        {"berserker", "Berserker"}, {"zojja", "Berserker"},
        {"bringer", "Bringer"}, {"giftbringer", "Bringer"},
        {"carrion", "Carrion"}, {"occam", "Carrion"},
        {"cavalier", "Cavalier"}, {"angchu", "Cavalier"},
        {"celestial", "Celestial"}, {"wupwup", "Celestial"},
        {"cleric", "Cleric"}, {"theodosus", "Cleric"}, {"tateos", "Cleric"},
        {"commander", "Commander"}, {"tizlak", "Commander"},
        {"crusader", "Crusader"}, {"ossa", "Crusader"},
        {"dire", "Dire"}, {"mathilde", "Dire"}, {"morbach", "Dire"},
        {"diviner", "Diviner"}, {"steelstar", "Diviner"},
        {"dragon", "Dragon"}, {"suun", "Dragon"},
        {"giver", "Giver"}, {"tixx", "Giver"},
        {"grieving", "Grieving"}, {"the twins", "Grieving"},
        {"harrier", "Harrier"}, {"zehtuka", "Harrier"},
        {"knight", "Knight"}, {"beigarth", "Knight"},
        {"magi", "Magi"}, {"hronk", "Magi"},
        {"marauder", "Marauder"}, {"svaard", "Marauder"},
        {"marshal", "Marshal"}, {"nadijeh", "Marshal"},
        {"minstrel", "Minstrel"}, {"maklain", "Minstrel"},
        {"nomad", "Nomad"}, {"ventari", "Nomad"},
        {"plaguedoctor", "Plaguedoctor"}, {"nerashi", "Plaguedoctor"},
        {"rabid", "Rabid"}, {"grizzlemouth", "Rabid"}, {"ferratus", "Rabid"},
        {"rampager", "Rampager"}, {"coalforge", "Rampager"}, {"forgemaster", "Rampager"},
        {"ritualist", "Ritualist"}, {"togo", "Ritualist"},
        {"sentinel", "Sentinel"}, {"tonn", "Sentinel"}, {"wei qi", "Sentinel"},
        {"seraph", "Seraph"}, {"thackeray", "Seraph"},
        {"settler", "Settler"}, {"leftpaw", "Settler"},
        {"shaman", "Shaman"}, {"zintl", "Shaman"},
        {"sinister", "Sinister"}, {"verata", "Sinister"},
        {"soldier", "Soldier"}, {"chorben", "Soldier"}, {"ahamid", "Soldier"},
        {"trailblazer", "Trailblazer"}, {"pahua", "Trailblazer"},
        {"valkyrie", "Valkyrie"}, {"stonecleaver", "Valkyrie"}, {"gobrech", "Valkyrie"},
        {"vigilant", "Vigilant"}, {"laranthir", "Vigilant"},
        {"viper", "Viper"}, {"yassith", "Viper"},
        {"wanderer", "Wanderer"}, {"ruka", "Wanderer"},
        {"zealot", "Zealot"}, {"keeper", "Zealot"}
    };
    for (const auto& entry : entries) {
        const std::string alias(entry.alias);
        if (key == alias || key == alias + "'s" || key == alias + "'")
            return entry.stat;
        // A full crafted name such as "Yassith's Vambraces" or
        // "Zojja's Claymore" is also an unambiguous stat prefix.
        if (key.compare(0, alias.size(), alias) == 0) {
            const std::string tail = key.substr(alias.size());
            if (tail.size() >= 3 && tail.compare(0, 3, "'s ") == 0)
                return entry.stat;
            if (tail.size() >= 2 && tail.compare(0, 2, "' ") == 0)
                return entry.stat;
        }
    }
    return {};
}

inline bool SameCanonicalStat(const std::string& a, const std::string& b) {
    const auto first = CanonicalStatName(a);
    return !first.empty() && first == CanonicalStatName(b);
}

} // namespace NamedStats
} // namespace AlterEgo
