#include "../src/ComposableLoadouts.h"
#include "../src/ComposableSemantics.h"
#include "../src/ComposableStore.h"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

using namespace AlterEgo::Composable;
namespace fs = std::filesystem;

static const Json kExample = Json::parse(R"({
  "builds": [{
    "id":"one", "name":"Mechanist", "profession":"Engineer",
    "game_mode":"Raid", "game_modes":["Raid","Fractal"], "notes":"Keep this",
    "source_url":"https://example.invalid/source", "source_character":"Hero",
    "chat_link":"[&abc]", "ae2_code":"AE2:abc",
    "created":123,
    "specializations":[{"id":12,"traits":[1,2,3]}],
    "skills":{"heal":123,"utilities":[1,2,3],"elite":99},
    "aquatic_skills":{"heal":4,"utilities":[4,5,6],"elite":77},
    "pets":[0,0,0,0], "legend_codes":[0,0,0,0], "weapons":[7,8],
    "gear":{
      "WeaponA1":{"item_id":10,"stat_id":1130,"weapon_type":"Pistol",
                  "sigil_id":100,"infusion_ids":[999],"infusion_snapshot":true},
      "WeaponA2":{"item_id":11,"stat_id":1130,"sigil_id":101},
      "WeaponB1":{"item_id":20,"stat_id":1118,"sigil_id":200},
      "Helm":{"item_id":1,"stat_id":1130,"rune_id":300,"infusion_ids":[888]},
      "Coat":{"item_id":2,"stat_id":1130,"rune_id":300},
      "Ring1":{"item_id":3,"stat_id":1118,"infusion_ids":[999]},
      "FutureSlot":{"future_field":{"preserve":true}}
    },
    "rune_name":"Test Rune","rune_id":300,
    "relic_name":"Relic A","relic_id":400,
    "infusion_totals":[{"item_id":999,"count":2,"attribute":"Power"}],
    "infusion_summary_present":true, "infusion_ar_total":162,
    "infusion_stat_totals":{"Power":10},
    "future_extension":{"hello":"world"}
  }, {
    "id":"two","name":"Skills only","profession":"Necromancer",
    "skills":{"heal":5,"utilities":[7,8,9],"elite":10}
  }]
})");

static void Put(const fs::path& path, const std::string& raw) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    assert(out);
    out << raw;
    out.close();
    assert(out.good());
}
static std::string Get(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    assert(in);
    return std::string(std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>());
}
static fs::path FreshDir(const std::string& suffix) {
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = fs::temp_directory_path() /
        ("ae-loadout-test-" + suffix + "-" + std::to_string(ticks));
    assert(fs::create_directories(path));
    return path;
}
int main() {
    std::string error;
    Document document;
    assert(SplitLegacy(kExample, document, error));
    assert(document.loadouts.size() == 2);
    assert(document.components.size() == 9);
    for (const auto& kind : Kinds()) assert(document.components[kind].size() == 2);
    Json reconstructed;
    assert(ToLegacy(document, reconstructed, error));
    assert(reconstructed["builds"] == kExample["builds"]);
    Document reloaded;
    assert(FromJson(ToJson(document), reloaded, error));
    assert(ToLegacy(reloaded, reconstructed, error));
    assert(reconstructed["builds"] == kExample["builds"]);
    assert(document.loadouts[0].metadata["future_extension"]["hello"] == "world");
    assert(Find(document,"weapons","weapons:one")->data["gear"].contains("WeaponA1"));
    assert(Find(document,"armor","armor:one")->data["gear"].contains("Helm"));
    assert(Find(document,"trinkets","trinkets:one")->data["gear"].contains("FutureSlot"));
    assert(Find(document,"runes","runes:one")->data["gear"]["Helm"]["rune_id"] == 300);
    assert(Find(document,"sigils","sigils:one")->data["gear"]["WeaponA1"]["sigil_id"] == 100);
    assert(Find(document,"infusions","infusions:one")->data["gear"]["Helm"]["infusion_ids"][0] == 888);
    assert(Find(document,"relic","relic:one")->data["relic_id"] == 400);
    assert(Find(document,"base_build","base_build:one")->data["weapons"] == Json::array({7,8}));
    assert(Find(document,"skills","skills:one")->data["skills"]["utilities"] == Json::array({1,2,3}));

    // Fashion is an optional reference, not copied equipment or overwritten
    // by a legacy Save call.
    assert(LinkFashion(document, "one", "look-123", error));
    assert(document.loadouts[0].fashion_id == "look-123");

    // Components can be assigned across Loadouts.
    assert(Assign(document,"two","relic","relic:one",error));
    assert(Find(document, "relic", document.loadouts[1].refs["relic"])->data["relic_id"] == 400);
    assert(CopyForLoadout(document,"two","relic","relic:two-private","Separate relic",error));
    assert(UpdateShared(document,"relic","relic:two-private",
                        Json{{"relic_id",401},{"relic_name","Other Relic"}},error));
    assert(Find(document,"relic","relic:one")->data["relic_id"] == 400);
    assert(Find(document,"relic","relic:two-private")->data["relic_id"] == 401);

    // Legacy UI edits detach only the affected shared component.
    Json oldView;
    assert(ToLegacy(document,oldView,error));
    oldView["builds"][1]["relic_id"] = 402;
    Document reconciled;
    assert(Reconcile(document,oldView,reconciled,error));
    assert(Find(reconciled,"relic","relic:one")->data["relic_id"] == 400);
    assert(ToLegacy(reconciled,reconstructed,error));
    assert(reconstructed == oldView);
    assert(reconciled.loadouts[0].fashion_id == "look-123");
    // The legacy SavedBuild serializer may not understand a newer field.
    // An edit from that UI must not delete unknown imported metadata.
    Json olderEditorView = oldView;
    olderEditorView["builds"][0].erase("future_extension");
    olderEditorView["builds"][0]["notes"] = "UI edit";
    Document futureSafe;
    assert(Reconcile(document, olderEditorView, futureSafe, error));
    assert(ToLegacy(futureSafe, reconstructed, error));
    assert(reconstructed["builds"][0]["future_extension"]["hello"] == "world");
    assert(reconstructed["builds"][0]["notes"] == "UI edit");


    // Utility positions may be swapped without a semantic mismatch.
    assert(Semantics::SameUtilities(
         Json{{"heal",1},{"elite",3},{"utilities",Json::array({7,8,9})}},
         Json{{"heal",1},{"elite",3},{"utilities",Json::array({9,7,8})}}));
    assert(!Semantics::SameUtilities(
         Json{{"heal",1},{"elite",3},{"utilities",Json::array({7,8,9})}},
         Json{{"heal",2},{"elite",3},{"utilities",Json::array({9,7,8})}}));

    // Infusion locations do not affect aggregate matching.
    auto infA = Find(document,"infusions","infusions:one")->data;
    auto infB = infA;
    infB["gear"]["Helm"]["infusion_ids"] = Json::array({999});
    infB["gear"]["Ring1"]["infusion_ids"] = Json::array({888});
    assert(Semantics::Compare(Semantics::Infusions(infA),
                              Semantics::Infusions(infB)) == Semantics::Verdict::Match);

    auto sigA = Find(document,"sigils","sigils:one")->data;
    auto sigB = sigA;
    sigB["gear"]["WeaponA1"]["sigil_id"] = 101;
    sigB["gear"]["WeaponA2"]["sigil_id"] = 100;
    assert(Semantics::Compare(Semantics::Sigils(sigA),
                              Semantics::Sigils(sigB)) == Semantics::Verdict::Match);
    sigB["gear"]["WeaponB1"]["sigil_id"] = 101;
    assert(Semantics::Compare(Semantics::Sigils(sigA),
                              Semantics::Sigils(sigB)) == Semantics::Verdict::Mismatch);

    // Every Compare row is routed to the exact owning v3 component, not
    // the former combined Armor/Trinkets bucket. Relic's *physical* slot is
    // a trinket while its effect/name is the independent Relic component.
    using Semantics::ComparisonComponent;
    assert(ComparisonComponent("Specializations", "Line 1") == "base_build");
    assert(ComparisonComponent("Aquatic skills", "Utilities") == "skills");
    assert(ComparisonComponent("Equipment stats", "WeaponA1") == "weapons");
    assert(ComparisonComponent("Equipment", "Helm") == "armor");
    assert(ComparisonComponent("Equipment stats", "Ring1") == "trinkets");
    assert(ComparisonComponent("Exact item IDs", "Relic") == "trinkets");
    assert(ComparisonComponent("Relic", "Effect / item name") == "relic");
    assert(ComparisonComponent("Runes", "Types / counts") == "runes");
    assert(ComparisonComponent("Sigils", "By weapon set") == "sigils");
    assert(ComparisonComponent("Infusions", "Types / counts") == "infusions");
    assert(ComparisonComponent("Infusion totals", "Agony Resistance") == "infusions");
    assert(ComparisonComponent("Future group", "unknown").empty());

    // A native editor changes only selected fields; unknown fields are
    // preserved by UpdateShared and by the backwards-compatible projection.
    Json withExtension = Find(document, "armor", "armor:one")->data;
    withExtension["gear"]["Helm"]["future_stat"] = {{"source","third-party"}};
    withExtension["gear"]["Helm"]["stat_name"] = "Berserker";
    assert(UpdateShared(document, "armor", "armor:one", withExtension, error));
    assert(ToLegacy(document, reconstructed, error));
    assert(reconstructed["builds"][0]["gear"]["Helm"]["future_stat"]["source"] ==
           "third-party");
    assert(reconstructed["builds"][0]["gear"]["Helm"]["stat_name"] == "Berserker");

    // Renaming and changing a shared component is one transaction.
    // Failure must not change either its name or its payload.
    Document nameEdit = document;
    const Json originalSnapshot = ToJson(nameEdit);
    const auto* prior = Find(nameEdit,"runes","runes:one");
    assert(prior);
    assert(!UpdateSharedNamed(nameEdit, "runes", "runes:one",
                              "", prior->data, error));
    assert(ToJson(nameEdit) == originalSnapshot);
    Json changedRune = prior->data;
    changedRune["rune_name"] = "Updated Rune";
    assert(UpdateSharedNamed(nameEdit, "runes", "runes:one",
                             "Reusable Rune", changedRune, error));
    const auto* updatedRune = Find(nameEdit,"runes","runes:one");
    assert(updatedRune && updatedRune->name == "Reusable Rune");
    assert(updatedRune->data["rune_name"] == "Updated Rune");
    assert(document.loadouts[0].refs.at("runes") == "runes:one");

    // Never overwrite a shared skill component with another profession's
    // captured skills, even when a future UI bypasses the dropdown filter.
    Document professionSafety = document;
    assert(Assign(professionSafety, "two", "skills", "skills:one", error));
    assert(!CaptureProfessionCompatible(professionSafety,
        "skills", "skills:one", "Engineer", error));
    assert(CaptureProfessionCompatible(professionSafety,
        "skills", "skills:one", "Necromancer", error) == false);
    assert(!CaptureProfessionCompatible(document,
        "base_build", "base_build:one", "Necromancer", error));
    assert(CaptureProfessionCompatible(document,
        "base_build", "base_build:one", "Engineer", error));
    assert(CaptureProfessionCompatible(document,
        "armor", "armor:one", "Necromancer", error));

    // Save Active replacement drops stale known gear/upgrade fields while
    // retaining foreign extension metadata and preserving other components.
    Json previousCapture = {
        {"gear", {{"Helm", {{"rune_id", 111},
                            {"custom_mod", {{"label", "keep"}}}}}}},
        {"extension", {{"source", "imported"}}}
    };
    Json newCapture = {{"gear", {{"Helm", {{"rune_id", 222}}}}}};
    const Json overlay = ReplaceCapturedFieldsPreservingExtensions(
        previousCapture, newCapture);
    assert(overlay["gear"]["Helm"]["rune_id"] == 222);
    assert(overlay["gear"]["Helm"]["custom_mod"]["label"] == "keep");
    assert(overlay["extension"]["source"] == "imported");
    Json cleared = ReplaceCapturedFieldsPreservingExtensions(
        previousCapture, Json::object());
    assert(cleared["gear"]["Helm"].contains("custom_mod"));
    assert(!cleared["gear"]["Helm"].contains("rune_id"));
    assert(cleared["extension"]["source"] == "imported");

    // Portable component packages have no stable ID. Importing them never
    // overwrites an existing shared component or changes existing references.
    const Component* reference = Find(document, "armor", "armor:one");
    assert(reference);
    Json exported = ExportComponent("armor", *reference);
    assert(exported["format"] == kComponentFormat);
    assert(!exported.contains("id"));
    std::string importedKind;
    Component portable;
    assert(ImportComponent(exported, importedKind, portable, error));
    assert(importedKind == "armor");
    assert(portable.id.empty());
    assert(portable.data == reference->data);
    assert(portable.data["gear"]["Helm"]["future_stat"]["source"] == "third-party");
    Json wrong = exported;
    wrong["kind"] = "weapons";
    assert(!ImportComponent(wrong, importedKind, portable, error));
    wrong = exported;
    wrong["version"] = kComponentVersion + 1;
    assert(!ImportComponent(wrong, importedKind, portable, error));
    wrong = exported;
    wrong["data"]["gear"]["Helm"]["sigil_id"] = 123;
    assert(!ImportComponent(wrong, importedKind, portable, error));
    wrong = exported;
    wrong["data"]["skills"] = Json::object();
    assert(!ImportComponent(wrong, importedKind, portable, error));
    assert(Find(document, "armor", "armor:one")->data == reference->data);

    // Corruption and broken references fail without damaging caller state.
    Json bad = ToJson(document);
    bad["version"] = 999;
    Document untouched = document;
    assert(!FromJson(bad,untouched,error));
    assert(ToJson(untouched) == ToJson(document));
    bad = ToJson(document);
    bad["loadouts"][0]["refs"]["relic"] = "nonexistent";
    assert(!FromJson(bad,untouched,error));
    assert(ToJson(untouched) == ToJson(document));

    // Byte-identical source backup, verified migration and atomic persistence.
    const fs::path dir = FreshDir("migration");
    const std::string original = kExample.dump(1);
    Put(dir / Store::kLegacy, original);
    bool migrated = false;
    Document opened;
    assert(Store::Open(dir.u8string(),opened,migrated,error));
    assert(migrated);
    assert(Get(dir / Store::kBefore) == original);
    assert(FromJson(Json::parse(Get(dir / Store::kFile)), reloaded, error));
    assert(Store::Save(dir.u8string(), opened, error));
    assert(fs::exists(dir / Store::kPrevious));
    // A newer legacy write from the old main DLL is reconciled into v3,
    // never allowed to erase the existing component graph or its fashion refs.
    assert(LinkFashion(opened, "one", "appearance-1", error));
    assert(Store::Save(dir.u8string(), opened, error));
    Json external = Json::parse(Get(dir / Store::kLegacy));
    external["builds"][0]["notes"] = "Edited by old DLL";
    Put(dir / Store::kLegacy, external.dump(2));
    fs::last_write_time(dir / Store::kLegacy,
        fs::last_write_time(dir / Store::kFile) + std::chrono::seconds(3));
    migrated = false;
    assert(Store::Open(dir.u8string(),opened,migrated,error));
    assert(migrated);
    assert(opened.loadouts[0].fashion_id == "appearance-1");
    assert(opened.loadouts[0].metadata["notes"] == "Edited by old DLL");
    // Always load the existing canonical data if the mirror is unchanged.
    migrated = true;
    assert(Store::Open(dir.u8string(),opened,migrated,error));
    assert(!migrated);
    // Refuse to replace a corrupt canonical file, preserving the bytes.
    Put(dir / Store::kFile, "{ invalid }");
    const auto corrupted = Get(dir / Store::kFile);
    assert(!Store::Save(dir.u8string(),reloaded,error));
    assert(Get(dir / Store::kFile) == corrupted);
    assert(!Store::Open(dir.u8string(),opened,migrated,error));
    fs::remove_all(dir);

    // Invalid legacy data must never be replaced by an empty v3 document.
    const fs::path badDir = FreshDir("broken");
    Put(badDir / Store::kLegacy, "{invalid}");
    assert(!Store::Open(badDir.u8string(),opened,migrated,error));
    assert(!fs::exists(badDir / Store::kFile));
    assert(!fs::exists(badDir / Store::kBefore));
    fs::remove_all(badDir);
    return 0;
}
