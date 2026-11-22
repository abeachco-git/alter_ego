#pragma once

// Snow Crows exposes canonical GW2 IDs in data-armory attributes. Keep this
// parser independent of Nexus, networking and the saved-build store so failures
// never mutate a user's library.
#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <regex>
#include <string>
#include <vector>

namespace AlterEgo {
namespace SnowCrows {

struct Gear {
    uint32_t itemId = 0;
    uint32_t statId = 0;
    std::string statName;
    std::string weaponType;
    std::vector<uint32_t> upgrades;
};
struct Spec {
    uint32_t id = 0;
    std::vector<uint32_t> traits;
};
struct InfusionTotal {
    uint32_t itemId = 0;
    int count = 0;
};
struct Page {
    std::string sourceUrl;
    std::string name;
    std::string profession;
    std::string gameMode;
    std::array<Spec, 3> specs;
    std::array<uint32_t, 5> skills{};
    std::map<std::string, Gear> gear;
    uint32_t relicId = 0;
    std::string relicName;
    uint32_t runeId = 0;
    std::vector<InfusionTotal> infusions;
};

inline std::string NormalizeUrl(std::string url) {
    const auto begin = url.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return {};
    url = url.substr(begin, url.find_last_not_of(" \t\r\n") - begin + 1);
    const std::string http = "http://";
    if (url.compare(0, http.size(), http) == 0) url = "https://" + url.substr(http.size());
    const std::string root = "https://snowcrows.com";
    const std::string www = "https://www.snowcrows.com";
    std::string path;
    if (url.compare(0, root.size(), root) == 0) path = url.substr(root.size());
    else if (url.compare(0, www.size(), www) == 0) path = url.substr(www.size());
    else return {};
    if (path.compare(0, 8, "/builds/") != 0 ||
        path.size() < 17 || path.find_first_of("?#\\ \r\n\t") != std::string::npos ||
        path.find("//") != std::string::npos || path.find("..") != std::string::npos)
        return {};
    // Reject unexpected separators/characters and embedded credentials/ports.
    for (unsigned char ch : path)
        if (!(ch >= 'a' && ch <= 'z') && !(ch >= 'A' && ch <= 'Z') &&
            !(ch >= '0' && ch <= '9') && ch != '/' && ch != '-' && ch != '_')
            return {};
    return root + path;
}
inline std::string Decode(std::string text) {
    for (const auto& pair : {
            std::pair<const char*, const char*>{"&amp;", "&"},
            {"&#039;", "'"}, {"&#39;", "'"}, {"&quot;", "\""},
            {"&lt;", "<"}, {"&gt;", ">"}, {"&nbsp;", " "}}) {
        const std::string from = pair.first, to = pair.second;
        size_t at = 0;
        while ((at = text.find(from, at)) != std::string::npos) {
            text.replace(at, from.size(), to);
            at += to.size();
        }
    }
    return text;
}
inline std::string Match(const std::string& text, const std::string& expression, int group = 1) {
    std::smatch m;
    return std::regex_search(text, m, std::regex(expression, std::regex::icase)) &&
           group >= 0 && group < (int)m.size() ? m[group].str() : "";
}
inline uint32_t Unsigned(const std::string& value) {
    if (value.empty() || value.size() > 10) return 0;
    uint64_t n = 0;
    for (char c : value) {
        if (c < '0' || c > '9') return 0;
        n = n * 10 + (c - '0');
        if (n > UINT32_MAX) return 0;
    }
    return static_cast<uint32_t>(n);
}
inline std::vector<uint32_t> Ids(const std::string& csv) {
    std::vector<uint32_t> result;
    size_t begin = 0;
    while (begin < csv.size()) {
        const size_t end = csv.find(',', begin);
        auto id = Unsigned(csv.substr(begin, end == std::string::npos ? end : end - begin));
        if (id) result.push_back(id);
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return result;
}
inline bool Parse(const std::string& url, const std::string& html, Page& out, std::string& error) {
    error.clear();
    const std::string normalized = NormalizeUrl(url);
    if (normalized.empty()) { error = "Only Snow Crows build URLs are supported."; return false; }
    if (html.size() < 1000 || html.size() > 3 * 1024 * 1024) {
        error = "The downloaded page is empty or unexpectedly large."; return false;
    }

    Page parsed;
    parsed.sourceUrl = normalized;
    parsed.name = Decode(Match(html, R"(Guild Wars 2 ([^"<]+?) Meta build)"));
    if (parsed.name.empty())
        parsed.name = Decode(Match(html, R"(<h1[^>]*>([^<]+)</h1>)"));
    const std::string mode = Match(normalized, R"(snowcrows\.com/builds/([^/]+)/)");
    parsed.gameMode = mode == "raids" ? "Raid" : mode == "fractals" ? "Fractal" :
                      mode == "wvw" ? "WvW" : mode == "pvp" ? "PvP" : "PvE";
    const std::string pathProf = Match(normalized, R"(snowcrows\.com/builds/[^/]+/([^/]+)/)");
    const std::map<std::string, std::string> profs = {
        {"guardian","Guardian"}, {"warrior","Warrior"}, {"engineer","Engineer"},
        {"ranger","Ranger"}, {"thief","Thief"}, {"elementalist","Elementalist"},
        {"mesmer","Mesmer"}, {"necromancer","Necromancer"}, {"revenant","Revenant"}};
    auto prof = profs.find(pathProf);
    if (prof == profs.end()) { error = "Unknown Snow Crows profession."; return false; }
    parsed.profession = prof->second;

    const size_t gearAt = html.find("id=\"gearset\"");
    const size_t skillsAt = html.find("id=\"skills\"", gearAt == std::string::npos ? 0 : gearAt);
    if (gearAt == std::string::npos || skillsAt == std::string::npos || skillsAt <= gearAt) {
        error = "Snow Crows equipment or build section was not found."; return false;
    }
    const std::string gearHtml = html.substr(gearAt, skillsAt - gearAt);
    const std::string skillsHtml = html.substr(skillsAt);
    parsed.skills = {};
    auto skillIds = Ids(Match(skillsHtml, R"SC(data-armory-embed="skills"\s+data-armory-ids="([0-9,]+)")SC"));
    if (skillIds.size() == 5)
        std::copy(skillIds.begin(), skillIds.end(), parsed.skills.begin());

    const std::regex specsRe(R"SC(data-armory-embed="specializations"\s+data-armory-ids="([0-9]+)"\s+data-armory-[0-9]+-traits="([0-9,]*)")SC", std::regex::icase);
    size_t specIndex = 0;
    for (std::sregex_iterator i(skillsHtml.begin(), skillsHtml.end(), specsRe), end; i != end && specIndex < 3; ++i) {
        parsed.specs[specIndex].id = Unsigned((*i)[1].str());
        parsed.specs[specIndex].traits = Ids((*i)[2].str());
        ++specIndex;
    }

    const std::regex rowRe(R"(<tr\b[^>]*>([\s\S]*?)</tr>)", std::regex::icase);
    size_t previousRowEnd = 0;
    std::string weaponSet;
    unsigned accessories = 0, rings = 0;
    for (std::sregex_iterator i(gearHtml.begin(), gearHtml.end(), rowRe), end; i != end; ++i) {
        const size_t start = static_cast<size_t>(i->position());
        const std::string preceding = gearHtml.substr(previousRowEnd, start - previousRowEnd);
        if (preceding.find("Primary Weapon Set") != std::string::npos) weaponSet = "A";
        if (preceding.find("Secondary Weapon Set") != std::string::npos) weaponSet = "B";
        previousRowEnd = start + i->length();
        const std::string row = (*i)[1].str();
        // Snow Crows does not guarantee attribute ordering. Some gear rows
        // put the selected stat BEFORE data-armory-ids, so read IDs from the
        // entire first item embed rather than assuming adjacent attributes.
        const std::string itemTag = Match(row,
            R"SC(<div[^>]*data-armory-embed="items"[^>]*>)SC", 0);
        const uint32_t item = Unsigned(Match(itemTag,
            R"SC(data-armory-ids="([0-9]+)")SC"));
        if (!item) continue;
        const std::string label = Decode(Match(row, R"(<p\s+class="mb-1">([^<]+))"));
        const std::string hint = Decode(Match(row,
            R"(<span\s+class="text-neutral-400 text-sm ml-2">([^<]+)</span>)"));
        if (label == "Infusion") {
            int count = static_cast<int>(Unsigned(Match(hint, R"(x([0-9]+))")));
            if (count > 0 && count <= 25) parsed.infusions.push_back({item, count});
            continue;
        }
        if (hint == "Relic") { parsed.relicId = item; parsed.relicName = label; continue; }
        std::string slot;
        if (hint == "Accessory") slot = ++accessories == 1 ? "Accessory1" : "Accessory2";
        else if (hint == "Ring") slot = ++rings == 1 ? "Ring1" : "Ring2";
        else if (hint == "Backpiece") slot = "Backpack";
        else if (hint == "Helm" || hint == "Shoulders" || hint == "Coat" ||
                 hint == "Gloves" || hint == "Leggings" || hint == "Boots" ||
                 hint == "Amulet") slot = hint;
        else if (hint == "Main Hand") slot = "Weapon" + weaponSet + "1";
        else if (hint == "Off Hand") slot = "Weapon" + weaponSet + "2";
        if (slot.empty() || (hint == "Main Hand" && weaponSet.empty())) continue;

        Gear g;
        g.itemId = item;
        const std::string itemStr = std::to_string(item);
        g.statId = Unsigned(Match(row, "data-armory-" + itemStr + "-stat=\"([0-9]+)\""));
        g.upgrades = Ids(Match(row, "data-armory-" + itemStr + "-upgrades=\"([0-9,]+)\""));
        g.statName = label;
        if (!weaponSet.empty() && slot.compare(0, 6, "Weapon") == 0) {
            // This text describes the recommended weapon, not a locked item skin.
            const std::vector<std::string> types = {
                "Greatsword", "Longbow", "Shortbow", "Hammer", "Scepter", "Pistol",
                "Rifle", "Shield", "Sword", "Dagger", "Focus", "Torch", "Staff",
                "Spear", "Trident", "Harpoon Gun", "Warhorn", "Axe", "Mace"};
            for (const auto& type : types) {
                if (label.size() > type.size() &&
                    label.compare(label.size() - type.size(), type.size(), type) == 0) {
                    g.weaponType = type;
                    g.statName = label.substr(0, label.size() - type.size());
                    break;
                }
            }
        }
        while (!g.statName.empty() && g.statName.back() == ' ') g.statName.pop_back();
        // Armor has one rune per piece; upgrades on weapons are sigils.
        if (slot == "Helm" || slot == "Shoulders" || slot == "Coat" ||
            slot == "Gloves" || slot == "Leggings" || slot == "Boots")
            if (!g.upgrades.empty()) parsed.runeId = g.upgrades.front();
        parsed.gear[slot] = std::move(g);
    }
    int totalInfusions = 0;
    for (const auto& inf : parsed.infusions) totalInfusions += inf.count;
    if (parsed.name.empty() || parsed.gear.size() < 12 || parsed.relicId == 0 ||
        specIndex != 3 || skillIds.size() != 5 || totalInfusions == 0 || totalInfusions > 25) {
        error = "Incomplete Snow Crows build: expected equipment, relic, skills, and three trait lines.";
        return false;
    }
    out = std::move(parsed);
    return true;
}

} // namespace SnowCrows
} // namespace AlterEgo
