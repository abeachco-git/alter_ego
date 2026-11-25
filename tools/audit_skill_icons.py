#!/usr/bin/env python3
"""Read-only skill-icon inventory and measured artwork-size audit for Alter Ego.

This script NEVER modifies, generates, embeds, or downloads game resources
into the repository/DLL. It samples image URLs in memory for size estimates.
Counts are sourced from the public live GW2 /v2/professions and /v2/skills APIs.
"""
import argparse
import collections
import hashlib
import io
import json
import random
import statistics
import sys
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen

BASE = "https://api.guildwars2.com"
HEADERS = {"User-Agent": "AlterEgo-SkillIconAudit/1.0",
           "Accept": "application/json,image/png,image/jpeg,*/*"}


def download(url, timeout=100):
    failure = None
    for retry in range(3):
        try:
            with urlopen(Request(url, headers=HEADERS), timeout=timeout) as response:
                return response.read()
        except (HTTPError, URLError, TimeoutError, OSError) as error:
            failure = error
            time.sleep(1.5 * (retry + 1))
    raise RuntimeError(f"{url}: {failure}")


def api(path):
    return json.loads(download(BASE + path).decode("utf-8"))


def all_skills():
    try:
        skills = api("/v2/skills?ids=all&v=latest")
        if not isinstance(skills, list) or not skills or not isinstance(skills[0], dict):
            raise ValueError("All-skills query did not return detail objects")
        return skills
    except Exception as exc:
        print(f"All-skills response unavailable ({exc}); using paginated detail batches",
              file=sys.stderr)
        ids = api("/v2/skills")
        def part(batch):
            return api("/v2/skills?ids=" + ",".join(map(str, batch)) + "&v=latest")
        batches = [ids[i:i + 170] for i in range(0, len(ids), 170)]
        result = []
        with ThreadPoolExecutor(max_workers=7) as executor:
            for batch in executor.map(part, batches):
                result.extend(batch)
        return result


def skill_id(entry):
    try:
        value = entry.get("id") if isinstance(entry, dict) else entry
        number = int(value)
        return number if number > 0 else None
    except (TypeError, ValueError):
        return None


def image_bytes(url):
    try:
        data = download(url, timeout=18)
        if data[:8] == b"\x89PNG\r\n\x1a\n":
            form = "png"
            dims = [int.from_bytes(data[16:20], "big"),
                    int.from_bytes(data[20:24], "big")]
        elif data[:3] == b"\xff\xd8\xff":
            form = "jpeg"
            dims = None
        else:
            return url, None
        outcome = {"original": len(data), "format": form, "dimensions": dims,
                   "hash": hashlib.sha256(data).hexdigest()}
        try:
            from PIL import Image
            with Image.open(io.BytesIO(data)) as image:
                outcome["dimensions"] = list(image.size)
                image = image.convert("RGBA")
                for side in (32, 48, 64):
                    resized = image.resize((side, side), Image.Resampling.LANCZOS)
                    buffer = io.BytesIO()
                    resized.save(buffer, "PNG", optimize=True)
                    outcome[f"png_{side}"] = len(buffer.getvalue())
                    try:
                        buffer = io.BytesIO()
                        resized.save(buffer, "WEBP", quality=85, method=4)
                        outcome[f"webp_{side}"] = len(buffer.getvalue())
                    except OSError:
                        pass
        except (ImportError, OSError, ValueError):
            pass
        return url, outcome
    except Exception as exc:
        return url, {"error": str(exc)}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--json", required=True)
    parser.add_argument("--markdown", required=True)
    parser.add_argument("--dll")
    parser.add_argument("--resource-dir")
    args = parser.parse_args()

    professions = api("/v2/professions?ids=all&v=latest")
    races = api("/v2/races?ids=all&v=latest")
    skills = all_skills()
    by_id = {skill_id(s): s for s in skills if skill_id(s)}
    groups = {key: set() for key in (
        "heal", "utility", "elite", "weapon", "aquatic",
        "profession_mechanic", "racial", "other_player", "selectable_palette"
    )}
    for profession in professions:
        for skill in profession.get("skills", []):
            sid = skill_id(skill)
            if sid is None:
                continue
            slot = skill.get("slot", "")
            if slot == "Heal":
                groups["heal"].add(sid)
            elif slot == "Utility":
                groups["utility"].add(sid)
            elif slot == "Elite":
                groups["elite"].add(sid)
            elif slot.startswith("Profession_"):
                groups["profession_mechanic"].add(sid)
            else:
                groups["other_player"].add(sid)
        for weapon in profession.get("weapons", {}).values():
            target = ("aquatic" if "Aquatic" in weapon.get("flags", [])
                      else "weapon")
            for skill in weapon.get("skills", []):
                sid = skill_id(skill)
                if sid is not None:
                    groups[target].add(sid)
        palette = profession.get("skills_by_palette", [])
        # The GW2 API returns [palette_id, skill_id] pairs, NOT bare IDs.
        # The palette id is used in build chat links; only the second element
        # is a real /v2/skills identifier. Also accept documented object forms.
        for entry in palette:
            if isinstance(entry, (list, tuple)) and len(entry) >= 2:
                sid = skill_id(entry[1])
            elif isinstance(entry, dict):
                sid = skill_id(entry.get("id"))
            else:
                sid = None
            if sid is not None:
                groups["selectable_palette"].add(sid)

    # Racial skills are explicitly enumerated by /v2/races. Skill metadata
    # does not consistently label them by type or flag.
    for race in races:
        for item in race.get("skills", []):
            sid = skill_id(item)
            if sid is not None:
                groups["racial"].add(sid)

    all_player = set().union(*(groups[key] for key in (
        "heal", "utility", "elite", "weapon", "aquatic",
        "profession_mechanic", "racial", "other_player",
        "selectable_palette")))
    core_picker = groups["heal"] | groups["utility"] | groups["elite"]
    profession_selectable = core_picker | groups["selectable_palette"] | groups["racial"]
    skill_urls = {sid: skill["icon"] for sid, skill in by_id.items()
                  if isinstance(skill.get("icon"), str) and skill["icon"]}
    strata = {
        "heal_utility_elite": core_picker,
        "profession_selectable": profession_selectable,
        "all_player_skills": all_player,
        "all_api_skills_with_icons": set(skill_urls),
    }
    selected = {name: ids & set(skill_urls) for name, ids in strata.items()}
    rng = random.Random(20261123)
    unique_urls = set()
    # Oversample the editor's actual working set, then broaden coverage.
    for name, amount in (("heal_utility_elite", 55),
                         ("all_player_skills", 55),
                         ("all_api_skills_with_icons", 45)):
        urls = sorted({skill_urls[id] for id in selected[name]})
        unique_urls.update(rng.sample(urls, min(len(urls), amount)))
    measured = {}
    with ThreadPoolExecutor(max_workers=10) as executor:
        futures = {executor.submit(image_bytes, u): u for u in unique_urls}
        for task in as_completed(futures):
            url, sizes = task.result()
            measured[url] = sizes
    report_strategies = {}
    for name, ids in strata.items():
        urls = {skill_urls[sid] for sid in selected[name]}
        samples = [measured[u] for u in urls if u in measured
                   and measured[u] and "original" in measured[u]]
        average = statistics.mean(s["original"] for s in samples) if samples else None
        estimates = {}
        if average:
            estimates["original_bytes"] = int(len(urls) * average)
            for format_key in ("png_32", "png_48", "png_64", "webp_48"):
                valid = [s[format_key] for s in samples if format_key in s]
                if valid:
                    estimates[format_key] = int(len(urls) * statistics.mean(valid))
        report_strategies[name] = {
            "skill_ids": len(ids),
            "skills_with_icon": len(selected[name]),
            "distinct_icon_urls": len(urls),
            "measured_sample_urls": len(samples),
            "measured_original_mean_bytes": round(average) if average else None,
            "estimated_total_bytes_by_format": estimates,
            "unavailable_icon_ids": len(ids - set(skill_urls)),
        }

    successful = {url: sample for url, sample in measured.items()
                  if sample and "original" in sample}
    hash_duplicate = len(successful) - len({s["hash"] for s in successful.values()})
    resource_dir = Path(args.resource_dir) if args.resource_dir else None
    fixed = list(resource_dir.glob("*")) if resource_dir and resource_dir.is_dir() else []
    report = {
        "source": "Guild Wars 2 public API; live, not an exhaustive game client skill list",
        "generated_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "total_api_skill_definitions": len(by_id),
        "professions": len(professions),
        "races": len(races),
        "categories_unique_ids": {k: len(v) for k, v in groups.items()},
        "categories_with_icon": {k: len(v & set(skill_urls)) for k, v in groups.items()},
        "strategies": report_strategies,
        "sample": {
            "requested_urls": len(unique_urls),
            "successful_urls": len(successful),
            "image_hash_duplicates_among_distinct_urls": hash_duplicate,
            "dimensions": dict(collections.Counter(
                str(s.get("dimensions")) for s in successful.values())),
            "original_bytes_range": (
                [min(s["original"] for s in successful.values()),
                 max(s["original"] for s in successful.values())]
                if successful else None),
        },
        "baseline": {
            "dll_bytes": Path(args.dll).stat().st_size if args.dll and Path(args.dll).exists() else None,
            "fixed_resource_icon_count": len(fixed),
            "fixed_resource_icon_bytes": sum(p.stat().st_size for p in fixed),
        },
        "methodology": [
            "Player subsets from /v2/professions skill and weapon listings plus [palette,skill] pairs",
            "Racial skill IDs from /v2/races; categories are not mutually exclusive",
            "Every potentially displayable icon is a broad upper bound: all /v2/skills entries with icon URLs, including non-selectable abilities",
            "Distinct URL counts remove duplicate URL artwork; sampled SHA256 additionally detects content duplicates",
            "Binary RCDATA growth is approximately original encoded image bytes plus minor resource metadata",
            "Size estimates extrapolate actual sampled URLs and are not measured complete catalogs",
            "Existing disk cache remains persistent and unchanged; no skill artwork is embedded",
        ],
    }
    out = Path(args.json)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n",
                   encoding="utf-8")
    lines = [
        "# Alter Ego: live GW2 skill-icon size audit",
        "",
        "Read-only measurements; this run **did not embed skill images**.",
        "",
        f"API skill definitions: {len(by_id)}; professions: {len(professions)}; races: {len(races)}.",
        "",
        "## Classification (unique skill IDs; categories can overlap)",
        "",
        "| Category | IDs | With icon |",
        "|---|---:|---:|",
    ]
    for key, ids in groups.items():
        lines.append(f"| {key} | {len(ids)} | {len(ids & set(skill_urls))} |")
    lines.extend(["", "## Embedding options (sample-extrapolated, not exact files)",
                  "", "| Option | IDs | Distinct icon URLs | Samples | Additional DLL (original art) | 48px PNG | 48px WebP |",
                  "|---|---:|---:|---:|---:|---:|---:|"])
    def mb(n):
        return f"{n / 1048576:.2f} MiB" if n else "n/a"
    for key, v in report_strategies.items():
        estimate = v["estimated_total_bytes_by_format"]
        lines.append(f"| {key} | {v['skill_ids']} | {v['distinct_icon_urls']} | "
                     f"{v['measured_sample_urls']} | {mb(estimate.get('original_bytes'))} | "
                     f"{mb(estimate.get('png_48'))} | {mb(estimate.get('webp_48'))} |")
    lines.extend(["", "These counts are upper bounds on the exact gameplay UI requirements; "
                  "not all available skills appear in Alter Ego's currently visible editor. "
                  "The 48px data estimates assume preprocessed images and a more "
                  "complex resource pipeline; no conversion is currently implemented.",
                  "",
                  "## Sample coverage and baseline",
                  "",
                  f"Successful artwork samples: {len(successful)}/{len(unique_urls)}.",
                  f"Duplicate image hashes from different sampled URLs: {hash_duplicate}.",
                  f"Current DLL bytes: {report['baseline']['dll_bytes']}.",
                  f"Existing fixed UI icons: {len(fixed)}, {report['baseline']['fixed_resource_icon_bytes']} bytes.",
                  "",
                  "For new game updates, use persistent disk cache and network fallback "
                  "for skills absent from the last compiled manifest.",
                  ""])
    Path(args.markdown).write_text("\n".join(lines), encoding="utf-8")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
