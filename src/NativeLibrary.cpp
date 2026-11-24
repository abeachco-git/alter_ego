#include "NativeLibrary.h"
#include "NativeLibraryCodec.h"
#include "NativeLibrarySchema.h"
#include <unordered_map>

namespace AlterEgo::NativeLibrary {
using namespace Codec;
using Composable::Document;
using Composable::Component;
using Composable::Loadout;

std::string ExactFingerprint(const Json& data) {
    std::uint64_t hash=14695981039346656037ull;
    for(unsigned char c:data.dump()) {hash^=c;hash*=1099511628211ull;}
    static const char* digits="0123456789abcdef";
    std::string result(16,'0');
    for(int i=15;i>=0;--i) {result[static_cast<size_t>(i)]=digits[hash&15u];hash>>=4;}
    return result;
}
bool ExactContent(const Json& a,const Json& b) {return a.dump()==b.dump();}

namespace {
void ValidateSnapshot(const Snapshot& snapshot) {
    std::string error;
    if(!Composable::Validate(snapshot.loadouts,error))
        throw Database::Error(SQLITE_CONSTRAINT,error);
    std::set<std::string> fashion;
    for(const auto& look:snapshot.fashion) {
        DecodedWardrobeLink w{};
        if(look.id.empty()||look.name.empty()||!fashion.insert(look.id).second||
           !Fashion::DecodeValid(look.wardrobe_link,w)||!look.extension.is_object())
            throw Database::Error(SQLITE_CONSTRAINT,"Invalid or duplicate Fashion record");
    }
    for(const auto& loadout:snapshot.loadouts.loadouts)
        if(!loadout.fashion_id.empty()&&!fashion.count(loadout.fashion_id))
            throw Database::Error(SQLITE_CONSTRAINT,"Unknown Fashion association");
}
template<class T> using Index=std::unordered_map<std::string,std::pair<size_t,const T*>>;
template<class T> Index<T> MakeIndex(const std::vector<T>& rows) {
    Index<T> result;
    for(size_t i=0;i<rows.size();++i) result.emplace(rows[i].id,std::make_pair(i,&rows[i]));
    return result;
}
bool SameLoadout(const Loadout& a,const Loadout& b) {
    return ExactContent(a.metadata,b.metadata)&&a.refs==b.refs&&a.fashion_id==b.fashion_id;
}
}

Repository::Repository(const std::string& path,Database::Connection::OpenMode mode,Fingerprint fingerprint,bool verify)
    :db_(path,mode),fingerprint_(std::move(fingerprint)) {
    const auto id=db_.Scalar("PRAGMA application_id");
    const auto version=db_.Scalar("PRAGMA user_version");
    if(mode==Database::Connection::OpenMode::Existing && (id!=kApplicationId||version<=0))
        throw Database::Error(SQLITE_NOTADB,"Existing library is not an initialized Alter Ego database");
    if(id==kApplicationId&&version==kSchemaVersion) {
        if(verify) db_.Verify();
    } else Database::Migrate(db_,kApplicationId,kSchemaVersion,UpgradeSchema);
}
Snapshot Repository::ReadUnlocked() {
    Snapshot snapshot;
    snapshot.revision=db_.Scalar("SELECT revision FROM library_state WHERE singleton=1");
    {
        auto q=db_.Prepare("SELECT kind,id,name,fingerprint FROM components ORDER BY kind,ordinal,id");
        while(q.Step()) {
            const auto kind=q.Text(0); Component c{q.Text(1),q.Text(2),ReadPayload(db_,kind,q.Text(1))};
            if(fingerprint_(c.data)!=q.Text(3))
                throw Database::Error(SQLITE_CORRUPT,"Component fingerprint verification failed");
            snapshot.loadouts.components[kind].push_back(std::move(c));
        }
    }
    {
        auto q=db_.Prepare("SELECT id,name,notes,weight,origin,source_character,source_tab,complete,created,visibility_flags,extension_json FROM fashion ORDER BY ordinal,id");
        while(q.Step()) snapshot.fashion.push_back(ReadFashion(db_,q));
    }
    {
        auto q=db_.Prepare(("SELECT id,fashion_id,modes_present,"+Select(metadataFields)+
            " FROM loadouts ORDER BY ordinal,id").c_str());
        while(q.Step()) snapshot.loadouts.loadouts.push_back(ReadLoadout(db_,q));
    }
    {
        auto q=db_.Prepare("SELECT id,name FROM equipment_sets ORDER BY ordinal,id");
        while(q.Step()) {
            Composable::EquipmentSet set{q.Text(0),q.Text(1),{}};
            auto refs=db_.Prepare("SELECT kind,component_id FROM equipment_set_components WHERE set_id=?");
            BindKeys(refs,{set.id});
            while(refs.Step())set.refs.emplace(refs.Text(0),refs.Text(1));
            snapshot.loadouts.equipment_sets.push_back(std::move(set));
        }
    }
    ValidateSnapshot(snapshot);
    return snapshot;
}
Snapshot Repository::Read() {
    Database::Transaction tx(db_);auto snapshot=ReadUnlocked();tx.Commit();return snapshot;
}
std::int64_t Repository::Commit(const Snapshot& before,const Snapshot& after) {
    ValidateSnapshot(after);
    Database::Transaction tx(db_);
    if(before.revision!=db_.Scalar("SELECT revision FROM library_state WHERE singleton=1"))
        throw Database::Error(SQLITE_BUSY,"Library changed while edit was prepared");
    bool changed=false;
    const auto oldFashion=MakeIndex(before.fashion),newFashion=MakeIndex(after.fashion);
    for(size_t i=0;i<after.fashion.size();++i) {
        const auto& look=after.fashion[i];auto old=oldFashion.find(look.id);
        if(old!=oldFashion.end()&&old->second.first==i&&
           Fashion::ToJson(*old->second.second)==Fashion::ToJson(look)) continue;
        WriteFashion(db_,look,i);changed=true;
    }
    const std::vector<Component> empty;
    for(const auto& kind:Composable::Kinds()) {
        auto a=after.loadouts.components.find(kind),b=before.loadouts.components.find(kind);
        const auto& current=a==after.loadouts.components.end()?empty:a->second;
        const auto& previous=b==before.loadouts.components.end()?empty:b->second;
        const auto old=MakeIndex(previous);
        for(size_t i=0;i<current.size();++i) {
            const auto& c=current[i];auto found=old.find(c.id);
            const bool exists=found!=old.end();
            const bool sameData=exists&&ExactContent(found->second.second->data,c.data);
            if(sameData&&found->second.first==i&&found->second.second->name==c.name)continue;
            Run(db_,"INSERT INTO components VALUES(?,?,?,?,?) ON CONFLICT(kind,id) DO UPDATE SET "
                "name=excluded.name,ordinal=excluded.ordinal,fingerprint=excluded.fingerprint",
                {kind,c.id,c.name,Number(i),fingerprint_(c.data)});
            if(!sameData) {
                Run(db_,"DELETE FROM component_payloads WHERE kind=? AND component_id=?",{kind,c.id});
                WritePayload(db_,kind,c);
                if(!ExactContent(ReadPayload(db_,kind,c.id),c.data))
                    throw Database::Error(SQLITE_CONSTRAINT,"Component relational round-trip failed");
            }
            changed=true;
        }
    }
    const auto oldLoads=MakeIndex(before.loadouts.loadouts),newLoads=MakeIndex(after.loadouts.loadouts);
    for(size_t i=0;i<after.loadouts.loadouts.size();++i) {
        const auto& l=after.loadouts.loadouts[i];auto old=oldLoads.find(l.id);
        if(old!=oldLoads.end()&&old->second.first==i&&SameLoadout(*old->second.second,l))continue;
        WriteLoadout(db_,l,i);changed=true;
    }
    for(const auto& old:before.loadouts.loadouts) if(!newLoads.count(old.id)) {
        Run(db_,"DELETE FROM loadouts WHERE id=?",{old.id});changed=true;
    }
    const auto oldSets=MakeIndex(before.loadouts.equipment_sets),newSets=MakeIndex(after.loadouts.equipment_sets);
    for(size_t i=0;i<after.loadouts.equipment_sets.size();++i) {
        const auto& set=after.loadouts.equipment_sets[i];auto old=oldSets.find(set.id);
        if(old!=oldSets.end()&&old->second.first==i&&old->second.second->name==set.name&&
            old->second.second->refs==set.refs)continue;
        Run(db_,"INSERT INTO equipment_sets VALUES(?,?,?) ON CONFLICT(id) DO UPDATE SET name=excluded.name,ordinal=excluded.ordinal",
            {set.id,set.name,Number(i)});
        Run(db_,"DELETE FROM equipment_set_components WHERE set_id=?",{set.id});
        for(const auto& [kind,id]:set.refs)
            Run(db_,"INSERT INTO equipment_set_components VALUES(?,?,?)",{set.id,kind,id});
        changed=true;
    }
    for(const auto& set:before.loadouts.equipment_sets)if(!newSets.count(set.id)) {
        Run(db_,"DELETE FROM equipment_sets WHERE id=?",{set.id});changed=true;
    }
    for(const auto& entry:before.loadouts.components) {
        auto a=after.loadouts.components.find(entry.first);
        const auto current=MakeIndex(a==after.loadouts.components.end()?empty:a->second);
        for(const auto& old:entry.second) if(!current.count(old.id)) {
            Run(db_,"DELETE FROM components WHERE kind=? AND id=?",{entry.first,old.id});changed=true;
        }
    }
    for(const auto& old:before.fashion) if(!newFashion.count(old.id)) {
        Run(db_,"DELETE FROM fashion WHERE id=?",{old.id});changed=true;
    }
    if(changed) db_.Exec("UPDATE library_state SET revision=revision+1 WHERE singleton=1");
    auto revision=db_.Scalar("SELECT revision FROM library_state WHERE singleton=1");
    tx.Commit();return revision;
}
std::vector<Match> Repository::ExactMatches(const std::string& kind,const Json& data) {
    if(!Composable::IsKind(kind)||!data.is_object())
        throw Database::Error(SQLITE_MISUSE,"Invalid component match request");
    auto q=db_.Prepare("SELECT c.id,c.name,(SELECT count(*) FROM loadout_components r "
        "WHERE r.kind=c.kind AND r.component_id=c.id)+(SELECT count(*) FROM equipment_set_components r "
        "WHERE r.kind=c.kind AND r.component_id=c.id) FROM components c "
        "WHERE c.kind=? AND c.fingerprint=? ORDER BY c.ordinal,c.id");
    BindKeys(q,{kind,fingerprint_(data)});
    std::vector<Match> result;
    while(q.Step()) {
        auto id=q.Text(0);
        if(ExactContent(ReadPayload(db_,kind,id),data))
            result.push_back({id,q.Text(1),q.Integer(2)});
    }
    return result;
}
std::vector<std::string> Repository::Users(const std::string& kind,const std::string& id) {
    auto q=db_.Prepare("SELECT l.id FROM loadout_components r JOIN loadouts l ON l.id=r.loadout_id "
        "WHERE r.kind=? AND r.component_id=? ORDER BY l.ordinal,l.id");
    BindKeys(q,{kind,id});std::vector<std::string> users;
    while(q.Step())users.push_back(q.Text(0));
    auto sets=db_.Prepare("SELECT s.id FROM equipment_set_components r JOIN equipment_sets s ON s.id=r.set_id "
        "WHERE r.kind=? AND r.component_id=? ORDER BY s.ordinal,s.id");
    BindKeys(sets,{kind,id});
    while(sets.Step())users.push_back(sets.Text(0));
    return users;
}

} // namespace AlterEgo::NativeLibrary
