#pragma once
#include "FashionModel.h"

namespace AlterEgo { namespace Fashion {

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
