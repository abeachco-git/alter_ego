#pragma once
#include "FashionModel.h"
#include "NativeLibraryStore.h"

namespace AlterEgo { namespace Fashion {

class Library {
public:
    std::vector<Look> looks;
    std::string directory;
    std::string error;
    bool Load(const std::string& path) {
        directory=path;NativeLibrary::Snapshot state;
        auto& store=NativeLibrary::RuntimeStore();
        if(!store.Open(path,error)||!store.Read(state,error)){looks.clear();return false;}
        looks=std::move(state.fashion);return true;
    }
private:
    bool Save() {
        NativeLibrary::Snapshot before;
        auto& store=NativeLibrary::RuntimeStore();
        if(directory.empty()||!store.Read(before,error))return false;
        auto next=before;next.fashion=looks;
        std::set<std::string> ids;
        for(const auto& look:looks)ids.insert(look.id);
        for(auto& l:next.loadouts.loadouts)
            if(!l.fashion_id.empty()&&!ids.count(l.fashion_id))l.fashion_id.clear();
        return store.Commit(before,std::move(next),error);
    }
public:
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

    bool UpdateDraft(const Look& baseline,const Look& edited) {
        if(directory.empty()||baseline.id.empty())return false;
        NativeLibrary::Snapshot before;
        auto& store=NativeLibrary::RuntimeStore();
        if(!store.Read(before,error))return false;
        auto next=before;
        auto it=std::find_if(next.fashion.begin(),next.fashion.end(),[&](const Look& look){return look.id==baseline.id;});
        if(it==next.fashion.end()||ToJson(*it)!=ToJson(baseline)){error="Saved Fashion changed; reopen editor.";return false;}
        Look copy=edited;
        copy.id=it->id;copy.created=it->created;
        copy.wardrobe_link=CanonicalLink(copy.wardrobe_link);
        if(copy.wardrobe_link.empty()||copy.name.empty())return false;
        *it=std::move(copy);
        auto published=next.fashion;
        if(!store.Commit(before,std::move(next),error))return false;
        looks=std::move(published);return true;
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
