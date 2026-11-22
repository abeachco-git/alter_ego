#!/usr/bin/env python3
"""Generate Windows RCDATA resources for fixed GW2 UI icons at build time.

Dynamic item/skin/skill textures and large backgrounds remain runtime disk-cached.
"""
import json
from pathlib import Path
import time
import urllib.request
import sys

BASE = "https://api.guildwars2.com"
PROFESSIONS = {
    "Guardian": 1, "Warrior": 2, "Engineer": 3, "Ranger": 4,
    "Thief": 5, "Elementalist": 6, "Mesmer": 7,
    "Necromancer": 8, "Revenant": 9,
}
CRAFTING = [
    "https://wiki.guildwars2.com/images/3/32/Armorsmith_tango_icon_20px.png",
    "https://wiki.guildwars2.com/images/b/b7/Artificer_tango_icon_20px.png",
    "https://wiki.guildwars2.com/images/8/8f/Chef_tango_icon_20px.png",
    "https://wiki.guildwars2.com/images/f/f3/Huntsman_tango_icon_20px.png",
    "https://wiki.guildwars2.com/images/f/f2/Jeweler_tango_icon_20px.png",
    "https://wiki.guildwars2.com/images/e/e5/Leatherworker_tango_icon_20px.png",
    "https://wiki.guildwars2.com/images/0/0b/Scribe_tango_icon_20px.png",
    "https://wiki.guildwars2.com/images/4/4d/Tailor_tango_icon_20px.png",
    "https://wiki.guildwars2.com/images/4/46/Weaponsmith_tango_icon_20px.png",
]
STATUS = {
    43766: "https://render.guildwars2.com/file/1932B731E2F70F2F1E3D453A4B7C26B24CF647C0/603246.png",
    98501: "https://render.guildwars2.com/file/5C759EC1C95F3BE53C167A7D9F0D27BF3AE56277/625611.png",
    68326: "https://render.guildwars2.com/file/01E7C8B0EB04F6E0CB25AA0403C96E0355B63A39/924581.png",
}

def download(url):
    last_error = None
    for attempt in range(4):
        try:
            req = urllib.request.Request(url, headers={
                "User-Agent": "AlterEgo-EmbeddedAssets/1.0",
                "Accept": "application/json,image/png,image/jpeg,*/*",
            })
            with urllib.request.urlopen(req, timeout=35) as response:
                result = response.read()
            if not result:
                raise ValueError("empty response")
            return result
        except Exception as exc:
            last_error = exc
            time.sleep((attempt + 1) * 1.5)
    raise RuntimeError(f"Could not fetch {url}: {last_error}")

def main(output):
    out = Path(output).resolve()
    folder = out.parent / "embedded_icon_data"
    folder.mkdir(parents=True, exist_ok=True)
    images = {}
    for prof, number in PROFESSIONS.items():
        data = json.loads(download(f"{BASE}/v2/professions/{prof}"))
        images[9000000 + number] = data["icon"]
        images[9200000 + number] = data["icon_big"]

    specializations = json.loads(download(f"{BASE}/v2/specializations?ids=all"))
    elites = 0
    for spec in specializations:
        if not spec.get("elite"):
            continue
        sid = int(spec["id"])
        if spec.get("icon"):
            images[1500000 + sid] = spec["icon"]
        if spec.get("profession_icon_big"):
            images[2000000 + sid] = spec["profession_icon_big"]
        elites += 1
    if elites < 27:
        raise ValueError(f"Expected >=27 elite specializations, found {elites}")

    for i, url in enumerate(CRAFTING):
        images[9100001 + i] = url
    images.update(STATUS)

    lines = ["// Generated at build time; images embedded into the DLL.", ""]
    for asset_id, url in sorted(images.items()):
        content = download(url)
        if content.startswith(b"\x89PNG\r\n\x1a\n"):
            extension = "png"
        elif content.startswith(b"\xff\xd8\xff"):
            extension = "jpg"
        else:
            raise ValueError(f"Not a PNG/JPEG image: {asset_id} {url}")
        path = folder / f"{asset_id}.{extension}"
        path.write_bytes(content)
        lines.append(f'AE_{asset_id} RCDATA "{path.as_posix()}"')
    out.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"Embedded {len(images)} icons including {elites} elite specializations")

if __name__ == "__main__":
    main(sys.argv[1])
