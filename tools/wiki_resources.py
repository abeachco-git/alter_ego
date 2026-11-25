#!/usr/bin/env python3
"""Bounded build-only Wiki data refresh; offline compilation uses validated snapshots."""
import argparse
import copy
import json
import re
import time
import tempfile
import urllib.parse
import urllib.request
from html.parser import HTMLParser
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SEED = ROOT / "assets/reference/wiki/rotations.json"
MANIFEST = ROOT / "assets/reference/wiki/manifest.json"
BASE = "https://wiki.guildwars2.com"
MAX_BYTES = 4 * 1024 * 1024
UA = "AlterEgo-Diagnostic/1.0"
DAY = 86400

def write(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", dir=path.parent, delete=False) as out:
        out.write(value)
        tmp = Path(out.name)
    tmp.replace(path)

def get(path):
    req = urllib.request.Request(BASE + path, headers={"User-Agent": UA})
    with urllib.request.urlopen(req, timeout=15) as response:
        data = response.read(MAX_BYTES + 1)
        if response.status != 200 or not data or len(data) > MAX_BYTES:
            raise ValueError("Unexpected HTTP payload")
    return data.decode("utf-8")

def api(**params):
    obj = json.loads(get("/api.php?" + urllib.parse.urlencode(dict(params, format="json"))))
    if "error" in obj:
        raise ValueError(str(obj["error"])[:300])
    return obj

class Tables(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.tables, self.stack = [], []
    def handle_starttag(self, tag, attrs):
        if tag == "table":
            self.stack.append([[], None, None])
        elif self.stack:
            t = self.stack[-1]
            if tag == "tr":
                t[1] = []
            elif tag in ("td", "th") and t[1] is not None:
                t[2] = []
    def handle_data(self, data):
        if self.stack and self.stack[-1][2] is not None:
            self.stack[-1][2].append(data)
    def handle_endtag(self, tag):
        if not self.stack:
            return
        t = self.stack[-1]
        if tag in ("td", "th") and t[2] is not None:
            t[1].append(" ".join("".join(t[2]).split()))
            t[2] = None
        elif tag == "tr" and t[1] is not None:
            t[0].append(t[1])
            t[1] = None
        elif tag == "table":
            self.tables.append(t[0])
            self.stack.pop()

def tables(html):
    p = Tables()
    p.feed(html)
    return p.tables

def fetch_page(page, parser, prop="text"):
    try:
        obj = api(action="parse", page=page, prop=prop)
        raw = obj["parse"][prop]["*"]
        result = parser(raw)
        print(page + ": API payload validated", flush=True)
        return result
    except Exception as exc:
        print(page + ": API failed validation (" + str(exc)[:180] + "); trying HTML", flush=True)
    raw = get("/wiki/" + urllib.parse.quote(page.replace(" ", "_"), safe=":/"))
    result = parser(raw)
    print(page + ": HTML fallback validated", flush=True)
    return result

def numbered(table, width):
    return [r for r in table if len(r) == width and r[0].isdigit()]

def clean_name(s):
    s = s.strip()
    if not s or len(s) > 100 or any(ord(ch) < 32 for ch in s):
        raise ValueError("Invalid name")
    return s

def array_rows(raw, numeric=False):
    match = re.search(r"\{\{#arraymap:\s*\n(.*?)\n\|\\n\|\$set\|", raw, re.S)
    if not match:
        raise ValueError("Missing schedule array")
    rows = [[x.strip() for x in line.split(",")] for line in match[1].strip().splitlines()]
    if len(rows) != 15 or any(len(row) != 3 for row in rows):
        raise ValueError("Expected 15 triples")
    return [[int(x) for x in row] if numeric else [clean_name(x) for x in row] for row in rows]

def parse_raid(raw):
    if "{{#switch: $set" in raw:
        cols = []
        for section in raw.split("{{#switch: $set")[1:]:
            body = section.split("}}",1)[0]
            rows = re.findall(r"^\s*\|\s*(\d+)\s*=\s*\[\[Daily Raid Bounties#Raid Bounty: ([^|\]]+)\|Raid Bounty:\]\]", body, re.M)
            if len(rows) not in (6,12) or [int(n) for n,_ in rows] != list(range(1,len(rows)+1)):
                raise ValueError("Incomplete raid switch")
            cols.append([clean_name(x) for _,x in rows]*(12//len(rows)))
        if len(cols)!=4:
            raise ValueError("Expected four raid switches")
        return cols

    cols = []
    for t in tables(raw):
        rows = numbered(t, 2)
        if len(rows) not in (6,12) or [int(r[0]) for r in rows] != list(range(1,len(rows)+1)):
            continue
        if not all(r[1].startswith("Raid Bounty: ") for r in rows):
            continue
        col = [clean_name(r[1].removeprefix("Raid Bounty: ")) for r in rows]
        cols.append(col * (12//len(col)))
    if len(cols) != 4:
        raise ValueError("Expected four complete raid bounty columns")
    return cols

def parse_daily(raw):
    if "{{#arraymap:" in raw:
        return array_rows(raw)
    for t in tables(raw):
        rows = numbered(t, 4)
        if [int(r[0]) for r in rows] == list(range(1,16)):
            return [[clean_name(x) for x in r[1:]] for r in rows]
    raise ValueError("Expected fifteen daily fractal rows")

def parse_recommended(raw):
    if "{{#arraymap:" in raw:
        return array_rows(raw, True)
    for t in tables(raw):
        rows = numbered(t, 4)
        if [int(r[0]) for r in rows] != list(range(1,16)):
            continue
        result = []
        for r in rows:
            row = []
            for x in r[1:]:
                m = re.fullmatch(r"(\d+)\s*[-–—]\s*(.+)", x)
                if not m:
                    raise ValueError("Malformed recommended fractal")
                row.append([int(m[1]), clean_name(m[2])])
            result.append(row)
        return result
    raise ValueError("Expected fifteen recommended rows")

def parse_scales(raw):
    # The template publishes both name->scales and scales->name. Validate
    # the complete bijection in both directions before using it.
    forward, reverse = {}, {}
    for name, nums in re.findall(r"^\s*\|\s*([A-Za-z][^=|\n]*?)\s*=\s*(\d+(?:\s*,\s*\d+){3})\s*$", raw, re.M):
        for n in map(int,nums.split(",")):
            if n in forward:
                raise ValueError("Duplicate scale")
            forward[n] = clean_name(name)
    for nums, name in re.findall(r"^\s*\|\s*(\d+(?:\s*\|\s*\d+){3})\s*=\s*([^|\n{}<>]+?)\s*$", raw, re.M):
        for n in map(int,nums.split("|")):
            if n in reverse:
                raise ValueError("Duplicate reverse scale")
            reverse[n] = clean_name(name)
    if set(forward) != set(range(1,101)) or forward != reverse:
        # HTML documentation fallback exposes 100 numbered list entries.
        # Accept only a complete unambiguous map, never a partial table.
        class Lists(HTMLParser):
            def __init__(self):
                super().__init__(convert_charrefs=True)
                self.active=None
                self.rows=[]
            def handle_starttag(self,tag,attrs):
                if tag in ("li","ul") and self.active is None:
                    self.active=[]
            def handle_data(self,data):
                if self.active is not None:
                    self.active.append(data)
            def handle_endtag(self,tag):
                if tag in ("li","ul") and self.active is not None:
                    self.rows.append(" ".join("".join(self.active).split()))
                    self.active=None
        p=Lists()
        p.feed(raw)
        forward={}
        for row in p.rows:
            m=re.fullmatch(r"(\d{1,3})\s*=\s*(.+)",row)
            if m and 1<=int(m[1])<=100:
                n=int(m[1])
                if n in forward and forward[n]!=m[2]:
                    raise ValueError("Conflicting HTML scale")
                forward[n]=clean_name(m[2])
        if set(forward)!=set(range(1,101)):
            raise ValueError("Expected complete scale mapping")
    return [forward[i] for i in range(1,101)]

EVENT_PHASES = [["core-wb","Svanir Shaman Chief","Svanir Shaman Chief"],["core-wb","Megadestroyer","Megadestroyer"],["core-wb","Fire Elemental","Fire Elemental"],["core-wb","The Shatterer","The Shatterer"],["core-wb","Great Jungle Wurm","Great Jungle Wurm"],["core-wb","Shadow Behemoth","Shadow Behemoth"],["core-wb","Claw of Jormag","Claw of Jormag"],["core-wb","Golem Mark II","Inquest Golem Mark II"],["core-hwb","Tequatl the Sunless","Tequatl the Sunless"],["hot-ab","Battle in Tarir (Octovine)","Battle in Tarir"],["hot-td","King of the Jungle (Chak Gerent)","King of the Jungle"],["hot-ds","Start advancing on the Blighting Towers","Dragon's Stand"],["pof-er","The Path to Ascension: Augury Rock","Path to Ascension"],["pof-td","Maws of Torment","Maws of Torment"],["pof-dv","Forged with Fire","Forged with Fire"],["eod-sp","Aetherblade Assault","Aetherblade Assault"],["eod-nkc","Kaineng Blackout","Kaineng Blackout"],["eod-ew","Gang War","Gang War of Echovald"],["eod-de","The Battle for the Jade Sea","Battle for the Jade Sea"],["soto-sa","Unlocking the Wizard's Tower","Wizard's Tower"],["soto-am","Defense of Amnytas","Defense of Amnytas"],["jw-js","Of Mists and Monsters","Of Mists and Monsters"],["jw-bn","A Titanic Voyage","A Titanic Voyage"],["voe-ss","Hammerhart Rumble!","Hammerhart Rumble!"],["voe-sw","Secrets of the Weald","Secrets of the Weald"],["voe-eg","Shackles of the Ancients","Shackles of the Ancients"],["voe-lh","Depths of Cruelty","Depths of Cruelty"]]

def parse_events(raw):
    obj=json.loads(raw)
    events=obj["events"]
    if not isinstance(events,dict) or len(events)<40:
        raise ValueError("Incomplete event timer")
    out={}
    for group,name,label in EVENT_PHASES:
        event=events[group]
        segments=event["segments"]
        matches=[str(key) for key,value in segments.items() if value.get("name")==name]
        if len(matches)!=1:
            raise ValueError("Missing or ambiguous phase: "+label)
        target=matches[0]
        seq=event["sequences"]
        partial,pattern=seq["partial"],seq["pattern"]
        for step in partial+pattern:
            if type(step["d"]) is not int or not 0<step["d"]<=1440 or str(step["r"]) not in segments:
                raise ValueError("Invalid event sequence")
        cycle=sum(step["d"] for step in pattern)
        starts=[]
        t=sum(step["d"] for step in partial) if cycle else 0
        if cycle:
            if 1440%cycle:
                raise ValueError("Non-daily event cycle")
            for step in pattern:
                if str(step["r"])==target:
                    starts.extend(range(t%cycle,1440,cycle))
                t+=step["d"]
        else:
            if sum(step["d"] for step in partial)<1440:
                raise ValueError("Incomplete daily sequence")
            for step in partial:
                if str(step["r"])==target and t<1440:
                    starts.append(t)
                t+=step["d"]
        out[label]=sorted(set(starts))
    validate("event_timers",out)
    return out

def fetch_events():
    page="Widget:Event timer/data.json"
    try:
        data=parse_events(api(action="parse",page=page,prop="wikitext")["parse"]["wikitext"]["*"])
        print(page+": API payload validated",flush=True)
        return data
    except Exception as exc:
        print(page+": API failed ("+str(exc)[:180]+"); trying raw JSON",flush=True)
    return parse_events(get("/wiki/Widget:Event_timer/data.json?action=raw"))


def validate(name, value):
    if name == "raid_bounties":
        if len(value) != 4 or any(len(col) != 12 for col in value):
            raise ValueError("Invalid raid rotation")
        for col in value:
            for x in col:
                clean_name(x)
        if len(set(value[0])) != 6 or len(set(value[3])) != 6 or len(set(value[1])) != 12 or len(set(value[2])) != 12:
            raise ValueError("Unexpected raid cycle")
    elif name == "fractals":
        sets, scales = value["sets"], value["scales"]
        if len(sets) != 15 or len(scales) != 100:
            raise ValueError("Invalid fractal rotation")
        for x in scales:
            clean_name(x)
        seen = set()
        for row in sets:
            daily, rec = row["daily"], row["recommended"]
            if len(daily) != 3 or len(set(daily)) != 3 or len(rec) != 3:
                raise ValueError("Invalid fractal set")
            key = tuple(sorted(daily))
            if key in seen or not set(daily) <= set(scales):
                raise ValueError("Ambiguous or unknown fractal")
            seen.add(key)
            for i, (n, title) in enumerate(rec):
                if type(n) is not int or not (i*25+1 <= n <= (i+1)*25) or scales[n-1] != title:
                    raise ValueError("Recommended scale/name mismatch")
    elif name == "event_timers":
        if set(value)!={phase[2] for phase in EVENT_PHASES}:
            raise ValueError("Incomplete event coverage")
        for label, starts in value.items():
            if not isinstance(starts,list) or not starts or starts!=sorted(set(starts)) or any(type(n) is not int or not 0<=n<1440 for n in starts):
                raise ValueError("Invalid daily starts: "+label)
    else:
        raise ValueError("Unknown dataset")

def refresh(name, old):
    if name == "event_timers":
        return fetch_events()
    if name == "raid_bounties":
        result = fetch_page("Template:Daily Raid Bounties", parse_raid, "wikitext")
        # A changed order requires a new verified UTC anchor; never silently
        # combine a different rotation with the existing historical anchor.
        if result != old:
            raise ValueError("Raid rotation changed; verify new UTC anchor before promotion")
        return result
    daily = fetch_page("Template:Daily Fractal Schedule", parse_daily, "wikitext")
    rec = fetch_page("Template:Daily Recommended Fractal Schedule", parse_recommended, "wikitext")
    scales = fetch_page("Template:Fractal info", parse_scales, "wikitext")
    rec = [[[n,scales[n-1]] if type(n) is int else n for n in row] for row in rec]
    return {"sets":[{"daily":d,"recommended":r} for d,r in zip(daily,rec)],"scales":scales}

def load(path):
    obj = json.loads(path.read_text(encoding="utf-8"))
    if obj["schema"] != 1 or set(obj["datasets"]) != {"raid_bounties","fractals","event_timers"}:
        raise ValueError("Unknown resource schema")
    for name, entry in obj["datasets"].items():
        validate(name,entry["data"])
        for key in ("last_attempt","last_success"):
            if type(entry[key]) is not int or entry[key] < 0 or entry[key] > time.time()+300:
                raise ValueError("Invalid resource timestamp")
    return obj

def due(entry, now, days):
    return not entry["last_attempt"] or now-entry["last_attempt"] >= days*DAY

def generate(obj, out):
    q = lambda s: json.dumps(s,ensure_ascii=True)
    d = obj["datasets"]
    raid = d["raid_bounties"]["data"]
    fractals = d["fractals"]["data"]
    write(out/"WikiRaidBounties.inc", "\n".join("    {"+",".join(map(q,col))+"}," for col in raid)+"\n")
    lines = []
    for row in fractals["sets"]:
        rec = row["recommended"]
        lines.append("    {{{"+",".join(map(q,row["daily"]))+"}}, {{"+",".join(str(x[0]) for x in rec)+"}}, {{"+",".join(q(x[1]) for x in rec)+"}}},")
    write(out/"WikiFractalSets.inc","\n".join(lines)+"\n")
    write(out/"WikiFractalScales.inc","\n".join("    "+q(x)+"," for x in fractals["scales"])+"\n")
    write(out/"WikiEventTimers.inc","\n".join("    {"+q(label)+",{"+",".join(map(str,starts))+"}}," for label,starts in sorted(d["event_timers"]["data"].items()))+"\n")

def probe():
    # One bounded sample per unverified source; no full crawl or promotion.
    probes = [
        ("skin_acquisition", lambda: api(action="ask",query="[[Has skin::+]]|?Has skin|?Has game id|limit=2")),
        ("currency_acquisition", lambda: api(action="ask",query="[[Category:Currencies]]|?Has game id|limit=2")),
    ]
    for name, fn in probes:
        try:
            value=fn()
            if not isinstance(value,(dict,list)) or not value:
                raise ValueError("Empty probe")
            if name!="event_timers" and not value.get("query",{}).get("results"):
                raise ValueError("No semantic rows")
            print("PROBE",name,"VALID",json.dumps(value,ensure_ascii=True)[:1200],flush=True)
        except Exception as exc:
            print("PROBE",name,"FAILED",str(exc)[:300],flush=True)

def main():
    p=argparse.ArgumentParser()
    p.add_argument("--state",type=Path,default=SEED)
    p.add_argument("--generate",type=Path)
    p.add_argument("--refresh",action="store_true")
    p.add_argument("--force",action="store_true")
    p.add_argument("--probe",action="store_true")
    a=p.parse_args()
    seed=load(SEED)
    try:
        obj=load(a.state)
    except (OSError,ValueError,KeyError,TypeError):
        obj=copy.deepcopy(seed)
    # A newer checked-in seed supersedes older restored CI state.
    for name, entry in seed["datasets"].items():
        if entry["last_success"] > obj["datasets"][name]["last_success"]:
            obj["datasets"][name] = copy.deepcopy(entry)
    if a.refresh:
        days=json.loads(MANIFEST.read_text())["build_interval_days"]
        now=int(time.time())
        for name,entry in obj["datasets"].items():
            if not a.force and not due(entry,now,days):
                print(name+": fresh attempt; no network",flush=True)
                continue
            entry["last_attempt"]=now
            try:
                result=refresh(name,entry["data"])
                validate(name,result)
                entry.update(data=result,last_success=now,source="GW2 Wiki (see manifest)")
                print(name+": LIVE VALIDATED",flush=True)
            except Exception as exc:
                print(name+": RETAINED LAST-KNOWN-GOOD: "+str(exc)[:300],flush=True)
            write(a.state,json.dumps(obj,indent=2,ensure_ascii=True)+"\n")
    if a.refresh:
        write(a.state,json.dumps(obj,indent=2,ensure_ascii=True)+"\n")
    if a.probe:
        probe()
    if a.generate:
        generate(obj,a.generate)

if __name__=="__main__":
    main()
