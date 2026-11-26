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
        NativeLibrary::Snapshot before;auto& store=NativeLibrary::RuntimeStore();
        if(directory.empty()||!store.Read(before,error))return -1;
        auto next=before;
        if (look.created == 0) look.created = std::time(nullptr);
        if (look.id.empty()) {
            const std::string prefix = std::to_string(look.created) + "_fashion_";
            unsigned index=static_cast<unsigned>(next.fashion.size());
            bool collision = true;
            while (collision) {
                look.id = prefix + std::to_string(index++);
                collision = std::any_of(next.fashion.begin(),next.fashion.end(),
                    [&](const Look& x) { return x.id == look.id; });
            }
        }
        if(std::any_of(next.fashion.begin(),next.fashion.end(),[&](const auto& row){return row.id==look.id;})){error="Fashion ID already exists.";return -1;}
        next.fashion.push_back(std::move(look));auto published=next.fashion;
        if(!store.Commit(before,std::move(next),error))return -1;
        looks=std::move(published);return static_cast<int>(looks.size())-1;
    }

    bool Update(int idx, const Look& updated) {
        if (idx < 0 || idx >= static_cast<int>(looks.size())) return false;
        const Look baseline=looks[idx];return UpdateDraft(baseline,updated);
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

    bool RemoveDraft(const Look& baseline){
        if(directory.empty()||baseline.id.empty())return false;
        NativeLibrary::Snapshot before;auto& store=NativeLibrary::RuntimeStore();
        if(!store.Read(before,error))return false;
        auto next=before;
        auto it=std::find_if(next.fashion.begin(),next.fashion.end(),[&](const Look& look){return look.id==baseline.id;});
        if(it==next.fashion.end()||ToJson(*it)!=ToJson(baseline)){error="Saved Fashion changed; review again.";return false;}
        next.fashion.erase(it);
        for(auto& l:next.loadouts.loadouts)if(l.fashion_id==baseline.id)l.fashion_id.clear();
        auto published=next.fashion;
        if(!store.Commit(before,std::move(next),error))return false;
        looks=std::move(published);return true;
    }
    bool Remove(int idx) {
        if (idx < 0 || idx >= static_cast<int>(looks.size())) return false;
        const Look baseline=looks[idx];return RemoveDraft(baseline);
    }
};

} // namespace Fashion
} // namespace AlterEgo
