#!/usr/bin/env python3
"""Install the unmodified, checksum-verified official SQLite amalgamation."""
import argparse
import hashlib
import io
import pathlib
import urllib.request
import zipfile

VERSION = "3.54.1"
ARCHIVE = "sqlite-amalgamation-3540100"
URL = "https://www.sqlite.org/2026/" + ARCHIVE + ".zip"
SHA3 = "92281c86e2957f9b9305890f9ac04204caa7b677cc868dc326d942689df28e30"
FILES = ("sqlite3.c", "sqlite3.h", "sqlite3ext.h")

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=pathlib.Path, help="Use a previously downloaded official ZIP")
    parser.add_argument("--output", type=pathlib.Path, default=pathlib.Path(__file__).resolve().parents[1] / "lib" / "sqlite")
    args = parser.parse_args()
    if args.archive:
        raw = args.archive.read_bytes()
    else:
        with urllib.request.urlopen(URL, timeout=60) as response:
            raw = response.read(16 * 1024 * 1024 + 1)
    if hashlib.sha3_256(raw).hexdigest() != SHA3:
        raise SystemExit("SQLite archive SHA3-256 mismatch; refusing to install")
    with zipfile.ZipFile(io.BytesIO(raw)) as archive:
        files = {name: archive.read(f"{ARCHIVE}/{name}") for name in FILES}
    args.output.mkdir(parents=True, exist_ok=True)
    for name, data in files.items():
        path = args.output / name
        if path.exists() and path.read_bytes() != data:
            raise SystemExit(f"Refusing to overwrite different vendor file: {path}")
    for name, data in files.items():
        path = args.output / name
        if not path.exists():
            temp = path.with_suffix(path.suffix + ".tmp")
            temp.write_bytes(data)
            temp.replace(path)
    print(f"Verified official SQLite {VERSION}: {args.output}")

if __name__ == "__main__":
    main()
