#!/usr/bin/env python3
"""Verify the linked PE version resource against the exact CI build identity."""
import os
import re
import struct
import sys
from pathlib import Path

from verify_embedded_icons import resources


def main(path):
    # Keep this independent of generated files: read the checked-in product version
    # and the Actions environment, and compare to the linked binary.
    cmake = (Path(__file__).resolve().parents[1] / "CMakeLists.txt").read_text()
    version = re.search(r"project\(AlterEgo VERSION (\d+)\.(\d+)\.(\d+)\)", cmake)
    if not version:
        raise ValueError("Missing product version")
    major, minor, patch = map(int, version.groups())
    run = int(os.environ["GITHUB_RUN_NUMBER"])
    attempt = int(os.environ["GITHUB_RUN_ATTEMPT"])
    run_id = os.environ["GITHUB_RUN_ID"]
    commit = os.environ["GITHUB_SHA"]
    file_version = f"{major}.{minor}.{patch}.{run}"
    product_version = f"{major}.{minor}.{patch}-dev"
    actual = resources(Path(path).read_bytes(), 16, lambda name: name == 1)
    if set(actual) != {1}:
        raise ValueError("Missing RT_VERSION resource")
    data = actual[1]
    length, value_length, value_type = struct.unpack_from("<HHH", data)
    key = "VS_VERSION_INFO\0".encode("utf-16le")
    if length > len(data) or value_length != 52 or value_type != 0 or data[6:6+len(key)] != key:
        raise ValueError("Malformed version root")
    offset = (6 + len(key) + 3) & ~3
    fixed = struct.unpack_from("<13I", data, offset)
    expected = (0xFEEF04BD, 0x10000, (major << 16) | minor, (patch << 16) | run,
                (major << 16) | minor, patch << 16)
    if fixed[:6] != expected or not (fixed[7] & 2) or fixed[9] != 2:
        raise ValueError(f"Incorrect fixed version fields: {fixed}")
    for value in (file_version, product_version, "AlterEgo.dll", "Alter Ego",
                  f"Development; build {run}; attempt {attempt}; run {run_id}; commit {commit}"):
        if (value + "\0").encode("utf-16le") not in data:
            raise ValueError(f"Missing version string: {value}")
    print(f"Verified linked Windows file version {file_version}, attempt {attempt}, commit {commit}")


if __name__ == "__main__":
    main(sys.argv[1])
