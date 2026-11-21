#include "../src/BuildCompareCore.h"
#include "../src/AutoRefreshCore.h"
#include <chrono>
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


    // Infusions remain a detailed Compare-only mismatch, not a reason
    // to flag equipment incorrect in the Characters summary.
    assert(AlterEgo::IsGearComparisonGroup("Infusions"));
    assert(!AlterEgo::IsCharacterEquipmentMatchGroup("Infusions"));
    assert(AlterEgo::IsCharacterEquipmentMatchGroup("Equipment stats"));
    assert(AlterEgo::IsCharacterEquipmentMatchGroup("Upgrades"));
    assert(AlterEgo::IsCharacterEquipmentMatchGroup("Relic"));
    AlterEgo::BuildComparison infusionMismatch;
    infusionMismatch.Add("Equipment stats", "Ring1", "161", "161", "Berserker", "Berserker");
    infusionMismatch.Add("Infusions", "Ring1", "1", "2", "Infusion A", "Infusion B");
    size_t characterMismatches = 0, detailedMismatches = 0;
    for (const auto& row : infusionMismatch.Rows()) {
        if (AlterEgo::IsGearComparisonGroup(row.group) && !row.matches)
            ++detailedMismatches;
        if (AlterEgo::IsCharacterEquipmentMatchGroup(row.group) && !row.matches)
            ++characterMismatches;
    }
    assert(detailedMismatches == 1);
    assert(characterMismatches == 0);

    // Saved Build Library recognition: a confirmed change restricted to
    // heal/utility/elite skills can be a near-match once gear matches.
    using AlterEgo::TemplateMatchStatus;
    using AlterEgo::ClassifyTemplateMatch;
    AlterEgo::BuildComparison exactTemplate;
    exactTemplate.Add("Specializations", "Line 3", "34", "34", "Reaper", "Reaper");
    exactTemplate.Add("Traits", "Line 3 Adept", "111", "111", "A", "A");
    exactTemplate.Add("Terrestrial skills", "Utility 2", "101", "101", "A", "A");
    exactTemplate.Add("Equipment stats", "Helm", "1", "2", "Berserker", "Assassin");
    assert(ClassifyTemplateMatch(exactTemplate) == TemplateMatchStatus::Exact);
    // Gear is classified independently; gear differences alone must never
    // masquerade as changed skills.
    AlterEgo::BuildComparison skillChange;
    skillChange.Add("Specializations", "Line 3", "34", "34", "Reaper", "Reaper");
    skillChange.Add("Terrestrial skills", "Heal", "10", "11", "Heal A", "Heal B");
    skillChange.Add("Terrestrial skills", "Utility 1", "100", "200", "A", "B");
    skillChange.Add("Terrestrial skills", "Elite", "400", "401", "C", "D");
    skillChange.Add("Aquatic skills", "Utility 3", "50", "51", "E", "F");
    skillChange.Add("Equipment stats", "Helm", "1", "1", "Same", "Same");
    assert(ClassifyTemplateMatch(skillChange) == TemplateMatchStatus::SkillsDiffer);
    skillChange.Add("Traits", "Line 3 Master", "22", "23", "X", "Y");
    assert(ClassifyTemplateMatch(skillChange) == TemplateMatchStatus::Different);
    AlterEgo::BuildComparison unknownSkill;
    unknownSkill.Add("Terrestrial skills", "Utility 2", "0", "200", "(empty)", "B");
    assert(ClassifyTemplateMatch(unknownSkill) == TemplateMatchStatus::Different);
    AlterEgo::BuildComparison nonSkill;
    nonSkill.Add("Build", "Weapon proficiency IDs", "1", "2", "A", "B");
    assert(ClassifyTemplateMatch(nonSkill) == TemplateMatchStatus::Different);
    AlterEgo::BuildComparison unverifiedTemplate;
    unverifiedTemplate.AddUnverified("Traits", "Line 2 Adept", "?", "?");
    assert(ClassifyTemplateMatch(unverifiedTemplate) == TemplateMatchStatus::Different);

    // MumbleLink-only auto-refresh scheduling, independent of GW2 or Nexus.
    using Clock = AlterEgo::AutoRefreshTracker::Clock;
    const auto t0 = Clock::time_point{} + std::chrono::seconds(100);
    AlterEgo::AutoRefreshTracker tracker;
    std::string requestChar;
    auto change = tracker.ObserveIdentity("Nuly", 70, t0, true, true, true);
    assert(change.login && !change.specializationChanged);
    assert(tracker.LiveSpecializationDiffers("Nuly", 71));
    assert(!tracker.LiveSpecializationDiffers("Nuly", 70));
    assert(!tracker.LiveSpecializationDiffers("Other", 71));
    assert(!tracker.LiveSpecializationDiffers("Nuly", 0));
    assert(!tracker.TakeDueRequest(t0 + std::chrono::seconds(7), true, true, false, requestChar));
    assert(!tracker.TakeDueRequest(t0 + std::chrono::seconds(8), true, false, false, requestChar));
    assert(!tracker.TakeDueRequest(t0 + std::chrono::seconds(8), true, true, true, requestChar));
    assert(tracker.TakeDueRequest(t0 + std::chrono::seconds(8), true, true, false, requestChar));
    assert(requestChar == "Nuly");
    assert(!tracker.TakeDueRequest(t0 + std::chrono::seconds(12), true, true, false, requestChar));

    change = tracker.ObserveIdentity("Nuly", 70, t0 + std::chrono::seconds(15), true, true, true);
    assert(!change.login && !change.specializationChanged && tracker.PendingCharacter().empty());
    change = tracker.ObserveIdentity("Nuly", 0, t0 + std::chrono::seconds(18), true, true, true);
    assert(!change.specializationChanged);
    change = tracker.ObserveIdentity("Nuly", 91, t0 + std::chrono::seconds(20), true, true, true);
    assert(!change.login && change.specializationChanged);
    change = tracker.ObserveIdentity("Nuly", 92, t0 + std::chrono::seconds(22), true, true, true);
    assert(change.specializationChanged); // debounce and coalesce rapid switches
    assert(!tracker.TakeDueRequest(t0 + std::chrono::seconds(29), true, true, false, requestChar));
    assert(!tracker.TakeDueRequest(t0 + std::chrono::seconds(30), true, true, false, requestChar));
    assert(tracker.TakeDueRequest(t0 + std::chrono::seconds(98), true, true, false, requestChar));
    assert(requestChar == "Nuly");

    // Disabling auto refresh cancels a queued request.
    tracker.ObserveIdentity("Nuly", 93, t0 + std::chrono::seconds(101), true, true, true);
    assert(!tracker.PendingCharacter().empty());
    assert(!tracker.TakeDueRequest(t0 + std::chrono::seconds(120), false, true, false, requestChar));
    assert(tracker.PendingCharacter().empty());

    // Same-name logout/relogin is observed only after an extended blank identity.
    tracker.ObserveIdentity("", 0, t0 + std::chrono::seconds(130), true, true, true);
    change = tracker.ObserveIdentity("Nuly", 93, t0 + std::chrono::seconds(132), true, true, true);
    assert(!change.login); // transient loading blank
    tracker.ObserveIdentity("", 0, t0 + std::chrono::seconds(140), true, true, true);
    change = tracker.ObserveIdentity("Nuly", 93, t0 + std::chrono::seconds(153), true, true, true);
    assert(change.login);
    assert(tracker.PendingCharacter() == "Nuly");

    // Switching characters discards the previous character's pending work.
    change = tracker.ObserveIdentity("Nul Sul", 202, t0 + std::chrono::seconds(154), true, true, true);
    assert(change.login && tracker.PendingCharacter() == "Nul Sul");
    assert(tracker.LiveSpecializationDiffers("Nul Sul", 201));
    assert(!tracker.LiveSpecializationDiffers("Nuly", 201));

    // Optional settings: an off switch still tracks live specializations.
    AlterEgo::AutoRefreshTracker disabled;
    disabled.ObserveIdentity("Nuly", 70, t0, false, true, true);
    assert(disabled.PendingCharacter().empty());
    change = disabled.ObserveIdentity("Nuly", 71, t0 + std::chrono::seconds(9), false, true, true);
    assert(change.specializationChanged);
    assert(disabled.LiveSpecializationDiffers("Nuly", 70));
}
