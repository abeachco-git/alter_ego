#!/usr/bin/env python3
"""Generate Windows fixed-icon resources exclusively from checked-in assets."""
import hashlib
import json
from pathlib import Path
import sys

ASSETS = Path(__file__).resolve().parents[1] / "assets" / "icons"


def read_icons(folder=ASSETS):
    manifest = json.loads((folder / "manifest.json").read_text(encoding="utf-8"))
    if manifest["schema_version"] != 1:
        raise ValueError("Unsupported icon manifest")
    images = {}
    for icon in manifest["icons"]:
        asset_id = icon["id"]
        name = icon["file"]
        if (type(asset_id) is not int or asset_id in images
                or name not in (f"{asset_id}.png", f"{asset_id}.jpg")):
            raise ValueError(f"Invalid or duplicate resource: {icon}")
        content = (folder / name).read_bytes()
        signature = b"\x89PNG\r\n\x1a\n" if name.endswith(".png") else b"\xff\xd8\xff"
        if (not content.startswith(signature) or len(content) != icon["size"]
                or hashlib.sha256(content).hexdigest() != icon["sha256"]):
            raise ValueError(f"Invalid or changed fixed icon: {name}")
        images[asset_id] = (name, content)
    expected = set(range(9000001, 9000010)) | set(range(9200001, 9200010))
    expected |= set(range(9100001, 9100010)) | {43766, 98501, 68326}
    small = {i-1500000 for i in images if 1500000 < i < 1600000}
    large = {i-2000000 for i in images if 2000000 < i < 2100000}
    if small != large or len(small) < 36:
        raise ValueError("Incomplete elite icon pairs")
    expected |= {1500000+i for i in small} | {2000000+i for i in large}
    if set(images) != expected:
        raise ValueError("Unexpected or missing fixed icon resource IDs")
    files = {p.name for p in folder.iterdir() if p.suffix.lower() in (".png", ".jpg")}
    if files != {name for name, _ in images.values()}:
        raise ValueError("Unlisted or missing image files")
    return images


def main(output):
    out = Path(output).resolve()
    folder = out.parent / "embedded_icon_data"
    folder.mkdir(parents=True, exist_ok=True)
    images = read_icons()
    lines = ["// Generated from checked-in fixed assets; no network access.", ""]
    for asset_id, (name, content) in sorted(images.items()):
        path = folder / name
        path.write_bytes(content)
        lines.append(f'AE_{asset_id} RCDATA "{path.as_posix()}"')
    out.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"Embedded {len(images)} verified local icons ({sum(len(v[1]) for v in images.values())} bytes)")


if __name__ == "__main__":
    main(sys.argv[1])
