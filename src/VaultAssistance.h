#pragma once
// Offline Wizard's Vault objective assistance. All times are UTC minutes.
// Scheduled starts are not a guarantee that a particular map instance is active.
// Source data: GW2 Wiki objective lists, Easy objectives and chat-link tables;
// event schedules cross-checked against GuildJen and GW2 Market, 2026-11-24.
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <vector>
#include <utility>
#include <map>

namespace AlterEgo { namespace VaultAssist {
inline const std::vector<int>* WikiStarts(const std::string& label) {
    static const std::map<std::string,std::vector<int>> starts = {
#include "WikiEventTimers.inc"
    };
    const auto it=starts.find(label);
    return it==starts.end()?nullptr:&it->second;
}
struct Destination {
    std::string label, map, link, note;
    int offset = -1, period = 0;
    std::vector<int> starts;
    Destination(std::string l, std::string m, std::string wp, std::string n,
                int o = -1, int p = 0, std::vector<int> s = {})
        : label(std::move(l)), map(std::move(m)), link(std::move(wp)),
          note(std::move(n)), offset(o), period(p), starts(std::move(s)) {
        if (const auto* verified=WikiStarts(label)) { starts=*verified; offset=-1; period=0; }
    }
};
using Options = std::vector<Destination>;
inline std::string Normalize(const std::string& title) {
    std::string out;
    for (unsigned char c : title) {
        if (std::isalnum(c)) out += static_cast<char>(std::tolower(c));
        else if (!out.empty() && out.back() != ' ') out += ' ';
    }
    if (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}
inline bool Has(const std::string& s, const std::string& term) {
    return (" " + s + " ").find(" " + term + " ") != std::string::npos;
}
inline int NextSeconds(const Destination& d, int64_t utc) {
    const auto until = [utc](int cycle, int offset) {
        const int64_t c = static_cast<int64_t>(cycle) * 60;
        const int64_t o = static_cast<int64_t>(offset) * 60;
        return static_cast<int>(((o - utc) % c + c) % c);
    };
    if (!d.starts.empty()) {
        int next = 86400;
        for (int start : d.starts) next = std::min(next,until(1440,start));
        return next;
    }
    if (d.period > 0 && d.offset >= 0) return until(d.period,d.offset);
    return -1;
}
inline std::string Countdown(int seconds) {
    if (seconds < 0) return "Variable start";
    if (!seconds) return "Scheduled now";
    const int m = (seconds + 59) / 60;
    if (m < 60) return "in " + std::to_string(m) + "m";
    return "in " + std::to_string(m/60) + "h " + std::to_string(m%60) + "m";
}
inline void Sort(Options& options, int64_t utc) {
    std::stable_sort(options.begin(),options.end(),[utc](const Destination& a,const Destination& b) {
        int x = NextSeconds(a,utc), y = NextSeconds(b,utc);
        return (x < 0 ? 86401 : x) < (y < 0 ? 86401 : y);
    });
}
inline Options Training() {
    return {{"Training Mech","Seitung Province","[&BL8MAAA=]","EoD: travel to Monastery Waypoint, then repeat the combat hero challenge north of Painter's Platform. Check objective progress after each win."},
            {"Painter's Platform (POI)","Seitung Province","[&BP8MAAA=]","The Veteran Jade Bulwark hero challenge is north of this point of interest. Map marker, not a teleport."},
            {"Tsuko Talonedge","Arborstone","[&BGMNAAA=]","EoD: repeat the Kestrel master combat hero challenge."},
            {"Silverwastes events","The Silverwastes","[&BH8HAAA=]","Core alternative: join fort capture and defense events near Camp Resolve."}};
}
inline Options Gather(bool plants, bool wood, bool ore, bool fish) {
    Options out;
    if (plants) out.emplace_back("Beetletun farm","Queensdale","[&BPoAAAA=]","Lettuce nodes south of Beetletun Waypoint; gather nearby plants as well.");
    if (wood) out.emplace_back("Cypress cluster","Malchor's Leap","[&BKYCAAA=]","Five cypress saplings by the Melandru statue southwest of Pagga's Waypoint.");
    if (ore) out.emplace_back("Rata Pten platinum","Mount Maelstrom","[&BMkCAAA=]","Criterion Waypoint; look for platinum and rich platinum veins around Rata Pten.");
    if (ore) out.emplace_back("Silverwastes ore","The Silverwastes","[&BH8HAAA=]","Follow the southern path west; look for ore near the Vinewrath area. Node spawns vary.");
    if (fish) out.emplace_back("Arborstone fishing","Arborstone","[&BGMNAAA=]","EoD fishing: Deep Fishing Hole at Danika's Respite.");
    out.emplace_back("Home / guild gathering","","","Use unlocked home instance or homestead nodes and upgraded guild hall synthesizers. Node availability depends on upgrades.");
    return out;
}
inline Options Meta(const std::string& t) {
    if (Has(t,"cantha")) return {
        {"Aetherblade Assault","Seitung Province","[&BGUNAAA=]","Daigo Ward Waypoint; scheduled event chain."},
        {"Kaineng Blackout","New Kaineng City","[&BBkNAAA=]","Ministry Ward Waypoint; scheduled event chain."},
        {"Gang War of Echovald","Echovald Wilds","[&BMwMAAA=]","Kropa Waypoint; scheduled event chain."},
        {"Battle for the Jade Sea","Dragon's End","[&BPEMAAA=]","Frozen Sea Waypoint; battle phase scheduled."},
        {"Destroy the Ravenous Wanderer","Gyala Delve","","Map-dependent chain; no fixed start assumed."}};
    if (Has(t,"heart of maguuma")) return {
        {"Battle in Tarir","Auric Basin","[&BAIIAAA=]","Octovine phase; arrive earlier for preparations."},
        {"King of the Jungle","Tangled Depths","[&BPUHAAA=]","Ley-Line Confluence Waypoint; Gerent phase."},
        {"Dragon's Stand","Dragon's Stand","[&BBAIAAA=]","Scheduled map push; arrive early."}};
    if (Has(t,"crystal desert")) return {
        {"Path to Ascension","Elon Riverlands","[&BFMKAAA=]","Scheduled map meta phase."},
        {"Maws of Torment","The Desolation","[&BKMKAAA=]","The Darklands Waypoint; scheduled meta phase."},
        {"Forged with Fire","Domain of Vabbi","","Check the map event chain."}};
    if (Has(t,"horn of maguuma")) return {
        {"Wizard's Tower","Skywatch Archipelago","[&BOANAAA=]","Unlocking the Wizard's Tower meta; Rata Novus Promenade is a travel starting point."},
        {"Defense of Amnytas","Amnytas","","Scheduled meta phase."},
        {"The Spider's Lair","Inner Nayos","","Map-dependent chain."}};
    if (Has(t,"janthir")) return {
        {"Of Mists and Monsters","Janthir Syntri","[&BCoPAAA=]","Stricken Plains Waypoint; travel starting point."},
        {"A Titanic Voyage","Bava Nisos","[&BGEPAAA=]","Scheduled event chain."}};
    if (Has(t,"castora")) return {
        {"Hammerhart Rumble!","Shipwreck Strand","","Scheduled map meta phase."},
        {"Secrets of the Weald","Starlit Weald","","Scheduled map meta phase."},
        {"Shackles of the Ancients","Eternity's Garden","","Scheduled event chain."},
        {"Depths of Cruelty","Leyspring Hollows","","Check the map event chain."}};
    return {};
}
inline Options Match(const std::string& title,const std::string& track) {
    const std::string t = Normalize(title), mode = Normalize(track);
    if (!mode.empty() && mode != "pve") return {};
    // Exact/qualified English titles, not inferred rotation or account expansion.
    if (Has(t,"defeat") && Has(t,"world boss")) {
        if (Has(t,"inquest golem mark ii")) return {{"Inquest Golem Mark II","Mount Maelstrom","[&BNQCAAA=]","Old Sledge Site Waypoint."}};
        if (Has(t,"claw of jormag")) return {{"Claw of Jormag","Frostgorge Sound","[&BH0CAAA=]","Earthshake Waypoint."}};
        if (Has(t,"megadestroyer")) return {{"Megadestroyer","Mount Maelstrom","[&BM0CAAA=]","Maelstrom's Waypoint."}};
        if (Has(t,"shatterer")) return {{"The Shatterer","Blazeridge Steppes","[&BE4DAAA=]","Lowland Burns Waypoint."}};
        if (Has(t,"great jungle wurm")) return {{"Great Jungle Wurm","Caledon Forest","[&BEEFAAA=]","Scheduled pre-event."}};
        if (Has(t,"shadow behemoth")) return {{"Shadow Behemoth","Queensdale","[&BPcAAAA=]","Swamplost Haven Waypoint."}};
        if (Has(t,"svanir shaman")) return {{"Svanir Shaman Chief","Wayfarer Foothills","[&BH4BAAA=]","Vendrake's Homestead Waypoint."}};
        if (Has(t,"fire elemental")) return {{"Fire Elemental","Metrica Province","[&BEgAAAA=]","Desider Atum Waypoint; travel toward Thaumanova Reactor."}};
    }
    if (Has(t,"defeat") && Has(t,"tequatl the sunless"))
        return {{"Tequatl the Sunless","Sparkfly Fen","[&BNABAAA=]","Splintered Coast Waypoint; arrive early."}};
    if (Has(t,"meta event") && Has(t,"events in")) {
        auto m = Meta(t);
        if (!m.empty()) return m;
    }
    if (Has(t,"meta event")) {
        for (const auto& region : {"cantha","heart of maguuma","crystal desert","horn of maguuma","janthir","castora"})
            for (const auto& event : Meta(region))
                if (Has(t,Normalize(event.label))) return {event};
    }
    if (t == "complete 3 events" || t == "complete 10 events") return Training();
    if (Has(t,"complete") && (Has(t,"events in cantha") || Has(t,"event in cantha")) && !Has(t,"meta event")) {
        auto options = Training(); options.pop_back();
        options.emplace_back("Jade guard mechs","New Kaineng City","[&BBYNAAA=]",
            "EoD: Garden Heights Waypoint; start sparring at a Training Mech Console. Requires a jade bot.");
        return options;
    }
    if (Has(t,"complete") && (Has(t,"event in the horn of maguuma") || Has(t,"events in the horn of maguuma")))
        return {{"Skywatch rifts","Skywatch Archipelago","[&BOANAAA=]","SotO: use Heart of the Obscure to locate a Tier 1 rift."}};
    if (Has(t,"complete") && (Has(t,"event in the crystal desert") || Has(t,"events in the crystal desert")))
        return {{"Elon Riverlands events","Elon Riverlands","[&BFMKAAA=]","PoF: look for events near the local meta chain."}};
    if (Has(t,"complete") && Has(t,"group events")) return {{"Silverwastes forts","The Silverwastes","[&BH8HAAA=]","Capture or defend RIBA forts. Only group events qualify."}};
    if (Has(t,"defiance bar")) return {{"Training Mech","Seitung Province","[&BL8MAAA=]","EoD: break the combat hero challenge's defiance bar."},
        {"Painter's Platform (POI)","Seitung Province","[&BP8MAAA=]","Hero challenge just north of this POI."}};
    if (Has(t,"dodge roll")) return {{"Training Grounds","Seitung Province","[&BL8MAAA=]","EoD: engage a trainer, then dodge in combat."},
        {"Combat Arena (POI)","Seitung Province","[&BJoMAAA=]","Training area marker."}};
    if (Has(t,"combo skills")) return {{"Training Grounds","Seitung Province","[&BL8MAAA=]","EoD: use combo finishers in a field near Trainer Raji."},
        {"Combat Arena (POI)","Seitung Province","[&BJoMAAA=]","Training area marker."}};
    if (Has(t,"harvesting sickle")) return Gather(true,false,false,false);
    if (Has(t,"logging axe")) return Gather(false,true,false,false);
    if (Has(t,"mining pick")) return Gather(false,false,true,false);
    if (Has(t,"gather") && Has(t,"crafting resources")) return Gather(true,true,true,false);
    if (Has(t,"catch") && Has(t,"fish")) return Gather(false,false,false,true);
    if (Has(t,"jumping puzzle")) {
        if (Has(t,"antre of adjournment")) return {{"Valley of Lyss Waypoint","Malchor's Leap","[&BKoCAAA=]","Enter the underwater passage north of the bridge to the east."},
            {"Pagga's Waypoint","Malchor's Leap","[&BKYCAAA=]","Alternative approach if the closest waypoint is contested."}};
        if (Has(t,"loreclaw expanse")) return {{"Loreclaw Waypoint","Plains of Ashford","[&BMcDAAA=]","Puzzle entrance nearby."}};
        if (Has(t,"retrospective runaround")) return {{"Camp Resolve Waypoint","The Silverwastes","[&BH8HAAA=]","Travel to the Skritt tunnels; follow the jumping puzzle route."}};
    }
    if (Has(t,"vasburg armory")) return {{"Jade Gate Waypoint","Echovald Wilds","[&BPkMAAA=]","Complete the Vasburg Armory mini-dungeon."}};
    if (Has(t,"valdhertz crypts")) return {{"Valdhertz Crypts (POI)","Echovald Wilds","[&BFkNAAA=]","Mini-dungeon entrance marker."}};
    if (Has(t,"rift hunt")) return {{"Weekly Rift Hunting","","","Use the current Weekly Rift Hunting achievement to choose an active map and use the Heart of the Obscure."}};
    if (Has(t,"defeat") && Has(t,"enemies in cantha"))
        return {{"Spirit Vestibule (POI)","Seitung Province","[&BI4MAAA=]","Nonaggressive wildlife to the east; EoD required."}};
    if (Has(t,"defeat") && Has(t,"enemies in the crystal desert"))
        return {{"Cragged Vale Waypoint","Domain of Vabbi","[&BKUKAAA=]","Check for Forged packs in the Vehjin Mines during local events."}};
    if (t == "complete a renown heart") return {{"Training Grounds","Seitung Province","[&BL8MAAA=]","EoD: Monastery Training heart; complete training tasks."}};
    return {};
}
} }
