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
    def test_tables(self):
        raw="<table>"+"".join("<tr><td>%d</td><td>A</td><td>B</td><td>C</td></tr>"%i for i in range(1,16))+"</table>"
        self.assertEqual(len(w.parse_daily(raw)),15)
        with self.assertRaises(ValueError):
            w.parse_daily(raw.replace("<td>15</td>","<td>16</td>"))
    def test_invalid_snapshot(self):
        data=copy.deepcopy(w.load(w.SEED)["datasets"]["fractals"]["data"])
        data["sets"][0]["recommended"][0][0]=100
        with self.assertRaises(ValueError):
            w.validate("fractals",data)

if __name__=="__main__":
    unittest.main()
