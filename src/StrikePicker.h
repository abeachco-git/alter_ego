#pragma once
// Weekly Strike presentation metadata. Operates on the original API bit name,
// not the shortened label rendered by StrikeDisplayName.
#include <string>
#include <unordered_map>
#include "ClearsRotation.h"

namespace AlterEgo {

// Both public achievement names and the UI's abbreviated "Bears" label
// must be recognized. These strings all represent the same Icebrood Saga boss.
inline bool IsBearsStrike(const std::string& key) {
    return key == "bears" ||
           key == "voiceandclawofthefallen" ||
           key == "voiceofthefallenandclawofthefallen" ||
           key == "voiceandclaw" ||
           key == "voiceofthefallenclawofthefallen";
}

inline const char* StrikePickerCategory(const std::string& name) {
    const std::string key = ClearsRotation::Normalize(name);
    if (key == "oldlionscourt") return "Core Game";
    if (IsBearsStrike(key) || key == "shiverpeakspass" ||
        key == "fraenirofjormag" || key == "boneskinner" ||
        key == "whisperofjormag" || key == "coldwar") return "Icebrood Saga";
    if (key == "aetherbladehideout" || key == "xunlaijadejunkyard" ||
        key == "kainengoverlook" || key == "harvesttemple") return "End of Dragons";
    if (key == "cosmicobservatory" || key == "templeoffebe") return "Secrets of the Obscure";
    if (key == "guardiansglade" || key == "kela") return "Janthir Wilds";
    if (key == "nexusofeternity" || key == "vloxx") return "Visions of Eternity";
    return "Strike Mission";
}

inline int StrikePickerOrder(const std::string& name) {
    const std::string key = ClearsRotation::Normalize(name);
    // Bears is first, while Old Lion's Court is the last tracked strike
    // (immediately before the separate, untracked Nexus UI cell).
    if (IsBearsStrike(key)) return 10;
    if (key == "oldlionscourt") return 1000;
    static const std::unordered_map<std::string, int> ranks = {
        {"shiverpeakspass", 20}, {"fraenirofjormag", 22},
        {"boneskinner", 23}, {"whisperofjormag", 24}, {"coldwar", 26},
        {"aetherbladehideout", 30}, {"xunlaijadejunkyard", 31},
        {"kainengoverlook", 32}, {"harvesttemple", 33},
        {"cosmicobservatory", 40}, {"templeoffebe", 41},
        {"guardiansglade", 50}, {"kela", 50}, {"nexusofeternity", 60}
    };
    const auto it = ranks.find(key);
    return it != ranks.end() ? it->second : 100;
}

} // namespace AlterEgo
