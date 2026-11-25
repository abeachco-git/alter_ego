#!/usr/bin/env python3
"""Manual, bounded acquisition of companion artwork into a review directory.

Normal builds never invoke this tool. No repository files are modified.
Optionally stage immutable Git blobs (not commits/refs) for later review.
"""
import argparse
import base64
import hashlib
import io
import json
import os
from pathlib import Path
import time
import urllib.request
from PIL import Image

BASE = "https://api.guildwars2.com"
RENDER = "https://render.guildwars2.com/file/"
AGENT = "AlterEgo-CompanionArtwork/1.0"

def request(url, payload=None, token=None):
    headers = {"User-Agent": AGENT, "Accept": "application/json,image/png,image/jpeg,*/*"}
    if token:
        if not url.startswith("https://api.github.com/repos/"):
            raise ValueError("Invalid authenticated destination")
        headers["Authorization"] = "Bearer " + token
    if payload is not None:
        headers["Content-Type"] = "application/json"
    req = urllib.request.Request(url, data=None if payload is None else json.dumps(payload).encode(), headers=headers)
    with urllib.request.urlopen(req, timeout=25) as response:
        data = response.read(8 * 1024 * 1024 + 1)
    if not data or len(data) > 8 * 1024 * 1024:
        raise ValueError("Invalid response length")
    return data

def api(path):
    return json.loads(request(BASE + path))

def checked_rows(rows, expected, label):
    if not isinstance(rows, list) or not all(isinstance(r, dict) and "id" in r for r in rows):
        raise ValueError("Invalid " + label)
    actual = [r["id"] for r in rows]
    if len(set(actual)) != len(actual) or set(actual) != set(expected):
        raise ValueError("Incomplete or duplicate " + label)
    return rows

def main():
    p = argparse.ArgumentParser()
    p.add_argument("output", type=Path)
    p.add_argument("--stage-blobs", action="store_true")
    args = p.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    version = "&v=2019-12-19T00:00:00.000Z"
    legend_ids = api("/v2/legends?v=2019-12-19T00:00:00.000Z")
    legends = checked_rows(api("/v2/legends?ids=all" + version), legend_ids, "legends")
    swaps = sorted({int(x["swap"]) for x in legends if int(x.get("swap", 0)) > 0})
    if len(swaps) < 8:
        raise ValueError("Missing legend swap skills")
    skills = checked_rows(api("/v2/skills?ids=" + ",".join(map(str, swaps))), swaps, "legend skills")
    pet_ids = api("/v2/pets")
    pets = checked_rows(api("/v2/pets?ids=all"), pet_ids, "pets")
    if len(pets) < 50:
        raise ValueError("Unexpectedly small pet catalog")
    targets = [(5000000+int(x["id"]), x, "legend swap skill", 64) for x in skills]
    targets += [(9400000+int(x["id"]), x, "ranger pet", 64) for x in pets]
    rows = []
    by_url = {}
    for resource, x, kind, maxside in sorted(targets):
        url = x.get("icon", "")
        if not url.startswith(RENDER):
            raise ValueError("Missing/unexpected render URL: " + str(x["id"]))
        if url not in by_url:
            original = request(url)
            with Image.open(io.BytesIO(original)) as source:
                if source.format not in ("PNG", "JPEG") or not (0 < source.width <= 1024 and 0 < source.height <= 1024):
                    raise ValueError("Invalid source image")
                image = source.convert("RGBA")
                image.thumbnail((maxside, maxside), Image.Resampling.LANCZOS)
                out = io.BytesIO()
                image.save(out, "PNG", optimize=True)
                by_url[url] = (out.getvalue(), image.size, source.size, hashlib.sha256(original).hexdigest())
            time.sleep(0.15)
        content, size, original_size, original_hash = by_url[url]
        name = str(resource) + ".png"
        (args.output / name).write_bytes(content)
        row = {"id": resource, "file": name, "size": len(content), "sha256": hashlib.sha256(content).hexdigest(),
               "source": url, "field": kind, "name": x.get("name",""), "width": size[0], "height": size[1],
               "source_dimensions": list(original_size), "source_sha256": original_hash,
               "provenance": "Official ArenaNet API/render asset, staged for review; lossless PNG output, downsampled only if above 64px."}
        rows.append(row)
    if args.stage_blobs:
        repo = os.environ["GITHUB_REPOSITORY"]
        if repo != "abeachco-git/alter_ego":
            raise ValueError("Unexpected repository")
        token = os.environ["GITHUB_TOKEN"]
        for row in rows:
            content = (args.output / row["file"]).read_bytes()
            expected = hashlib.sha1(b"blob " + str(len(content)).encode() + b"\0" + content).hexdigest()
            result = json.loads(request("https://api.github.com/repos/" + repo + "/git/blobs",
                {"content": base64.b64encode(content).decode(), "encoding": "base64"}, token))
            if result.get("sha") != expected:
                raise ValueError("Unexpected staged blob SHA")
            row["git_blob"] = expected
    (args.output / "manifest.json").write_text(json.dumps({"icons": rows}, indent=2) + "\n", encoding="utf-8")
    print(f"Staged {len(skills)} legend swap icons and {len(pets)} pet icons ({sum(r['size'] for r in rows)} bytes). Review before updating asset manifest.")

if __name__ == "__main__":
    main()
