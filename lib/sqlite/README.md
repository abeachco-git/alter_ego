# SQLite dependency

SQLite 3.54.1 (2026-10-16), public domain. Upstream: https://sqlite.org/
Official amalgamation archive: https://www.sqlite.org/2026/sqlite-amalgamation-3540100.zip
SHA3-256: `92281c86e2957f9b9305890f9ac04204caa7b677cc868dc326d942689df28e30`.

The vendor tool installs the unmodified sqlite3.c, sqlite3.h and sqlite3ext.h.
Run `python3 tools/vendor_sqlite.py` from the repository, or pass
`--archive /path/to/official.zip` for offline verification. To update, review
upstream release notes, update the version/URL/digest in the script and this
notice, replace the three vendor files together, and run the required CTest
and Windows UCRT gates. Do not use shell.c or a runtime SQLite DLL.
