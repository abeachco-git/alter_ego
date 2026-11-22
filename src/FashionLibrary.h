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

inline nlohmann::json ToJson(const Look& look) {
    return {{"id", look.id}, {"name", look.name}, {"notes", look.notes},
            {"wardrobe_link", look.wardrobe_link}, {"weight", look.weight},
            {"origin", look.origin}, {"source_character", look.source_character},
            {"source_tab", look.source_tab}, {"complete", look.complete},
            {"characters", look.characters}, {"created", look.created}};
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
    if (look.id.empty()) return false;
    return true;
}

class Library {
public:
    std::vector<Look> looks;
    std::string file_path;

    bool Load(const std::string& path) {
        file_path = path;
        looks.clear();
        std::ifstream input(path, std::ios::binary);
        if (!input) return true; // a fresh installation has no library
        try {
            nlohmann::json j;
            input >> j;
            if (!j.is_object() || !j.contains("looks") || !j["looks"].is_array()) return false;
            for (const auto& item : j["looks"]) {
                Look look;
                if (FromJson(item, look)) looks.push_back(std::move(look));
            }
            return true;
        } catch (...) { looks.clear(); return false; }
    }

    bool Save() const {
        if (file_path.empty()) return false;
        nlohmann::json j;
        j["version"] = 1;
        j["looks"] = nlohmann::json::array();
        for (const auto& look : looks) j["looks"].push_back(ToJson(look));
        const std::string tmp = file_path + ".tmp";
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            if (!out) return false;
            out << j.dump(2);
            out.flush();
            if (!out.good()) return false;
        }
#ifdef _WIN32
        if (!MoveFileExA(tmp.c_str(), file_path.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            std::remove(tmp.c_str());
            return false;
        }
#else
        if (std::rename(tmp.c_str(), file_path.c_str()) != 0) {
            std::remove(tmp.c_str());
            return false;
        }
#endif
        return true;
    }

    int FindExact(const std::string& link) const {
        const std::string canonical = CanonicalLink(link);
        if (canonical.empty()) return -1;
        for (int i = 0; i < static_cast<int>(looks.size()); ++i)
            if (looks[i].wardrobe_link == canonical) return i;
        return -1;
    }

    int Add(Look look) {
        look.wardrobe_link = CanonicalLink(look.wardrobe_link);
        if (look.wardrobe_link.empty() || look.name.empty()) return -1;
        if (look.created == 0) look.created = std::time(nullptr);
        if (look.id.empty()) {
            const std::string prefix = std::to_string(look.created) + "_fashion_";
            unsigned index = static_cast<unsigned>(looks.size());
            bool collision = true;
            while (collision) {
                look.id = prefix + std::to_string(index++);
                collision = std::any_of(looks.begin(), looks.end(),
                    [&](const Look& x) { return x.id == look.id; });
            }
        }
        looks.push_back(std::move(look));
        if (!Save()) { looks.pop_back(); return -1; }
        return static_cast<int>(looks.size()) - 1;
    }

    bool Update(int idx, const Look& updated) {
        if (idx < 0 || idx >= static_cast<int>(looks.size())) return false;
        Look copy = updated;
        copy.wardrobe_link = CanonicalLink(copy.wardrobe_link);
        if (copy.wardrobe_link.empty() || copy.name.empty()) return false;
        Look before = looks[idx];
        copy.id = before.id;
        copy.created = before.created;
        looks[idx] = std::move(copy);
        if (!Save()) { looks[idx] = std::move(before); return false; }
        return true;
    }

    bool Remove(int idx) {
        if (idx < 0 || idx >= static_cast<int>(looks.size())) return false;
        Look old = looks[idx];
        looks.erase(looks.begin() + idx);
        if (!Save()) { looks.insert(looks.begin() + idx, std::move(old)); return false; }
        return true;
    }
};

} // namespace Fashion
} // namespace AlterEgo
