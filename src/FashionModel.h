#pragma once
// Persistent, profession-agnostic wardrobe snapshots for the Fashion collection.
// Live equipment captures are explicitly marked partial: the character API does
// not expose all Fashion Template visibility/outfit settings.
#include "ChatLink.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif

namespace AlterEgo {
namespace Fashion {

struct Look {
    std::string id;
    std::string name;
    std::string notes;
    std::string wardrobe_link;
    std::string weight = "Unknown"; // Light, Medium, Heavy, Any, Unknown
    std::string origin;             // "wardrobe_link" or "live_equipment"
    std::string source_character;
    int source_tab = 0;
    bool complete = true;
    std::vector<std::string> characters;
    std::time_t created = 0;
    nlohmann::json extension = nlohmann::json::object();
};

inline bool DecodeValid(const std::string& input, DecodedWardrobeLink& decoded) {
    std::string payload;
    if (!ChatLink::ExtractPayload(input, payload)) return false;
    // A wardrobe link has exactly 48 16-bit fields after its 0x0F type.
    // ChatLink::DecodeWardrobe is intentionally lenient; library imports
    // must not accept truncated or malformed links.
    for (const char c : payload) {
        if (!std::isalnum(static_cast<unsigned char>(c)) &&
            c != '+' && c != '/' && c != '=') return false;
    }
    const auto raw = ChatLink::Base64Decode(payload);
    if (raw.size() != 97 || raw[0] != LINK_WARDROBE_TMPL) return false;
    return ChatLink::DecodeWardrobe(input, decoded);
}

inline std::string CanonicalLink(const std::string& input) {
    DecodedWardrobeLink decoded{};
    if (!DecodeValid(input, decoded)) return {};
    return ChatLink::EncodeWardrobe(decoded);
}

inline bool ContainsIgnoreCase(std::string haystack, std::string needle) {
    std::transform(haystack.begin(), haystack.end(), haystack.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::transform(needle.begin(), needle.end(), needle.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return haystack.find(needle) != std::string::npos;
}

inline bool MatchesSearch(const Look& look, const std::string& query) {
    if (query.empty() || ContainsIgnoreCase(look.name, query) ||
        ContainsIgnoreCase(look.notes, query) ||
        ContainsIgnoreCase(look.weight, query) ||
        ContainsIgnoreCase(look.source_character, query)) return true;
    for (const auto& character : look.characters)
        if (ContainsIgnoreCase(character, query)) return true;
    return false;
}

inline void Associate(Look& look, const std::string& character) {
    if (character.empty()) return;
    if (std::find(look.characters.begin(), look.characters.end(), character) == look.characters.end())
        look.characters.push_back(character);
}

inline bool HasCapturedSkin(const DecodedWardrobeLink& w) {
    for(auto id:{w.helm_skin,w.shoulders_skin,w.chest_skin,w.gloves_skin,
        w.leggings_skin,w.boots_skin,w.backpack_skin,w.aquabreather_skin,
        w.aquatic_weapon_a,w.aquatic_weapon_b,w.weapon_a_main,w.weapon_a_off,
        w.weapon_b_main,w.weapon_b_off})if(id)return true;
    return false;
}
inline bool MakeLiveLook(const std::string& link,const std::string& name,
    const std::string& character,int tab,Look& out) {
    DecodedWardrobeLink w{};
    if(name.empty()||character.empty()||tab<1||!DecodeValid(link,w)||!HasCapturedSkin(w))return false;
    Look next;
    next.name=name;next.wardrobe_link=ChatLink::EncodeWardrobe(w);
    next.complete=false;next.origin="live_equipment";
    next.source_character=character;next.source_tab=tab;
    Associate(next,character);
    out=std::move(next);return true;
}

inline nlohmann::json ToJson(const Look& look) {
    nlohmann::json out = look.extension;
    if (!out.is_object()) out = nlohmann::json::object();
    out.update({{"id", look.id}, {"name", look.name}, {"notes", look.notes},
            {"wardrobe_link", look.wardrobe_link}, {"weight", look.weight},
            {"origin", look.origin}, {"source_character", look.source_character},
            {"source_tab", look.source_tab}, {"complete", look.complete},
            {"characters", look.characters}, {"created", look.created}});
    return out;
}

inline bool FromJson(const nlohmann::json& j, Look& look) {
    if (!j.is_object()) return false;
    const std::string code = j.value("wardrobe_link", std::string());
    const std::string canonical = CanonicalLink(code);
    if (canonical.empty()) return false;
    look.id = j.value("id", std::string());
    look.name = j.value("name", std::string());
    look.notes = j.value("notes", std::string());
    look.wardrobe_link = canonical;
    look.weight = j.value("weight", std::string("Unknown"));
    look.origin = j.value("origin", std::string("wardrobe_link"));
    look.source_character = j.value("source_character", std::string());
    look.source_tab = j.value("source_tab", 0);
    look.complete = j.value("complete", true);
    look.created = j.value("created", std::time_t(0));
    look.characters.clear();
    if (j.contains("characters") && j["characters"].is_array())
        for (const auto& n : j["characters"])
            if (n.is_string()) Associate(look, n.get<std::string>());
    look.extension = j;
    for (const char* key : {"id","name","notes","wardrobe_link","weight","origin",
                            "source_character","source_tab","complete","characters","created"})
        look.extension.erase(key);
    if (look.id.empty()) return false;
    return true;
}

} // namespace Fashion
} // namespace AlterEgo
