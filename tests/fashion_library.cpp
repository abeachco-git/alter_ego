#include "../src/FashionLibrary.h"
#include <cassert>
#include <cstdio>
#include <string>
int case_fashion_library(int, char**) {
    using namespace AlterEgo;
    using namespace AlterEgo::Fashion;
    DecodedWardrobeLink original{};
    original.helm_skin = 113;
    original.helm_dyes[0] = 1;   // Dye Remover is distinct from missing
    original.helm_dyes[1] = 217;
    original.chest_skin = 125;
    original.chest_dyes[0] = 300;
    original.outfit_id = 81;
    original.outfit_dyes[3] = 42;
    original.weapon_a_main = 321;
    original.visibility_flags = 0x14fa;
    const std::string link = ChatLink::EncodeWardrobe(original);
    // Real 2026 in-game wardrobe template sample: verify complete 97-byte
    // payload compatibility, not only our own encoder's generated links.
    const std::string gameLink =
        "[&D1gDMhwBAAEAAQABAHEdUAALAFAANwDgHFAACwA3ADcAHx5QAAsAYQX5BBAePAALAHIAPADCHQsANwALAFAAAx0LAFAAYQX5BAMAAQABAAEAAQA/EqISXxIAAGoZnR39fg==]";
    DecodedWardrobeLink gameDecoded{};
    assert(DecodeValid(gameLink, gameDecoded));
    assert(CanonicalLink(gameLink) == gameLink);
    DecodedWardrobeLink decoded{};
    assert(DecodeValid(link, decoded));
    assert(decoded.helm_dyes[0] == 1);
    assert(decoded.helm_dyes[1] == 217);
    assert(decoded.outfit_dyes[3] == 42);
    assert(decoded.visibility_flags == 0x14fa);
    assert(!DecodeValid("[&Dw==]", decoded));
    assert(!DecodeValid("[&D1gD***]", decoded));
    assert(CanonicalLink(std::string("Link: ") + link) == link);
    Look captured;
    assert(MakeLiveLook(link,"Alt outfit","Character One",3,captured));
    assert(captured.name=="Alt outfit"&&captured.source_tab==3&&captured.source_character=="Character One");
    assert(captured.origin=="live_equipment"&&!captured.complete&&captured.characters.size()==1);
    assert(captured.wardrobe_link==link);
    assert(!MakeLiveLook(link,"Alt outfit","Character One",0,captured));
    DecodedWardrobeLink empty{};
    assert(!MakeLiveLook(ChatLink::EncodeWardrobe(empty),"Empty","Character One",2,captured));
    assert(captured.source_tab==3);
    const CharacterIdentity::Evidence birth{"2020-01-02T03:04:05Z","Guardian","Human"};
    captured.id="captured";
    captured.source_identity=CharacterIdentity::Provenance{"A","Character One",birth};
    assert(AssociateVerified(captured,*captured.source_identity));
    assert(!AssociateVerified(captured,{"A","Renamed",birth}));
    assert(AssociateVerified(captured,{"B","Character One",birth}));
    const auto encoded=ToJson(captured);
    Look decodedLook;
    assert(FromJson(encoded,decodedLook)&&ToJson(decodedLook)==encoded);
    assert(MatchesSearch(decodedLook,AlterEgo::Search::Words("Renamed"),{"Renamed"}));
    assert(!MatchesSearch(decodedLook,"Renamed"));
    auto invalidAssociation=encoded;
    invalidAssociation["character_associations"][0]["local_id"]=1;
    assert(!FromJson(invalidAssociation,decodedLook));
    Look look;
    look.name = "Test Wardrobe";
    look.weight = "Heavy";
    look.wardrobe_link = link;
    Associate(look, "Character One");
    Associate(look, "Character Two");
    Associate(look, "Character One");
    assert(look.characters.size() == 2);
    assert(MatchesSearch(look, "two"));
    assert(MatchesSearch(look, "HEAVY"));
    assert(!MatchesSearch(look, "Ranger"));
    const auto serialized = ToJson(look);
    assert(serialized.at("name") == "Test Wardrobe");
    Look restored;
    assert(FromJson(nlohmann::json::object({{"id","abc"}, {"name","Test Wardrobe"},
        {"wardrobe_link",link}, {"characters",look.characters}}), restored));
    assert(restored.characters.size() == 2);
    const std::string file=(std::filesystem::temp_directory_path()/"alter_ego_fashion_regression_db").u8string();
    std::filesystem::remove_all(file);
    Library lib;
    assert(lib.Load(file));
    int first = lib.Add(look);
    assert(first == 0);
    assert(lib.FindExact(link) == 0);
    Library reloaded;
    assert(reloaded.Load(file));
    assert(reloaded.looks.size() == 1);
    assert(reloaded.looks[0].characters.size() == 2);
    assert(reloaded.looks[0].wardrobe_link == link);
    const auto baseline=reloaded.looks[0];
    auto edited=baseline;edited.notes="Saved draft";
    auto abandoned=baseline;abandoned.notes="Cancelled";
    assert(reloaded.looks[0].notes!=abandoned.notes);
    assert(reloaded.UpdateDraft(baseline,edited));
    assert(reloaded.looks[0].id==baseline.id&&reloaded.looks[0].notes=="Saved draft");
    // Stale Fashion drafts cannot overwrite the already committed edit.
    assert(!lib.UpdateDraft(baseline,abandoned));
    Library verified;assert(verified.Load(file));
    assert(verified.looks[0].notes=="Saved draft");
    assert(!verified.RemoveDraft(baseline));
    const auto current=verified.looks[0];
    Look unrelated=look;unrelated.name="Other";assert(lib.Add(unrelated)==1); // stale projection cannot overwrite edited row
    assert(lib.looks[0].notes=="Saved draft");
    assert(verified.RemoveDraft(current));assert(verified.looks.size()==1&&verified.looks[0].name=="Other");
    assert(!verified.RemoveDraft(current));
    const auto other=verified.looks.front();
    assert(verified.DuplicateDraft(other,"Other - Copy")==1);
    const auto cloned=verified.looks.back();
    assert(cloned.id!=other.id&&cloned.wardrobe_link==other.wardrobe_link&&cloned.characters==other.characters);
    assert(cloned.notes==other.notes&&cloned.extension==other.extension&&cloned.complete==other.complete);
    assert(verified.RemoveDraft(cloned));
    // Reviewed optional-reference detachment must be atomic and must not
    // silently detach a newly linked Loadout or a differently named owner.
    auto& store=NativeLibrary::RuntimeStore();
    NativeLibrary::Snapshot before;std::string error;
    assert(store.Read(before,error));auto graph=before;
    const Composable::Json legacy={{"builds",Composable::Json::array({
        {{"id","owner"},{"name","Owner"},{"profession","Necromancer"},
         {"skills",{{"heal",0},{"utilities",{0,0,0}},{"elite",0}}}}
    })}};
    assert(Composable::SplitLegacy(legacy,graph.loadouts,error));
    auto* owner=Composable::FindLoadout(graph.loadouts,"owner");assert(owner);
    owner->fashion_id=other.id;
    assert(store.Commit(before,graph,error));
    const auto reviewed=Composable::FashionUsers(graph.loadouts,other.id);
    assert(reviewed.size()==1);
    assert(!verified.RemoveDraft(other)); // unreviewed references are protected
    assert(store.Read(before,error));graph=before;
    assert(Composable::DuplicateLoadout(graph.loadouts,"owner","second-owner","Second Owner",42,error));
    assert(store.Commit(before,graph,error));
    assert(!verified.RemoveDraft(other,reviewed)); // a reference was added
    assert(store.Read(before,error));
    assert(before.fashion.size()==1&&Composable::FashionUsers(before.loadouts,other.id).size()==2);
    const auto reviewedBoth=Composable::FashionUsers(before.loadouts,other.id);
    const auto originalGraph=before.loadouts;
    assert(verified.RemoveDraft(other,reviewedBoth));
    assert(store.Read(before,error)&&before.fashion.empty());
    assert(before.loadouts.loadouts.size()==2);
    for(auto& l:before.loadouts.loadouts)assert(l.fashion_id.empty());
    auto expectedGraph=originalGraph;
    for(auto& l:expectedGraph.loadouts)l.fashion_id.clear();
    assert(Composable::ToJson(before.loadouts)==Composable::ToJson(expectedGraph));
    NativeLibrary::RuntimeStore().Close();
    std::filesystem::remove_all(file);
    return 0;
}
