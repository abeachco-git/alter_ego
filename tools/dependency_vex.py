"""Product-scoped vulnerability assessments. No advisory IDs are removed."""
import re
import subprocess

STATUSES = {"not_affected", "affected", "fixed", "under_investigation"}
JUSTIFICATIONS = {"component_not_present", "vulnerable_code_not_present",
                  "vulnerable_code_not_in_execute_path",
                  "vulnerable_code_cannot_be_controlled_by_adversary",
                  "inline_mitigations_already_exist"}


def load_assessments(data, components, git):
    if data["schema_version"] != 1:
        raise ValueError("Unsupported vulnerability assessment schema")
    known = {c["id"]: c for c in components}
    result = {}
    for a in data["assessments"]:
        key = (a["component"], a["advisory"])
        if (key in result or a["component"] not in known
                or a["status"] not in STATUSES or not a["evidence"]
                or not a["source"].startswith("https://") or not a["guards"]
                or not re.fullmatch(r"\d{4}-\d{2}-\d{2}", a["reviewed"])):
            raise ValueError(f"Invalid or duplicate vulnerability assessment: {key}")
        if a["status"] == "not_affected" and a.get("justification") not in JUSTIFICATIONS:
            raise ValueError(f"Missing valid VEX justification: {key}")
        if a["status"] == "affected" and not a.get("action"):
            raise ValueError(f"Missing remediation action: {key}")
        guards = {g["path"]: g["git_object"] for g in a["guards"]}
        if len(guards) != len(a["guards"]):
            raise ValueError(f"Duplicate assessment guard: {key}")
        for path, oid in guards.items():
            if not path or not re.fullmatch("[0-9a-f]{40}", oid):
                raise ValueError(f"Invalid assessment guard: {key}")
        component = known[a["component"]]
        stale = []
        if component["version"] != a["version"]:
            stale.append("dependency version changed")
        for p in component["paths"]:
            if guards.get(p["path"]) != p["git_object"]:
                stale.append("dependency fingerprint changed: " + p["path"])
        for path, oid in guards.items():
            try:
                if git("rev-parse", "HEAD:" + path) != oid or git(
                        "status", "--porcelain", "--untracked-files=all", "--", path):
                    stale.append("reviewed input changed: " + path)
            except subprocess.CalledProcessError:
                stale.append("reviewed input missing: " + path)
        if key == ("sqlite", "CVE-2026-119586"):
            if not {"CMakeLists.txt", "cmake", ".github/workflows/windows-dll.yml"} <= guards.keys():
                stale.append("missing SQLite build guards")
            if any(re.search(r"(?:^|/)(?:shell\.c|sqlite3(?:\.exe|\.dll)?)$", p, re.I)
                   for p in git("ls-files").splitlines()):
                stale.append("SQLite shell or standalone binary present")
        evaluated = dict(a)
        evaluated["stale_reasons"] = stale
        evaluated["effective_status"] = "under_investigation" if stale else a["status"]
        result[key] = evaluated
    return result


def annotate(report, assessments):
    for c in report["components"]:
        c["findings"] = []
        for advisory in c["advisories"]:
            a = assessments.get((c["id"], advisory))
            c["findings"].append({
                "advisory": advisory,
                "status": a["effective_status"] if a else "under_investigation",
                "assessment": a,
            })
    report["recorded_assessments"] = list(assessments.values())
    return report


def openvex(assessments, components, sha, timestamp, repository):
    purls = {c["id"]: c["purl"] for c in components}
    product = "pkg:github/" + repository.lower() + "@" + sha
    statements = []
    for a in assessments.values():
        status = a["effective_status"]
        s = {"vulnerability": {"name": a["advisory"]},
             "products": [{"@id": product, "subcomponents": [
                 {"@id": purls[a["component"]]}]}],
             "status": status,
             "status_notes": ("Review required: " + "; ".join(a["stale_reasons"])
                              if a["stale_reasons"] else a["evidence"]
                              + " Source: " + a["source"] + " Reviewed: " + a["reviewed"])}
        if status == "not_affected":
            s["justification"] = a["justification"]
            s["impact_statement"] = a["evidence"]
        if status == "affected":
            s["action_statement"] = a["action"]
        statements.append(s)
    return {"@context": "https://openvex.dev/ns/v0.2.0",
            "@id": "https://github.com/" + repository + "/commit/" + sha + "#openvex",
            "author": "Alter Ego dependency assessment", "timestamp": timestamp,
            "version": 1, "statements": statements}
