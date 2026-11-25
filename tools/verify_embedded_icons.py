#!/usr/bin/env python3
"""Verify all named fixed icon resources inside the actual linked PE DLL."""
import struct
import sys
from pathlib import Path
from generate_embedded_icons import read_icons


def resources(blob):
    u16 = lambda o: struct.unpack_from("<H", blob, o)[0]
    u32 = lambda o: struct.unpack_from("<I", blob, o)[0]
    pe = u32(0x3c)
    if blob[pe:pe+4] != b"PE\0\0" or u16(pe+4) != 0x8664:
        raise ValueError("Expected x64 PE")
    opt = pe+24
    if u16(opt) != 0x20b:
        raise ValueError("Expected PE32+")
    sec = opt+u16(pe+20)
    sections = []
    for i in range(u16(pe+6)):
        q = sec+40*i
        sections.append((u32(q+12), max(u32(q+8), u32(q+16)), u32(q+20)))
    def offset(rva):
        for va, size, raw in sections:
            if va <= rva < va+size:
                return raw+rva-va
        raise ValueError(f"Invalid RVA {rva}")
    base = offset(u32(opt+112+16))
    def entries(rel):
        p = base+rel
        for i in range(u16(p+12)+u16(p+14)):
            name, child = struct.unpack_from("<II", blob, p+16+8*i)
            if name >> 31:
                q = base+(name & 0x7fffffff)
                name = blob[q+2:q+2+2*u16(q)].decode("utf-16le")
            yield name, child & 0x7fffffff
    images = {}
    for kind, directory in entries(0):
        if kind != 10:
            continue
        for name, languages in entries(directory):
            if not isinstance(name, str) or not name.startswith("AE_"):
                continue
            lang_entries = list(entries(languages))
            if len(lang_entries) != 1 or name in images:
                raise ValueError(f"Unexpected resource variants: {name}")
            _, data = lang_entries[0]
            p = base+data
            start = offset(u32(p))
            images[name] = blob[start:start+u32(p+4)]
    return images


def main(path):
    expected = {f"AE_{i}": content for i, (_, content) in read_icons().items()}
    actual = resources(Path(path).read_bytes())
    if actual.keys() != expected.keys():
        raise ValueError(f"Missing: {expected.keys()-actual.keys()}; extra: {actual.keys()-expected.keys()}")
    for name in expected:
        if actual[name] != expected[name]:
            raise ValueError(f"Embedded resource differs from Git asset: {name}")
    print(f"Verified all {len(expected)} fixed icon resources byte-for-byte in the linked DLL")


if __name__ == "__main__":
    main(sys.argv[1])
