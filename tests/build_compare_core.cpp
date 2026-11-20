#include "../src/BuildCompareCore.h"
#include <cassert>
#include <vector>
int main() {
    AlterEgo::BuildComparison c;
    c.Add("Skills", "Heal", "100", "100", "Skill (name available)", "#100 (name missing)");
    c.Add("Gear", "Helm stat", "161", "168", "Berserker's", "Viper's");
    assert(c.Rows().size() == 2);
    assert(c.DifferenceCount() == 1);
    assert(c.Rows()[0].matches && !c.Rows()[1].matches);
    c.AddUnverified("Equipment stats", "Helm", "Assassin's", "Assassin's (not verified)");
    assert(c.Rows().size() == 3);
    assert(!c.Rows()[2].verified && !c.Rows()[2].matches);
    assert(c.DifferenceCount() == 1); // unknown isn't a mismatch
    assert(c.UnverifiedCount() == 1);
    using AlterEgo::StatEquivalence;
    using AlterEgo::CompareStatDefinitions;
    const std::vector<std::string> assassinStats = {"Power", "Precision", "CritDamage"};
    const std::vector<std::string> assassinReordered = {"CritDamage", "Power", "Precision"};
    const std::vector<std::string> otherStats = {"Power", "Precision", "ConditionDamage"};
    // Different GW2 itemstat IDs with the same name + attributes are equivalent.
    assert(CompareStatDefinitions(1050, 1070, "Assassin's", "Assassin's",
        assassinStats, assassinReordered) == StatEquivalence::Matches);
    assert(CompareStatDefinitions(1050, 1050, "", "", {}, {}) == StatEquivalence::Matches);
    // Attribute mismatches and distinct stat names remain genuine differences.
    assert(CompareStatDefinitions(1050, 1070, "Assassin's", "Assassin's",
        assassinStats, otherStats) == StatEquivalence::Different);
    assert(CompareStatDefinitions(1050, 1070, "Assassin's", "Berserker's",
        assassinStats, assassinStats) == StatEquivalence::Different);
    // Same name is insufficient when itemstat metadata is incomplete.
    assert(CompareStatDefinitions(1050, 1070, "Assassin's", "Assassin's",
        {}, assassinReordered) == StatEquivalence::NotVerified);
    assert(CompareStatDefinitions(1050, 1070, "Assassin's", "Assassin's",
        {}, {}) == StatEquivalence::NotVerified);
    assert(CompareStatDefinitions(1050, 1070, "", "",
        assassinStats, assassinStats) == StatEquivalence::NotVerified);
    assert(CompareStatDefinitions(1050, 0, "Assassin's", "",
        assassinStats, {}) == StatEquivalence::NotVerified);

    // Regressions: a shared relic is visible in every equipment tab and must
    // be captured there; ordinary gear and gathering tools must not leak in.
    using AlterEgo::IsEquipmentInCapturedBuild;
    assert(IsEquipmentInCapturedBuild("Relic", 0, 1));
    assert(IsEquipmentInCapturedBuild("Relic", 0, 4));
    assert(IsEquipmentInCapturedBuild("Relic", 3, 3));
    assert(!IsEquipmentInCapturedBuild("Relic", 3, 1));
    assert(IsEquipmentInCapturedBuild("Coat", 2, 2));
    assert(!IsEquipmentInCapturedBuild("Coat", 0, 2));
    assert(!IsEquipmentInCapturedBuild("PowerCore", 0, 1));
    assert(!IsEquipmentInCapturedBuild("Sickle", 0, 1));

    // A true mismatch must be explicit; missing or incomplete data must not
    // be presented as an equipment mismatch.
    using AlterEgo::GearMatchStatus;
    using AlterEgo::ClassifyGearMatch;
    assert(ClassifyGearMatch(true, true, 3, false, true) == GearMatchStatus::Different);
    assert(ClassifyGearMatch(true, true, 3, true, true) == GearMatchStatus::Matches);
    assert(ClassifyGearMatch(true, true, 3, true, false) == GearMatchStatus::NotVerified);
    assert(ClassifyGearMatch(true, true, 0, true, false) == GearMatchStatus::NotVerified);
    assert(ClassifyGearMatch(false, true, 0, true, false) == GearMatchStatus::NoSavedGear);
    assert(ClassifyGearMatch(true, false, 0, true, false) == GearMatchStatus::NoRetrievedGear);

    // Missing a recorded equipment slot is a real, visible mismatch.
    assert(ClassifyGearMatch(true, true, 2, false, false, 1) == GearMatchStatus::MissingItem);
    assert(ClassifyGearMatch(true, true, 2, true, true, 1) == GearMatchStatus::MissingItem);
    // Missing API details must be flagged, but not called a verified match.
    assert(ClassifyGearMatch(true, true, 3, true, true, 0, true) == GearMatchStatus::NotVerified);
    assert(ClassifyGearMatch(true, true, 3, false, true, 0, true) == GearMatchStatus::Different);

    // Variant selection: complete match wins even if another candidate is first.
    using AlterEgo::GearMatchCandidate;
    using AlterEgo::PreferGearMatchCandidate;
    GearMatchCandidate full{GearMatchStatus::Matches, 0, 0, 25, 16};
    GearMatchCandidate oneDiff{GearMatchStatus::Different, 1, 0, 25, 16};
    GearMatchCandidate fourDiff{GearMatchStatus::Different, 4, 0, 25, 16};
    GearMatchCandidate missing{GearMatchStatus::MissingItem, 1, 1, 24, 16};
    GearMatchCandidate unknown{GearMatchStatus::NotVerified, 0, 0, 9, 16};
    GearMatchCandidate notSaved{GearMatchStatus::NoSavedGear, 0, 0, 0, 0};
    GearMatchCandidate notFetched{GearMatchStatus::NoRetrievedGear, 0, 0, 0, 16};
    assert(PreferGearMatchCandidate(full, oneDiff));
    assert(!PreferGearMatchCandidate(oneDiff, full));
    assert(PreferGearMatchCandidate(oneDiff, fourDiff));
    assert(PreferGearMatchCandidate(oneDiff, missing));
    assert(PreferGearMatchCandidate(missing, unknown));
    assert(PreferGearMatchCandidate(unknown, notFetched));
    assert(PreferGearMatchCandidate(notFetched, notSaved));
    assert(!PreferGearMatchCandidate(oneDiff, oneDiff)); // stable library order
    assert(PreferGearMatchCandidate(
        GearMatchCandidate{GearMatchStatus::NotVerified, 0, 0, 12, 15}, unknown));
}
