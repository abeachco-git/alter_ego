#!/usr/bin/env python3
"""Validate vendored dependency fingerprints, generate CycloneDX and GitHub snapshots,
and optionally query OSV. Standard library only; never modifies vendored code."""
import argparse
import datetime
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import urllib.request

ROOT = Path(__file__).resolve().parents[1]


def git(*args):
    return subprocess.check_output(["git", *args], cwd=ROOT, text=True).strip()


def write(path, data):
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def validate(data):
    assert data["schema_version"] == 1, "Unsupported inventory schema"
    components = data["components"]
    ids = {c["id"] for c in components}
    assert len(ids) == len(components) and ids, "Duplicate or empty inventory"
    assert len({c["purl"] for c in components}) == len(components), "Duplicate PURL"
    for c in components:
        assert c["version"] and c["purl"].startswith("pkg:"), c["id"]
        assert c["policy"] in ("abi-locked", "manual"), c["id"]
        assert set(c["depends_on"]) <= ids and c["id"] not in c["depends_on"], c["id"]
        assert c["paths"], c["id"]
        if c["upstream_commit"]:
            assert re.fullmatch("[0-9a-f]{40}", c["upstream_commit"]), c["id"]
        for p in c["paths"]:
            path = p["path"]
            assert (ROOT / path).exists(), path
            # Fingerprint the committed blob/tree and reject unstaged or staged
            # tracked edits. This avoids claiming an SBOM for a dirty vendor tree.
            actual = git("rev-parse", "HEAD:" + path)
            if actual != p["git_object"] or git("status", "--porcelain", "--untracked-files=all", "--", path):
                raise ValueError(f"{c['id']}: {path} changed; review provenance and update dependencies.json explicitly")
    # ABI is intentionally fixed; changing the manifest is not an upgrade approval.
    by_id = {c["id"]: c for c in components}
    assert by_id["imgui"]["version"] == "1.80", "Nexus ImGui ABI must remain 1.80"
    assert by_id["nexus-api"]["version"] == "6", "Nexus API ABI must remain 6"
    return components


def timestamp():
    return datetime.datetime.now(datetime.timezone.utc).isoformat(timespec="seconds").replace("+00:00", "Z")


def sbom(components, sha):
    root = "alter-ego:" + sha
    items = []
    children = {d for c in components for d in c["depends_on"]}
    for c in components:
        licenses = ([{"license": {"name": "Public domain"}}] if c["license"] == "Public domain"
                    else [{"expression": c["license"]}])
        properties = [{"name": "alterego:policy", "value": c["policy"]},
                      {"name": "alterego:provenance", "value": c["notes"]}]
        properties += [{"name": "alterego:git-object:" + p["path"], "value": p["git_object"]}
                       for p in c["paths"]]
        if c["upstream_commit"]:
            properties.append({"name": "alterego:upstream-baseline", "value": c["upstream_commit"]})
        items.append({"type": c["type"], "bom-ref": c["id"], "name": c["name"],
                      "version": c["version"], "purl": c["purl"], "licenses": licenses,
                      "externalReferences": [{"type": "vcs", "url": c["source"]}],
                      "properties": properties})
    return {"$schema": "https://cyclonedx.org/schema/bom-1.6.schema.json",
            "bomFormat": "CycloneDX", "specVersion": "1.6", "version": 1,
            "metadata": {"timestamp": timestamp(),
                         "component": {"type": "application", "bom-ref": root,
                                       "name": "Alter Ego", "version": sha}},
            "components": items,
            "dependencies": [{"ref": root, "dependsOn": [c["id"] for c in components if c["id"] not in children]}] +
                            [{"ref": c["id"], "dependsOn": c["depends_on"]} for c in components]}


def snapshot(components, sha):
    children = {d for c in components for d in c["depends_on"]}
    resolved = {c["id"]: {"package_url": c["purl"],
                          "relationship": "indirect" if c["id"] in children else "direct",
                          "scope": "runtime", "dependencies": c["depends_on"]}
                for c in components}
    return {"version": 0, "sha": sha, "ref": os.environ["GITHUB_REF"],
            "job": {"correlator": "vendored-dependencies", "id": os.environ["GITHUB_RUN_ID"]},
            "detector": {"name": "vendored-dependency-inventory", "version": "1.0.0",
                         "url": "https://github.com/" + os.environ["GITHUB_REPOSITORY"]},
            "scanned": timestamp(),
            "manifests": {"dependencies.json": {"name": "dependencies.json",
                         "file": {"source_location": "dependencies.json"}, "resolved": resolved}}}


def osv(components):
    # Query only verified upstream baselines. Modified vendored code may
    # differ from upstream, and a missing OSV record is not proof of safety.
    covered = [c for c in components if c["upstream_commit"]]
    request = urllib.request.Request("https://api.osv.dev/v1/querybatch",
              data=json.dumps({"queries": [{"commit": c["upstream_commit"]} for c in covered]}).encode(),
              headers={"Content-Type": "application/json", "User-Agent": "AlterEgo-dependency-audit"})
    with urllib.request.urlopen(request, timeout=60) as response:
        results = json.load(response)["results"]
    if len(results) != len(covered):
        raise ValueError("Incomplete OSV response")
    report = []
    for c, result in zip(covered, results):
        vulns = result.get("vulns", [])
        report.append({"id": c["id"], "version": c["version"], "baseline": c["upstream_commit"],
                       "status": "matches" if vulns else "no matches (not a safety guarantee)",
                       "advisories": sorted({v["id"] for v in vulns})})
    report += [{"id": c["id"], "version": c["version"], "status": "manual review required",
                "advisories": []} for c in components if not c["upstream_commit"]]
    return {"queried_at": timestamp(), "limitations": "OSV commit coverage varies; local modifications and unknown upstream commits require manual review.",
            "components": report}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", default="dependency-reports")
    parser.add_argument("--snapshot", action="store_true")
    parser.add_argument("--osv", action="store_true")
    args = parser.parse_args()
    data = json.loads((ROOT / "dependencies.json").read_text(encoding="utf-8"))
    components = validate(data)
    sha = git("rev-parse", "HEAD")
    out = Path(args.output)
    write(out / "sbom.cdx.json", sbom(components, sha))
    print(f"Verified {len(components)} component fingerprints; generated CycloneDX SBOM.")
    if args.snapshot:
        if os.environ["GITHUB_SHA"] != sha or not os.environ["GITHUB_REF"].startswith("refs/heads/"):
            raise ValueError("Snapshot requires exact checked-out branch commit")
        write(out / "github-snapshot.json", snapshot(components, sha))
    if args.osv:
        report = osv(components)
        write(out / "osv-report.json", report)
        summary = ["## Vendored dependency advisory review", "",
                   "| Component | Version | OSV result |", "| --- | --- | --- |"]
        for c in report["components"]:
            result = ", ".join(c["advisories"]) or c["status"]
            summary.append(f"| {c['id']} | {c['version']} | {result} |")
            if c["advisories"]:
                print(f"::warning title=Vendored dependency advisory::{c['id']}: {result}")
        summary += ["", report["limitations"]]
        print("\n".join(summary))
        if os.environ.get("GITHUB_STEP_SUMMARY"):
            with open(os.environ["GITHUB_STEP_SUMMARY"], "a", encoding="utf-8") as f:
                f.write("\n".join(summary) + "\n")


if __name__ == "__main__":
    try:
        main()
    except (AssertionError, ValueError, OSError, KeyError, subprocess.CalledProcessError) as exc:
        sys.exit(f"Dependency audit failed: {exc}")
