"""Opt-in, bounded Snow Crows structure and native parser diagnostic.

No MetaBattle tests, source snapshots, guide text output, or runtime networking.
"""
import collections
import html.parser
import pathlib
import re
import subprocess
import sys
import tempfile
import urllib.request

PAGES = (
    "https://snowcrows.com/builds/raids/ranger/power-soulbeast-hammer-sword-axe",
    "https://snowcrows.com/builds/raids/revenant/power-conduit-staff-sword-sword",
)
LIMIT = 3 * 1024 * 1024


class Markup(html.parser.HTMLParser):
    def __init__(self):
        super().__init__()
        self.embeds = collections.Counter()
        self.companions = []

    def handle_starttag(self, tag, attrs):
        a = dict(attrs)
        kind = a.get("data-armory-embed", "")
        if kind and re.fullmatch(r"[a-zA-Z_-]{1,32}", kind):
            self.embeds[kind] += 1
            ids = a.get("data-armory-ids", "")
            if kind.lower() in {"pets", "pet", "legends", "legend"} and re.fullmatch(r"[0-9,]{1,128}", ids):
                self.companions.append((kind, ids))


def main(executable):
    failures = 0
    for url in PAGES:
        print("Probe:", url, flush=True)
        try:
            req = urllib.request.Request(url, headers={
                "User-Agent": "Mozilla/5.0 (compatible; AlterEgo/1.0)",
                "Accept": "text/html",
            })
            with urllib.request.urlopen(req, timeout=20) as response:
                if not response.geturl().startswith("https://snowcrows.com/builds/"):
                    raise ValueError("unexpected redirect")
                body = response.read(LIMIT + 1)
                if len(body) > LIMIT:
                    raise ValueError("provider payload exceeds parser budget")
                print("HTTP", response.status, "bytes", len(body), flush=True)
            markup = Markup()
            markup.feed(body.decode("utf-8", errors="replace"))
            print("Armory embed counts:", dict(markup.embeds), flush=True)
            print("Explicit companion embed IDs:", markup.companions, flush=True)
            # Report structural facts only, not retained guide prose or markup.
            html = body.decode("utf-8", errors="replace")
            start = html.find('id="skills"')
            if start >= 0:
                skill_section = html[start:]
                tags = re.findall(r'<[^>]+data-armory-embed="(?:skills|specializations)"[^>]*>', skill_section, re.I)
                for tag in tags[:16]:
                    kind = re.search(r'data-armory-embed="([^"]+)"', tag)
                    ids = re.search(r'data-armory-ids="([0-9,]*)"', tag)
                    print("Skill section embed", kind.group(1) if kind else "?", ids.group(1) if ids else "absent",
                          "attribute order", re.findall(r'data-armory-[a-z0-9-]+(?==)', tag, re.I), flush=True)
            gear = html.find('id="gearset"')
            if gear >= 0 and start > gear:
                hints = re.findall(r'<span\s+class="text-neutral-400 text-sm ml-2">([^<]+)</span>', html[gear:start])
                print("Equipment hints:", [hint for hint in hints if re.fullmatch(r"[A-Za-z0-9 -]{1,40}", hint)], flush=True)
            with tempfile.TemporaryDirectory() as tmp:
                page = pathlib.Path(tmp) / "guide.html"
                page.write_bytes(body)
                result = subprocess.run([executable, "snowcrows_import", "--probe", url, str(page)], timeout=30, check=False)
                failures += result.returncode != 0
        except Exception as exc:
            print("Probe unavailable:", type(exc).__name__, str(exc), flush=True)
            failures += 1
    return int(bool(failures))


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("Usage: probe_snowcrows.py <native ae_imports executable>")
    raise SystemExit(main(sys.argv[1]))
