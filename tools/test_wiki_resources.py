import copy
import tempfile
import unittest
from pathlib import Path
import wiki_resources as w

class Resources(unittest.TestCase):
    def test_seed_and_generation(self):
        obj=w.load(w.SEED)
        with tempfile.TemporaryDirectory() as d:
            w.generate(obj,Path(d))
            self.assertEqual(len((Path(d)/"WikiFractalScales.inc").read_text().splitlines()),100)
    def test_freshness(self):
        self.assertTrue(w.due({"last_attempt":0},1,30))
        self.assertFalse(w.due({"last_attempt":100},100+29*w.DAY,30))
        self.assertTrue(w.due({"last_attempt":100},100+30*w.DAY,30))
        self.assertFalse(w.due({"last_attempt":100},100+w.DAY-1,1))
        self.assertTrue(w.due({"last_attempt":100},100+w.DAY,1))
        import json
        self.assertEqual(json.loads(w.MANIFEST.read_text())["build_interval_days"],1)
    def test_tables(self):
        raw="<table>"+"".join("<tr><td>%d</td><td>A</td><td>B</td><td>C</td></tr>"%i for i in range(1,16))+"</table>"
        self.assertEqual(len(w.parse_daily(raw)),15)
        with self.assertRaises(ValueError):
            w.parse_daily(raw.replace("<td>15</td>","<td>16</td>"))
    def test_wikitext_schedules(self):
        data=w.load(w.SEED)["datasets"]["fractals"]["data"]
        rows="\n".join(", ".join(row["daily"]) for row in data["sets"])
        raw="{{#arraymap:\n"+rows+"\n|\\n|$set|"
        self.assertEqual(w.parse_daily(raw),[r["daily"] for r in data["sets"]])
        rec="\n".join(",".join(str(x[0]) for x in row["recommended"]) for row in data["sets"])
        self.assertEqual(w.parse_recommended("{{#arraymap:\n"+rec+"\n|\\n|$set|"),[[x[0] for x in r["recommended"]] for r in data["sets"]])
        groups={}
        for i,name in enumerate(data["scales"],1):
            groups.setdefault(name,[]).append(i)
        raw="\n".join("| "+name+" = "+", ".join(map(str,ids)) for name,ids in groups.items())
        raw+="\n"+"\n".join("| "+" | ".join(map(str,ids))+" = "+name for name,ids in groups.items())
        self.assertEqual(w.parse_scales(raw),data["scales"])
    def test_event_snapshot(self):
        events=w.load(w.SEED)["datasets"]["event_timers"]["data"]
        self.assertEqual(events["Shackles of the Ancients"][0],75)
        self.assertEqual(events["Forged with Fire"][:3],[0,60,120])
        self.assertEqual(len(events["Depths of Cruelty"]),8)
        broken=copy.deepcopy(events)
        del broken["Tequatl the Sunless"]
        with self.assertRaises(ValueError):
            w.validate("event_timers",broken)
    def test_invalid_snapshot(self):
        data=copy.deepcopy(w.load(w.SEED)["datasets"]["fractals"]["data"])
        data["sets"][0]["recommended"][0][0]=100
        with self.assertRaises(ValueError):
            w.validate("fractals",data)

if __name__=="__main__":
    unittest.main()
