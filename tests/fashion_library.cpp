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
    assert(verified.Remove(0));
    NativeLibrary::RuntimeStore().Close();
    std::filesystem::remove_all(file);
    return 0;
}
