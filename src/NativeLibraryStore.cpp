#include "NativeLibraryStore.h"
#include "NativeLibrarySchema.h"
#include <atomic>
#include <chrono>
#include <fstream>
#include <unordered_map>
#ifdef _WIN32
#include <windows.h>
#endif

namespace AlterEgo::NativeLibrary {
namespace fs=std::filesystem;
using Json=Composable::Json;
using Mode=Database::Connection::OpenMode;
namespace {

bool Exists(const fs::path& path) {
    std::error_code ec;
    auto status=fs::symlink_status(path,ec);
    if(ec==std::errc::no_such_file_or_directory) return false;
    if(ec) throw std::runtime_error("Cannot inspect "+path.u8string()+": "+ec.message());
    if(status.type()==fs::file_type::not_found) return false;
    if(!fs::is_regular_file(status))
        throw std::runtime_error("Expected a regular file: "+path.u8string());
    return true;
}
std::string ReadBytes(const fs::path& path) {
    constexpr std::uintmax_t limit=64u*1024u*1024u;
    std::ifstream input(path,std::ios::binary|std::ios::ate);
    if(!input) throw std::runtime_error("Cannot open "+path.u8string());
    const auto n=input.tellg();
    if(n<0||static_cast<std::uintmax_t>(n)>limit)
        throw std::runtime_error("Legacy library exceeds size limit");
    std::string bytes(static_cast<size_t>(n),'\0');input.seekg(0);
    if(!bytes.empty()) input.read(&bytes[0],static_cast<std::streamsize>(bytes.size()));
    if(!input) throw std::runtime_error("Incomplete read: "+path.u8string());
    return bytes;
}
void WriteNew(const fs::path& path,const std::string& bytes) {
    if(Exists(path)) throw std::runtime_error("Refusing to overwrite protected migration backup");
    std::ofstream output(path,std::ios::binary|std::ios::trunc);
    if(!output||!output.write(bytes.data(),static_cast<std::streamsize>(bytes.size())))
        throw std::runtime_error("Cannot write migration backup: "+path.u8string());
    output.flush();output.close();
    if(!output||ReadBytes(path)!=bytes)
        throw std::runtime_error("Migration backup verification failed");
}
std::string Unique() {
    static std::atomic<std::uint64_t> sequence{0};
    return std::to_string(std::chrono::system_clock::now().time_since_epoch().count())+
        "-"+std::to_string(sequence.fetch_add(1));
}
fs::path CreateUniqueDirectory(const fs::path& parent,const std::string& prefix) {
    fs::create_directories(parent);
    for(int i=0;i<100;++i) {
        const auto path=parent/(prefix+Unique());
        if(fs::create_directory(path)) return path;
    }
    throw std::runtime_error("Cannot allocate staging directory");
}
struct Scratch {
    fs::path path;
    explicit Scratch(const fs::path& parent):path(CreateUniqueDirectory(parent,".ae-stage-")){}
    ~Scratch(){std::error_code ec;fs::remove_all(path,ec);}
};
void PublishNew(const fs::path& source,const fs::path& target) {
    if(Exists(target)) throw std::runtime_error("Refusing to replace existing database: "+target.u8string());
    std::error_code ec;fs::create_hard_link(source,target,ec);
    if(!ec) return;
#ifdef _WIN32
    if(MoveFileExW(source.c_str(),target.c_str(),MOVEFILE_WRITE_THROUGH)) return;
#endif
    throw std::runtime_error("Cannot publish database without replacing existing data: "+target.u8string());
}
Json FashionJson(const std::vector<Fashion::Look>& looks) {
    Json rows=Json::array();
    for(const auto& look:looks) rows.push_back(Fashion::ToJson(look));
    return rows;
}
std::vector<Fashion::Look> ParseFashion(const std::string& bytes) {
    auto value=Json::parse(bytes);
    if(!value.is_object()||!value.contains("looks")||!value.at("looks").is_array())
        throw std::runtime_error("Legacy Fashion library requires a looks array");
    std::vector<Fashion::Look> looks;
    std::set<std::string> ids;
    for(const auto& record:value.at("looks")) {
        Fashion::Look look;
        if(!Fashion::FromJson(record,look)||look.name.empty()||!ids.insert(look.id).second)
            throw std::runtime_error("Invalid or duplicate legacy Fashion record");
        looks.push_back(std::move(look));
    }
    return looks;
}
void VerifySnapshot(const Snapshot& actual,const Snapshot& expected) {
    if(!ExactContent(Composable::ToJson(actual.loadouts),Composable::ToJson(expected.loadouts))||
       !ExactContent(FashionJson(actual.fashion),FashionJson(expected.fashion)))
        throw std::runtime_error("Native library round-trip verification failed");
}

Snapshot OpenOrMigrate(const Paths& paths) {
    const auto target=paths.Library();
    if(Exists(target)) {
        Repository db(target.u8string(),Mode::Existing);
        return db.Read();
    }
    // Experimental storage cannot be mistaken for the user's original library.
    for(const char* name:{"loadouts_v3.json","loadouts_v3.previous.json"})
        if(Exists(paths.base/name))
            throw std::runtime_error("Experimental v3 files remain. Archive or remove them before native migration; no data changed.");
    std::map<std::string,std::string> originals;
    for(const char* name:{"build_library.json","fashion_library.json"})
        if(Exists(paths.base/name)) originals.emplace(name,ReadBytes(paths.base/name));
    Snapshot proposed;
    if(originals.count("build_library.json")) {
        std::string error; const auto legacy=Json::parse(originals.at("build_library.json"));
        if(!SplitLegacyExact(legacy,proposed.loadouts,error)) throw std::runtime_error(error);
        Json rebuilt;
        if(!Composable::ToLegacy(proposed.loadouts,rebuilt,error)||
           !ExactContent(rebuilt.at("builds"),legacy.at("builds")))
            throw std::runtime_error("Legacy migration changed loadout contents: "+error);
    }
    if(originals.count("fashion_library.json"))
        proposed.fashion=ParseFashion(originals.at("fashion_library.json"));

    Scratch stage(target.parent_path());
    const auto candidate=stage.path/"library.db";
    {
        Repository db(candidate.u8string(),Mode::Create);
        const auto empty=db.Read();
        proposed.revision=db.Commit(empty,proposed);
        VerifySnapshot(db.Read(),proposed);
    }
    // Verify after closing/reopening, before either publishing or archiving.
    {
        Repository db(candidate.u8string(),Mode::Existing);
        VerifySnapshot(db.Read(),proposed);
    }
    fs::path backup;
    if(!originals.empty()) {
        backup=CreateUniqueDirectory(paths.Migration(),"legacy-");
        for(const auto& item:originals) WriteNew(backup/item.first,item.second);
        for(const auto& item:originals)
            if(ReadBytes(paths.base/item.first)!=item.second)
                throw std::runtime_error("Legacy library changed during migration; source files retained");
    }
    PublishNew(candidate,target);
    Snapshot verified;
    {
        Repository db(target.u8string(),Mode::Existing);
        verified=db.Read();VerifySnapshot(verified,proposed);
    }
    // The verified DB is now authoritative, even when empty. Remove source
    // files only when identical archived copies exist. A crash before removal
    // is harmless: future startup sees the initialized DB and never reimports.
    for(const auto& item:originals) {
        const auto source=paths.base/item.first;
        if(Exists(source)&&ReadBytes(source)==item.second&&ReadBytes(backup/item.first)==item.second) {
            std::error_code ec;fs::remove(source,ec);
            // An inaccessible original remains untouched; never overwrite it.
        }
    }
    return verified;
}
} // namespace

bool SplitLegacyExact(const Json& legacy,Composable::Document& output,std::string& error) {
    try {
        Composable::Document doc;
        if(!Composable::SplitLegacy(legacy,doc,error)) return false;
        // Every SplitLegacy component belongs to exactly one imported loadout.
        // Same-profession skill context avoids sharing ambiguous empty bars.
        std::unordered_map<std::string,std::string> skillProfessions;
        for(const auto& l:doc.loadouts) {
            const auto* base=Composable::Find(doc,"base_build",l.refs.at("base_build"));
            skillProfessions[l.refs.at("skills")]=base?base->data.value("profession",std::string()):"";
        }
        for(auto& group:doc.components) {
            std::unordered_map<std::string,std::vector<size_t>> indexed;
            std::unordered_map<std::string,std::string> remap;
            std::vector<Composable::Component> kept;
            for(auto& component:group.second) {
                std::string key=ExactFingerprint(component.data);
                if(group.first=="skills") key+=":"+skillProfessions[component.id];
                auto& candidates=indexed[key];
                const Composable::Component* match=nullptr;
                for(const auto n:candidates)
                    if(ExactContent(kept[n].data,component.data)) {match=&kept[n];break;}
                if(match) remap.emplace(component.id,match->id);
                else {candidates.push_back(kept.size());kept.push_back(std::move(component));}
            }
            for(auto& loadout:doc.loadouts) {
                auto ref=loadout.refs.find(group.first);
                if(ref!=loadout.refs.end()) {
                    auto found=remap.find(ref->second);
                    if(found!=remap.end()) ref->second=found->second;
                }
            }
            group.second=std::move(kept);
        }
        if(!Composable::Validate(doc,error))return false;
        Json rebuilt;
        if(!Composable::ToLegacy(doc,rebuilt,error)||!ExactContent(rebuilt.at("builds"),legacy.at("builds")))
            return Composable::Fail(error,"Exact legacy consolidation changed content");
        output=std::move(doc);return true;
    }catch(const std::exception& e){error=e.what();return false;}
}

bool Store::Open(const std::string& directory,std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);error.clear();
    if(loaded_&&directory_==directory) return true;
    loaded_=false;directory_.clear();snapshot_=Snapshot{};
    try {
        auto verified=OpenOrMigrate(Paths(directory));
        directory_=directory;snapshot_=std::move(verified);loaded_=true;return true;
    } catch(const std::exception& e){error=e.what();return false;}
}
bool Store::Read(Snapshot& snapshot,std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);error.clear();
    if(!loaded_){error="Native library is not open";return false;}
    snapshot=snapshot_;return true;
}
bool Store::Commit(const Snapshot& before,Snapshot after,std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);error.clear();
    if(!loaded_){error="Native library is not open";return false;}
    if(before.revision!=snapshot_.revision){error="Library changed; reopen the edit";return false;}
    try {
        Repository db(Paths(directory_).Library().u8string(),Mode::Existing,ExactFingerprint,false);
        after.revision=db.Commit(snapshot_,after);
        snapshot_=std::move(after);return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
bool Store::Backup(const std::string& destination,std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);error.clear();
    if(!loaded_){error="Native library is not open";return false;}
    try {
        const auto target=fs::u8path(destination);
        if(Exists(target))throw std::runtime_error("Backup already exists; choose a new filename");
        Scratch stage(target.has_parent_path()?target.parent_path():fs::current_path());
        const auto candidate=stage.path/"backup.db";
        {
            Database::Connection src(Paths(directory_).Library().u8string(),Mode::Existing);
            if(src.Scalar("PRAGMA application_id")!=kApplicationId)
                throw std::runtime_error("Invalid source database");
            Database::Connection out(candidate.u8string(),Mode::Create);
            src.BackupTo(out);
        }
        {
            Repository verify(candidate.u8string(),Mode::Existing);
            VerifySnapshot(verify.Read(),snapshot_);
        }
        PublishNew(candidate,target);return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
void Store::Close() {
    std::lock_guard<std::mutex> lock(mutex_);
    loaded_=false;directory_.clear();snapshot_=Snapshot{};
}
Store& RuntimeStore(){static Store store;return store;}

} // namespace AlterEgo::NativeLibrary
