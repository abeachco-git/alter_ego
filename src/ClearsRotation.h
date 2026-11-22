#pragma once
// GW2 published 12-day raid bounty and 15-day fractal rotations.
// Sources: https://wiki.guildwars2.com/wiki/Daily_Raid_Bounties
// https://wiki.guildwars2.com/wiki/Template:Daily_Fractal_Schedule
// https://wiki.guildwars2.com/wiki/Template:Daily_Recommended_Fractal_Schedule
// Date anchors checked against the November 21, 2026 wiki bounty
// schedule and GW2Treasures live daily fractal listing. UTC throughout.
// No network activity or user/account data is needed here.
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

namespace ClearsRotation {
static constexpr int64_t kDay = 86400;
static constexpr int64_t kAnchorUtc = 1795219200; // 2026-11-21 00:00 UTC
inline int Mod(int64_t a, int b) { return static_cast<int>((a % b + b) % b); }
inline int64_t FloorDay(int64_t seconds) {
    return seconds >= 0 ? (seconds / kDay) : ((seconds - (kDay - 1)) / kDay);
}

inline int BountySet(int64_t utcSeconds) {
    // Nov 21 was set 2 (zero-based 1). The two six-item columns repeat
    // after six days, while the other two repeat after twelve.
    return Mod(FloorDay(utcSeconds) - FloorDay(kAnchorUtc) + 1, 12);
}
inline const char* BountyName(int set, int column) {
    static const char* kBosses[4][12] = {
        {"Shiverpeaks Pass","Voice and Claw of the Fallen","Fraenir of Jormag",
         "Gorseval","Cairn","Mursaat Overseer","Shiverpeaks Pass",
         "Voice and Claw of the Fallen","Fraenir of Jormag","Gorseval",
         "Cairn","Mursaat Overseer"},
        {"Aetherblade Hideout","Cardinal Sabir","Whisper of Jormag",
         "Vale Guardian","Cosmic Observatory","Cold War","Boneskinner",
         "Sabetha","Xunlai Jade Junkyard","Temple of Febe",
         "Keep Construct","Kela"},
        {"Slothasor","Matthias","Xera","Samarog",
         "Conjured Amalgamate","Twin Largos","Decima","Cardinal Adina",
         "Old Lion's Court","Ura","Kaineng Overlook","Deimos"},
        {"Qadim","Qadim the Peerless","Soulless Horror","Harvest Temple",
         "Dhuum","Greer","Qadim","Qadim the Peerless",
         "Soulless Horror","Harvest Temple","Dhuum","Greer"}
    };
    if (set < 0 || set >= 12 || column < 0 || column >= 4) return "";
    return kBosses[column][set];
}
inline std::string Normalize(const std::string& name) {
    std::string s;
    for (unsigned char ch : name)
        if (std::isalnum(ch)) s.push_back(static_cast<char>(std::tolower(ch)));
    // Wiki and weekly achievement names for the same encounter differ.
    if (s == "sloth") return "slothasor";
    if (s == "mursaat") return "mursaatoverseer";
    if (s == "adina") return "cardinaladina";
    if (s == "sabir") return "cardinalsabir";
    if (s == "gorsevalthemultifarious") return "gorseval";
    if (s == "sabethathesaboteur") return "sabetha";
    if (s == "cairntheindomitable") return "cairn";
    if (s == "matthiasgabrel") return "matthias";
    if (s == "voiceofthefallenandclawofthefallen") return "voiceandclawofthefallen";
    if (s == "raidbountyvoiceandclawofthefallen") return "voiceandclawofthefallen";
    if (s.rfind("raidbounty", 0) == 0) return Normalize(s.substr(10));
    return s;
}
inline std::string DateLabel(int64_t midnightUtc) {
    std::time_t ts = static_cast<std::time_t>(midnightUtc);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &ts);
#else
    gmtime_r(&ts, &tm);
#endif
    char text[16];
    std::snprintf(text, sizeof(text), "%02d/%02d", tm.tm_mon + 1, tm.tm_mday);
    return text;
}
// Raid-bounty labels include the weekday and abbreviated month so they
// cannot be mistaken for a numerical completed/total progress fraction.
inline std::string BountyDateLabel(int64_t midnightUtc) {
    std::time_t ts = static_cast<std::time_t>(midnightUtc);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &ts);
#else
    gmtime_r(&ts, &tm);
#endif
    static constexpr const char* kWeekdays[] = {
        "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
    };
    static constexpr const char* kMonths[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    if (tm.tm_wday < 0 || tm.tm_wday > 6 ||
        tm.tm_mon < 0 || tm.tm_mon > 11) return "";
    char text[24]{};
    std::snprintf(text, sizeof(text), "%s %s %d",
        kWeekdays[tm.tm_wday], kMonths[tm.tm_mon], tm.tm_mday);
    return text;
}
// Shows today unless that bounty has been completed; otherwise the NEXT
// occurrence only, stopping before weekly reset. Weekly raid completion is
// deliberately ignored: the daily bounty is a separate reward.
inline std::string NextBountyDate(const std::string& encounter,
                                  int64_t nowUtc,
                                  int64_t nextWeeklyResetUtc,
                                  bool bountyCompletedToday) {
    const std::string needle = Normalize(encounter);
    const int64_t todayUtc = FloorDay(nowUtc) * kDay;
    if (needle.empty() || nextWeeklyResetUtc <= nowUtc) return "";
    for (int daysAhead = 0; daysAhead <= 8; ++daysAhead) {
        int64_t start = todayUtc + static_cast<int64_t>(daysAhead) * kDay;
        if (start >= nextWeeklyResetUtc) break;
        if (daysAhead == 0 && bountyCompletedToday) continue;
        for (int column = 0; column < 4; ++column)
            if (Normalize(BountyName(BountySet(start), column)) == needle)
                return BountyDateLabel(start);
    }
    return "";
}

struct FractalSet {
    std::array<const char*, 3> dailies;
    std::array<int, 3> recScales;
    std::array<const char*, 3> recNames;
};
inline const FractalSet& Fractals(int index) {
    static const FractalSet kSets[15] = {
        {{{"Nightmare","Snowblind","Volcanic"}}, {{2,37,53}}, {{"Uncategorized","Siren's Reef","Underground Facility"}}},
        {{{"Aetherblade","Thaumanova Reactor","Uncategorized"}}, {{6,28,61}}, {{"Cliffside","Volcanic","Aquatic Ruins"}}},
        {{{"Chaos","Cliffside","Twilight Oasis"}}, {{10,32,65}}, {{"Molten Boss","Swampland","Aetherblade"}}},
        {{{"Captain Mai Trin Boss","Deepstone","Silent Surf"}}, {{14,34,74}}, {{"Aetherblade","Thaumanova Reactor","Sunqua Peak"}}},
        {{{"Nightmare","Solitary Throne","Solid Ocean"}}, {{1,50,70}}, {{"Volcanic","Lonely Tower","Kinfall"}}},
        {{{"Chaos","Uncategorized","Urban Battleground"}}, {{15,48,60}}, {{"Thaumanova Reactor","Shattered Observatory","Solid Ocean"}}},
        {{{"Deepstone","Molten Furnace","Siren's Reef"}}, {{24,35,66}}, {{"Sunqua Peak","Solid Ocean","Silent Surf"}}},
        {{{"Molten Boss","Twilight Oasis","Underground Facility"}}, {{21,36,75}}, {{"Silent Surf","Uncategorized","Lonely Tower"}}},
        {{{"Silent Surf","Swampland","Volcanic"}}, {{7,40,67}}, {{"Aquatic Ruins","Molten Boss","Deepstone"}}},
        {{{"Aquatic Ruins","Lonely Tower","Thaumanova Reactor"}}, {{8,31,54}}, {{"Underground Facility","Urban Battleground","Siren's Reef"}}},
        {{{"Sunqua Peak","Underground Facility","Urban Battleground"}}, {{11,39,59}}, {{"Deepstone","Molten Furnace","Twilight Oasis"}}},
        {{{"Aetherblade","Chaos","Nightmare"}}, {{18,27,64}}, {{"Captain Mai Trin Boss","Snowblind","Solitary Throne"}}},
        {{{"Cliffside","Lonely Tower","Kinfall"}}, {{4,30,58}}, {{"Urban Battleground","Chaos","Molten Furnace"}}},
        {{{"Deepstone","Solid Ocean","Swampland"}}, {{16,42,62}}, {{"Twilight Oasis","Captain Mai Trin Boss","Uncategorized"}}},
        {{{"Captain Mai Trin Boss","Molten Boss","Shattered Observatory"}}, {{5,47,68}}, {{"Swampland","Nightmare","Cliffside"}}},
    };
    return kSets[Mod(index, 15)];
}
// The rotation table is fixed, but its absolute date anchor can drift if
// ArenaNet adds/removes a fractal. Resolve TODAY's index using the three
// actual daily fractal names returned by H&S before forecasting tomorrow.
// We require exactly three unique names and exactly one matching set.
inline int FindDailyFractalSet(const std::vector<std::string>& currentDailyNames) {
    if (currentDailyNames.size() != 3) return -1;
    std::array<std::string, 3> live{};
    for (size_t i = 0; i < live.size(); ++i) {
        live[i] = Normalize(currentDailyNames[i]);
        if (live[i].empty()) return -1;
    }
    if (live[0] == live[1] || live[1] == live[2] || live[0] == live[2])
        return -1;
    int match = -1;
    for (int index = 0; index < 15; ++index) {
        const auto& set = Fractals(index);
        bool allMatch = true;
        for (const std::string& value : live) {
            bool found = false;
            for (const char* name : set.dailies)
                if (Normalize(name) == value) { found = true; break; }
            if (!found) { allMatch = false; break; }
        }
        if (allMatch) {
            if (match >= 0) return -1; // ambiguous: don't predict
            match = index;
        }
    }
    return match;
}
inline const FractalSet* NextFractalsFromLive(const std::vector<std::string>& today) {
    const int index = FindDailyFractalSet(today);
    return index < 0 ? nullptr : &Fractals(index + 1);
}
// The date-anchored helpers remain for compatibility/regression tests; the
// UI must use NextFractalsFromLive rather than trusting this anchor.
inline int FractalSetIndex(int64_t utcSeconds) {
    return Mod(FloorDay(utcSeconds) - FloorDay(kAnchorUtc) + 9, 15);
}
inline const FractalSet& NextFractals(int64_t utcSeconds) {
    return Fractals(FractalSetIndex(utcSeconds) + 1);
}
} // namespace ClearsRotation
