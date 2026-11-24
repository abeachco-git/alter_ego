#!/usr/bin/env python3
"""Disposable, reproducible SQL workload over the real Alter Ego schemas.

Uses Python's SQLite runtime, not the DLL's vendored library or C++ codecs.
Never opens a user's existing database. Timings are environment-specific.
"""
import argparse
import json
import platform
import re
import sqlite3
import statistics
import tempfile
import threading
import time
from contextlib import closing
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def schema(path):
    source = (ROOT / path).read_text(encoding="utf-8")
    version = int(re.search(r"kSchemaVersion\s*=\s*(\d+)", source)[1])
    blocks = re.findall(r'R"SQL\((.*?)\)SQL"', source, re.S)
    # Durable library schema v3 is fresh-only: intermediate experimental
    # schemas are deliberately not migration sources. Cache migrations remain
    # ordered one block per version, and unexpected layouts still fail closed.
    if path == "src/NativeLibrarySchema.h" and version == 3 and len(blocks) == 1:
        return version, blocks
    if len(blocks) != version:
        raise RuntimeError("Schema migration layout changed; update benchmark")
    return version, list(reversed(blocks))


def connect(path, synchronous):
    db = sqlite3.connect(path, timeout=1.5, isolation_level=None)
    db.execute("PRAGMA foreign_keys=ON")
    db.execute("PRAGMA trusted_schema=OFF")
    db.execute("PRAGMA synchronous=" + synchronous)
    return db


def initialize(path, mode, sync, source, application_id):
    version, migrations = schema(source)
    with closing(connect(path, sync)) as db:
        assert db.execute("PRAGMA journal_mode=" + mode).fetchone()[0] == mode.lower()
        for sql in migrations:
            db.executescript("BEGIN IMMEDIATE;\n" + sql + "\nCOMMIT;")
        db.execute(f"PRAGMA application_id={application_id}")
        db.execute(f"PRAGMA user_version={version}")


def seed(cache, library, mode, sync, scale):
    initialize(cache, mode, sync, "src/CacheDatabase.h", 0x41454348)
    initialize(library, mode, sync, "src/NativeLibrarySchema.h", 0x41454C42)
    with closing(connect(cache, sync)) as db:
        db.execute("BEGIN IMMEDIATE")
        for account in ("Fixture.1001", "Fixture.1002"):
            db.execute("INSERT INTO achievement_snapshots VALUES(?,1)", (account,))
            db.executemany("INSERT INTO achievement_progress VALUES(?,?,?,?,?,?,?)",
                           ((account, i, i % 10, 10, 0, 0, 1) for i in range(1, 8000*scale+1)))
            db.executemany("INSERT INTO achievement_completed_bits VALUES(?,?,?)",
                           ((account, i, b) for i in range(1, 8000*scale+1, 4) for b in range(3)))
            for n in range(20*scale):
                row = db.execute("INSERT INTO character_rows(account_name,name,profession,level,data_refreshed) "
                                 "VALUES(?,?,?,80,1)", (account, f"Hero {n}", "Necromancer")).lastrowid
                db.executemany("INSERT INTO character_equipment(character_id,ordinal,item_id,slot,tab) "
                               "VALUES(?,?,?,?,?)", ((row, j, 10000+j, f"Slot{j%20}", j//20+1)
                                                     for j in range(40)))
        db.execute("INSERT INTO character_snapshot VALUES(1,1)")
        db.execute("COMMIT")
    with closing(connect(library, sync)) as db:
        db.execute("BEGIN IMMEDIATE")
        # Shared membership fixtures model linked and independent Loadouts.
        gear_kinds = ("weapons", "armor", "trinkets", "runes", "sigils", "infusions", "relic")
        db.execute("INSERT INTO components VALUES('skills','skills-0','Skills',0,'skills-0')")
        db.execute("INSERT INTO component_payloads(kind,component_id,flags,extension_json) VALUES('skills','skills-0',0,'{}')")
        for kind in gear_kinds:
            db.execute("INSERT INTO components VALUES(?,?,?,0,?)", (kind,kind+"-0",kind,kind+"-0"))
            db.execute("INSERT INTO component_payloads(kind,component_id,flags,extension_json) VALUES(?,?,0,'{}')", (kind,kind+"-0"))
        db.execute("INSERT INTO equipment_sets VALUES('set-0','Fixture set',0)")
        db.executemany("INSERT INTO equipment_set_components VALUES('set-0',?,?)", ((kind,kind+"-0") for kind in gear_kinds))
        for n in range(250*scale):
            ident = f"component-{n}"
            loadout = f"loadout-{n}"
            db.execute("INSERT INTO components VALUES('base_build',?,?,?,?)", (ident, ident, n, ident))
            db.execute("INSERT INTO component_payloads(kind,component_id,flags,profession,extension_json) "
                       "VALUES('base_build',?,0,'Necromancer','{}')", (ident,))
            linked = n % 2 == 0
            db.execute("INSERT INTO loadouts(id,ordinal,modes_present,name,extension_json,equipment_set_id) VALUES(?,?,0,?,'{}',?)",
                       (loadout,n,loadout,"set-0" if linked else None))
            db.execute("INSERT INTO loadout_components VALUES(?,'base_build',?)", (loadout,ident))
            db.execute("INSERT INTO loadout_components VALUES(?,'skills','skills-0')", (loadout,))
            if not linked:
                db.executemany("INSERT INTO loadout_components VALUES(?,?,?)", ((loadout,kind,kind+"-0") for kind in gear_kinds))
        db.execute("COMMIT")


def elapsed(action):
    start = time.perf_counter_ns()
    action()
    return (time.perf_counter_ns() - start) / 1e6


def stats(samples):
    ordered = sorted(samples)
    return {"median_ms": round(statistics.median(ordered), 3),
            "p95_ms": round(ordered[(95*len(ordered)+99)//100-1], 3),
            "samples": len(ordered)}


def sizes(path):
    def size(suffix):
        p = Path(str(path)+suffix)
        return p.stat().st_size if p.exists() else 0
    return {"db": size(""), "wal": size("-wal"), "shm": size("-shm"), "journal": size("-journal")}


def exercise(directory, mode, sync, scale, repeats):
    cache, library = directory/"cache.db", directory/"library.db"
    directory.mkdir()
    seed(cache, library, mode, sync, scale)

    def startup():
        with closing(connect(cache, sync)) as c, closing(connect(library, sync)) as l:
            # Bulk projection cost includes SQLite-to-Python conversion.
            c.execute("SELECT * FROM character_rows ORDER BY character_id").fetchall()
            c.execute("SELECT * FROM character_equipment ORDER BY character_id,ordinal").fetchall()
            l.execute("SELECT * FROM components ORDER BY kind,ordinal").fetchall()
            l.execute("SELECT * FROM component_payloads").fetchall()
            l.execute("SELECT * FROM loadouts ORDER BY ordinal").fetchall()
            l.execute("SELECT * FROM loadout_components").fetchall()
            l.execute("SELECT * FROM equipment_sets ORDER BY ordinal").fetchall()
            l.execute("SELECT * FROM equipment_set_components").fetchall()

    def account_read(db):
        db.execute("BEGIN")
        rows = db.execute("SELECT * FROM achievement_progress WHERE account=? ORDER BY id",
                          ("Fixture.1001",)).fetchall()
        db.execute("SELECT * FROM achievement_completed_bits WHERE account=? ORDER BY achievement_id,bit",
                   ("Fixture.1001",)).fetchall()
        db.execute("COMMIT")
        assert len(rows) == 8000*scale

    def char_write(db, n):
        # Same scoped transaction shape as incremental character persistence:
        # retain parent identity, replace its child rows, advance freshness.
        db.execute("BEGIN IMMEDIATE")
        db.execute("UPDATE character_rows SET data_refreshed=? WHERE character_id=1", (n+2,))
        db.execute("DELETE FROM character_equipment WHERE character_id=1")
        db.executemany("INSERT INTO character_equipment(character_id,ordinal,item_id,slot,tab) VALUES(1,?,?,?,?)",
                       ((j,10000+j+n,f"Slot{j%20}",j//20+1) for j in range(40)))
        db.execute("COMMIT")

    def library_write(db, n):
        db.execute("BEGIN IMMEDIATE")
        db.execute("UPDATE loadouts SET notes=? WHERE id='loadout-0'", (f"edit {n}",))
        db.execute("UPDATE library_state SET revision=revision+1 WHERE singleton=1")
        db.execute("COMMIT")

    def reopen_write(n):
        with closing(connect(cache,sync)) as db:
            char_write(db,n)

    result = {"journal": mode, "synchronous": sync,
              "warm_startup": stats([elapsed(startup) for _ in range(repeats)]),
              "short_connection_character_write": stats([elapsed(lambda: reopen_write(n)) for n in range(repeats)])}
    with closing(connect(cache,sync)) as c, closing(connect(library,sync)) as l:
        result["account_read"] = stats([elapsed(lambda: account_read(c)) for _ in range(repeats)])
        result["persistent_character_write"] = stats([elapsed(lambda: char_write(c,n)) for n in range(repeats)])
        result["library_edit"] = stats([elapsed(lambda: library_write(l,n)) for n in range(repeats)])
        # A reader pins a snapshot for 40ms while a different connection writes.
        # This intentionally removes the current application-wide cache mutex.
        # It measures potential concurrency, not current addon thread behavior.
        contention = []
        for n in range(min(5,repeats)):
            c.execute("BEGIN")
            c.execute("SELECT * FROM character_rows WHERE character_id=1").fetchone()
            started = threading.Event()
            errors = []
            times = []

            def writer():
                try:
                    with closing(connect(cache,sync)) as writer_db:
                        started.set()
                        times.append(elapsed(lambda: char_write(writer_db,n)))
                except Exception as exc:
                    errors.append(str(exc))
                    started.set()

            worker = threading.Thread(target=writer)
            worker.start()
            started.wait()
            time.sleep(0.04)
            c.execute("COMMIT")
            worker.join()
            if errors:
                raise RuntimeError(errors)
            contention.extend(times)
        result["writer_with_reader_40ms"] = stats(contention)
        result["active_file_bytes"] = {"cache": sizes(cache), "library": sizes(library)}
        for db in (c,l):
            assert db.execute("PRAGMA integrity_check").fetchall() == [("ok",)]
            assert not db.execute("PRAGMA foreign_key_check").fetchall()
            if mode == "WAL":
                db.execute("PRAGMA wal_checkpoint(TRUNCATE)").fetchall()
    result["closed_file_bytes"] = {"cache": sizes(cache), "library": sizes(library)}
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--scale", type=int, default=1)
    p.add_argument("--repeats", type=int, default=15)
    p.add_argument("--output", type=Path)
    args = p.parse_args()
    if not 1 <= args.scale <= 20 or not 1 <= args.repeats <= 1000:
        p.error("scale must be 1–20 and repeats 1–1000")
    with tempfile.TemporaryDirectory(prefix="ae-sqlite-bench-") as temp:
        results = [exercise(Path(temp)/f"{mode}-{sync}",mode,sync,args.scale,args.repeats)
                   for mode,sync in (("DELETE","FULL"),("WAL","FULL"),("WAL","NORMAL"))]
    report = {"platform": platform.platform(), "python": platform.python_version(),
              "sqlite": sqlite3.sqlite_version, "scale": args.scale, "repeats": args.repeats,
              "fixture": {"accounts": 2, "characters": 40*args.scale, "equipment_rows": 1600*args.scale,
                          "achievement_rows": 16000*args.scale, "loadouts": 250*args.scale,
                          "linked_loadouts": 125*args.scale, "equipment_sets": 1},
              "scope": "Synthetic SQL/codec timings, warm OS cache; not a DLL startup or in-game measurement.",
              "results": results}
    output = json.dumps(report, indent=2)
    if args.output:
        args.output.write_text(output+"\n", encoding="utf-8")
    print(output)


if __name__ == "__main__":
    main()
