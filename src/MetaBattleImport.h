#pragma once

// Parse the primary equipment and build template displayed on a MetaBattle
// build page.  No Wiki API or script execution is required: GW2 IDs are
// present in server-rendered Armory embeds.  Keep extraction self-contained
// so a layout change fails without overwriting any saved user build.
#include "SnowCrowsImport.h"
#include "AscendedStatNames.h"
#include <regex>
#include <string>
#include <vector>

namespace AlterEgo {
namespace MetaBattle {

inline std::string NormalizeUrl(std::string url) {
    const auto first = url.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    url = url.substr(first, url.find_last_not_of(" \t\r\n") - first + 1);
    if (url.compare(0, 7, "http://") == 0)
        url = "https://" + url.substr(7);
    static const std::string domains[] = {
        "https://metabattle.com", "https://www.metabattle.com"};
    std::string path;
    for (const auto& domain : domains) {
        if (url.compare(0, domain.size(), domain) == 0 &&
            url.size() > domain.size() && url[domain.size()] == '/') {
            path = url.substr(domain.size());
            break;
        }
    }
    if (path.empty() || path.find_first_of("?#\\ \t\r\n") != std::string::npos ||
        path.find("..") != std::string::npos || path.find("//") != std::string::npos)
        return {};
    const bool direct = path.compare(0, 12, "/wiki/Build:") == 0;
    const bool encoded = path.compare(0, 14, "/wiki/Build%3A") == 0 ||
                         path.compare(0, 14, "/wiki/Build%3a") == 0;
    if (!direct && !encoded) return {};
    const size_t prefix = direct ? 12 : 14;
    if (path.size() <= prefix || path.size() > 300) return {};
    // The URL prefix contains its two expected slashes. The article slug
    // must not contain additional path separators.
    for (size_t i = prefix; i < path.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(path[i]);
        const bool valid = std::isalnum(c) || c == '_' || c == '-' ||
                           c == ':' || c == '%' || c == '(' || c == ')' ||
                           c == '.' || c == '+' || c == ',';
        if (!valid) return {};
    }
    return "https://metabattle.com" + path;
}

inline std::string TextFromSmall(const std::string& html) {
    std::string withBar = std::regex_replace(
        html, std::regex(R"(<br\s*/?>)", std::regex::icase), "|");
    withBar = std::regex_replace(withBar, std::regex(R"(<[^>]*>)"), "");
    return SnowCrows::Decode(withBar);
}
inline bool IsTwoHanded(const std::string& type) {
    return type == "Greatsword" || type == "Staff" || type == "Hammer" ||
           type == "Longbow" || type == "Shortbow" || type == "Rifle" ||
           type == "Spear" || type == "Trident" || type == "Harpoon Gun";
}
inline std::string FirstText(std::string s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    return s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
}

inline bool Parse(const std::string& url, const std::string& html,
                  SnowCrows::Page& out, std::string& error) {
    error.clear();
    const auto normalized = NormalizeUrl(url);
    if (normalized.empty()) {
        error = "Only MetaBattle build URLs are supported.";
        return false;
    }
    if (html.size() < 1000 || html.size() > 3 * 1024 * 1024) {
        error = "The MetaBattle page is empty or unexpectedly large.";
        return false;
    }
    const size_t skillAt = html.find("id=\"Skill_Bar\"");
    const size_t specAt = html.find("id=\"Specializations\"");
    const size_t gearAt = html.find("id=\"Equipment\"");
    if (skillAt == std::string::npos || specAt == std::string::npos ||
        gearAt == std::string::npos || !(skillAt < specAt && specAt < gearAt)) {
        error = "MetaBattle build skills, specializations or equipment were not found.";
        return false;
    }

    SnowCrows::Page page;
    page.sourceUrl = normalized;
    page.name = SnowCrows::Decode(SnowCrows::Match(
        html, R"(<h1[^>]*id="firstHeading"[^>]*>([^<]+)</h1>)"));
    if (page.name.empty())
        page.name = SnowCrows::Decode(SnowCrows::Match(
            html, R"(<title>([^<]+?)(?:\s+-\s+MetaBattle[^<]*)?</title>)"));

    // MetaBattle includes the original profession in its MediaWiki category
    // metadata. Do not infer it from the elite-spec title (new elite specs
    // would then be misclassified).
    const std::string prof = SnowCrows::Match(html,
        R"("(Guardian|Warrior|Engineer|Ranger|Thief|Elementalist|Mesmer|Necromancer|Revenant) builds")");
    if (prof.empty()) {
        error = "MetaBattle profession category was not found.";
        return false;
    }
    page.profession = prof;
    if (html.find("\"PvP builds\"") != std::string::npos) page.gameMode = "PvP";
    else if (html.find("\"WvW builds\"") != std::string::npos) page.gameMode = "WvW";
    else if (html.find("\"Raid builds\"") != std::string::npos) page.gameMode = "Raid";
    else if (html.find("\"Fractals of the Mists builds\"") != std::string::npos)
        page.gameMode = "Fractal";
    else page.gameMode = "PvE";

    // The first Armory skill embed is frequently a profession mechanic bar.
    // Extract the actual 5-slot heal/utility/elite block after "Utility".
    const std::string skillSection = html.substr(skillAt, specAt - skillAt);
    const size_t utilityAt = skillSection.find(">Utility</div>");
    if (utilityAt == std::string::npos) {
        error = "MetaBattle primary utility skills were not found.";
        return false;
    }
    const std::string utilitySection = skillSection.substr(utilityAt);
    const auto skillIds = SnowCrows::Ids(SnowCrows::Match(
        utilitySection,
        R"MB(data-armory-embed="skills"[^>]*data-armory-ids="([0-9,]+)")MB"));
    if (skillIds.size() != 5)
        page.importWarning = "Primary skills incomplete; review heal, utilities and elite.";
    std::copy_n(skillIds.begin(), std::min(skillIds.size(), page.skills.size()), page.skills.begin());

    const std::string specializationSection = html.substr(specAt, gearAt - specAt);
    const std::regex specsRe(
        R"MB(data-armory-embed="specializations"[^>]*data-armory-ids="([0-9]+)"[^>]*data-armory-[0-9]+-traits="([0-9,]*)")MB",
        std::regex::icase);
    size_t countSpecs = 0;
    for (std::sregex_iterator i(specializationSection.begin(), specializationSection.end(), specsRe), end;
         i != end && countSpecs < page.specs.size(); ++i) {
        page.specs[countSpecs].id = SnowCrows::Unsigned((*i)[1].str());
        page.specs[countSpecs].traits = SnowCrows::Ids((*i)[2].str());
        ++countSpecs;
    }
    if (countSpecs != 3) {
        if (!page.importWarning.empty()) page.importWarning += " ";
        page.importWarning += "Specialization lines incomplete; review traits.";
    }

    // The default loadout appears directly below the Equipment heading.
    // Later <h3> sections may contain complete *alternative* equipment grids
    // (e.g. Reaper's Impact Sigil Variant): these can overwrite ring/backpiece
    // slots and make infusion counts exceed 25, causing all totals to be lost.
    // Bound the parser to the primary grid before the first subsection.
    const size_t consumablesAt = html.find("id=\"Consumables\"", gearAt);
    size_t equipmentEnd = consumablesAt == std::string::npos
        ? html.size() : consumablesAt;
    const size_t subsectionAt = html.find("<h3", gearAt);
    if (subsectionAt != std::string::npos && subsectionAt < equipmentEnd)
        equipmentEnd = subsectionAt;
    const std::string gearHtml = html.substr(gearAt, equipmentEnd - gearAt);
    const std::regex smallRe(R"(<small\b[^>]*>([\s\S]*?)</small>)", std::regex::icase);
    int accessory = 0, ring = 0, weaponSet = 0;
    int sigilsSeen = 0;
    std::vector<std::string> pendingWeaponSlots;
    std::vector<uint32_t> pendingSigils;
    int totalInfusions = 0;

    auto finishWeapons = [&]() {
        if (pendingWeaponSlots.size() == 1) {
            auto& g = page.gear[pendingWeaponSlots.front()];
            if (!pendingSigils.empty()) g.upgrades.push_back(pendingSigils[0]);
            if (IsTwoHanded(g.weaponType) && pendingSigils.size() > 1)
                g.upgrades.push_back(pendingSigils[1]);
        } else {
            for (size_t w = 0; w < pendingWeaponSlots.size() && w < pendingSigils.size(); ++w)
                page.gear[pendingWeaponSlots[w]].upgrades.push_back(pendingSigils[w]);
        }
        pendingWeaponSlots.clear();
        pendingSigils.clear();
        sigilsSeen = 0;
        ++weaponSet;
    };
    const std::vector<std::string> weaponTypes = {
        "Harpoon Gun", "Greatsword", "Shortbow", "Longbow", "Trident",
        "Warhorn", "Scepter", "Dagger", "Sword", "Pistol", "Rifle",
        "Hammer", "Shield", "Focus", "Torch", "Staff", "Spear", "Axe", "Mace"};

    for (std::sregex_iterator i(gearHtml.begin(), gearHtml.end(), smallRe), end;
         i != end; ++i) {
        const size_t smallAt = static_cast<size_t>(i->position());
        // Match the equipment-slot class even when the page adds extra CSS classes.
        const size_t wrapper = gearHtml.rfind("equipment-slot", smallAt);
        if (wrapper == std::string::npos) continue;
        const size_t divStart = gearHtml.rfind("<div", wrapper);
        if (divStart == std::string::npos) continue;
        const std::string beforeSmall = gearHtml.substr(divStart, smallAt - divStart);
        const uint32_t id = SnowCrows::Unsigned(SnowCrows::Match(
            beforeSmall, R"MB(data-armory-ids="([0-9]+)")MB"));
        if (!id) continue;
        const std::string label = TextFromSmall((*i)[1].str());
        const size_t split = label.find('|');
        const std::string kind = FirstText(label.substr(0, split));
        const std::string stat = split == std::string::npos
            ? "" : FirstText(label.substr(split + 1));
        if (kind == "Relic") { page.relicId = id; continue; }
        if (kind == "Rune") { page.runeId = id; continue; }
        if (kind == "Infusion") {
            const int amount = static_cast<int>(SnowCrows::Unsigned(
                SnowCrows::Match(stat, R"(x([0-9]+))")));
            if (amount >= 1 && amount <= 25) {
                page.infusions.push_back({id, amount});
                totalInfusions += amount;
            }
            continue;
        }
        if (kind == "Sigil") {
            if (weaponSet < 2 && !pendingWeaponSlots.empty()) {
                pendingSigils.push_back(id);
                if (++sigilsSeen == 2) finishWeapons();
            }
            continue;
        }

        std::string slot;
        if (kind == "Head") slot = "Helm";
        else if (kind == "Shoulders") slot = "Shoulders";
        else if (kind == "Chest") slot = "Coat";
        else if (kind == "Hands") slot = "Gloves";
        else if (kind == "Legs") slot = "Leggings";
        else if (kind == "Feet") slot = "Boots";
        else if (kind == "Backpiece") slot = "Backpack";
        else if (kind == "Accessory") slot = ++accessory == 1 ? "Accessory1" : "Accessory2";
        else if (kind == "Amulet") slot = "Amulet";
        else if (kind == "Ring") slot = ++ring == 1 ? "Ring1" : "Ring2";
        else {
            for (const auto& type : weaponTypes)
                if (kind == type) {
                    if (weaponSet < 2 && pendingWeaponSlots.size() < 2) {
                        slot = weaponSet == 0 ? (pendingWeaponSlots.empty() ? "WeaponA1" : "WeaponA2")
                                              : (pendingWeaponSlots.empty() ? "WeaponB1" : "WeaponB2");
                    }
                    break;
                }
        }
        if (slot.empty()) continue;
        SnowCrows::Gear g;
        g.itemId = id;
        g.statName = NamedStats::CanonicalStatName(stat);
        if (g.statName.empty()) g.statName = stat;
        if (slot.compare(0, 6, "Weapon") == 0) {
            g.weaponType = kind;
            pendingWeaponSlots.push_back(slot);
        }
        page.gear[slot] = std::move(g);
    }
    if (!pendingWeaponSlots.empty()) finishWeapons();

    // Preserve MetaBattle's original template link rather than reconstructing
    // one from still-loading palette metadata.
    page.templateCode = SnowCrows::Decode(SnowCrows::Match(
        html.substr(skillAt, gearAt - skillAt),
        R"(<div[^>]*class="[^"]*build-template-code[^"]*"[^>]*>(\[&amp;[A-Za-z0-9+/=]+\])</div>)"));

    if (page.name.empty()) {
        error = "MetaBattle build title was not found.";
        return false;
    }
    // Do not reject an otherwise identifiable build for intentionally blank
    // equipment or for a source layout that omits some armory IDs.
    if (page.gear.size() < 12) {
        if (!page.importWarning.empty()) page.importWarning += " ";
        page.importWarning += "Only " + std::to_string(page.gear.size()) +
            " equipment slots resolved; review source gear.";
    }
    if (!page.relicId) {
        if (!page.importWarning.empty()) page.importWarning += " ";
        page.importWarning += "Relic not resolved; choose manually if needed.";
    }
    if (totalInfusions > 25) {
        page.infusions.clear();
        if (!page.importWarning.empty()) page.importWarning += " ";
        page.importWarning += "Infusion counts ambiguous; review manually.";
    }
    bool hasSkills = false, hasSpecs = false;
    for (auto id : page.skills) if (id) hasSkills = true;
    for (const auto& spec : page.specs) if (spec.id) hasSpecs = true;
    if (!hasSkills && !hasSpecs && page.gear.empty() && page.templateCode.empty()) {
        error = "No usable MetaBattle build data could be extracted.";
        return false;
    }
    out = std::move(page);
    return true;
}

} // namespace MetaBattle
} // namespace AlterEgo
