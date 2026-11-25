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

def parse_raid(raw):
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
    for t in tables(raw):
        rows = numbered(t, 4)
        if [int(r[0]) for r in rows] == list(range(1,16)):
            return [[clean_name(x) for x in r[1:]] for r in rows]
    raise ValueError("Expected fifteen daily fractal rows")

def parse_recommended(raw):
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
    # Template source has switch branches; rendered documentation may also
    # contain the numbered mapping. Do not infer ranges or missing entries.
    candidates = {}
    for n, name in re.findall(r"(?:^|\n|\|)\s*(\d{1,3})\s*=\s*([^\n|<>{}]+)", raw):
        n = int(n)
        if 1 <= n <= 100:
            name = clean_name(name.strip())
            if n in candidates and candidates[n] != name:
                raise ValueError("Conflicting scale mapping")
            candidates[n] = name
    if set(candidates) != set(range(1,101)):
        raise ValueError("Expected complete scale mapping")
    return [candidates[i] for i in range(1,101)]

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
    else:
        raise ValueError("Unknown dataset")

def refresh(name, old):
    if name == "raid_bounties":
        result = fetch_page("Template:Daily Raid Bounties", parse_raid)
        # A changed order requires a new verified UTC anchor; never silently
        # combine a different rotation with the existing historical anchor.
        if result != old:
            raise ValueError("Raid rotation changed; verify new UTC anchor before promotion")
        return result
    daily = fetch_page("Template:Daily Fractal Schedule", parse_daily)
    rec = fetch_page("Template:Daily Recommended Fractal Schedule", parse_recommended)
    scales = fetch_page("Template:Fractal info", parse_scales, "wikitext")
    return {"sets":[{"daily":d,"recommended":r} for d,r in zip(daily,rec)],"scales":scales}

def load(path):
    obj = json.loads(path.read_text(encoding="utf-8"))
    if obj["schema"] != 1 or set(obj["datasets"]) != {"raid_bounties","fractals"}:
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

def probe():
    # One bounded sample per unverified source; no full crawl or promotion.
    probes = [
        ("event_timers", lambda: json.loads(get("/wiki/Widget:Event_timer/data.json?action=raw"))),
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
    if a.probe:
        probe()
    if a.generate:
        generate(obj,a.generate)

if __name__=="__main__":
    main()
