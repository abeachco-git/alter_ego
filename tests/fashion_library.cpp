#include "../src/FashionLibrary.h"
#include <cassert>
#include <cstdio>
#include <string>
int main() {
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
    DecodedWardrobeLink decoded{};
    assert(DecodeValid(link, decoded));
    assert(decoded.helm_dyes[0] == 1);
    assert(decoded.helm_dyes[1] == 217);
    assert(decoded.outfit_dyes[3] == 42);
    assert(decoded.visibility_flags == 0x14fa);
    assert(!DecodeValid("[&Dw==]", decoded));
    assert(!DecodeValid("[&D1gD***]", decoded));
    assert(CanonicalLink(std::string("Link: ") + link) == link);
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
    Look restored;
    assert(FromJson(nlohmann::json::object({{"id","abc"}, {"name","Test Wardrobe"},
        {"wardrobe_link",link}, {"characters",look.characters}}), restored));
    assert(restored.characters.size() == 2);
    const std::string file = "/tmp/alter_ego_fashion_regression.json";
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
    assert(reloaded.Remove(0));
    std::remove(file.c_str());
}
