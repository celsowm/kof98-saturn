#!/usr/bin/env python3
"""Size the native hot set from an X1 guest profile.

Reads the text profiles written by the host core (`NG_PROFILE_OUT`, header
`# kof-guest-profile-v1`) plus the optional executed-word dumps
(`NG_PROFILE_WORDS`, header `# kof-guest-words-v1`). It never touches a ROM:
the inputs are already reduced guest addresses and counts.

What it answers, for one or more runs (a fight window, an idle/attract window,
their union):

  * how many instructions run in each region (P1, P2 banks, BIOS)
  * which 4 KiB pages and 16 KiB recompilation units carry them
  * the native text needed to cover 50/80/90/95/99% of the instructions,
    estimated from measured target sizes per unit
  * how much of the run a budgeted native subset keeps, i.e. the interpreter
    fallback share that decides whether native paging can reach real time

The estimate is deliberately pessimistic and clearly labelled: per-unit sizes
are the measured page-0 P1 sizes (-Os 334,724 B, -O2 479,428 B, -O3 514,020 B
per 16 KiB) and the measured 8-unit BIOS slice (585,656 B -O2, 625,840 B -O3).
Real units vary; the point is to rule a plan in or out before building it.

Usage:
  python tools/profile_guest.py local/profile/fight_3900_5000.txt \
      --words local/profile/fight_3900_5000.words
"""
import argparse
import json
import re
from pathlib import Path

HEADER = "# kof-guest-profile-v1"
WORDS_HEADER = "# kof-guest-words-v1"
GAME_BYTES = 0x300000
BIOS_BASE = 0xC00000
BIOS_BYTES = 0x20000
UNIT_BYTES = 0x4000
PAGE_BYTES = 0x1000
LINE_BYTES = 0x40

PAGE_RE = re.compile(r"^page ([0-9A-F]{6}) hits ([0-9]+) words ([0-9]+)$")
LINE_RE = re.compile(r"^line ([0-9A-F]{6}) hits ([0-9]+)$")
RANGE_RE = re.compile(r"^([0-9A-F]{6})-([0-9A-F]{6})$")
SUMMARY_KEYS = ("instructions", "distinct_words", "distinct_pages")

# Measured on the target (sh2eb-elf) for one 16 KiB unit; see
# docs/NATIVE_68000_BOOT.md. BIOS figures are the whole 8-unit slice / 8.
UNIT_SIZE = {
    "-Os": {"game": 334724, "bios": 585656 // 8},
    "-O2": {"game": 479428, "bios": 585656 // 8},
    "-O3": {"game": 514020, "bios": 625840 // 8},
}
OPT_LEVELS = ("-Os", "-O2", "-O3")


def region_of(addr):
    if addr < 0x100000:
        return "p1"
    if addr < GAME_BYTES:
        return "p2"
    if BIOS_BASE <= addr < BIOS_BASE + BIOS_BYTES:
        return "bios"
    raise ValueError(f"address outside the profiled windows: {addr:06X}")


def unit_of(addr):
    if addr < GAME_BYTES:
        return addr & ~(UNIT_BYTES - 1)
    return BIOS_BASE + ((addr - BIOS_BASE) & ~(UNIT_BYTES - 1))


def check_addr(addr, align, what):
    region_of(addr)
    if addr % align:
        raise ValueError(f"{what} {addr:06X} is not {align}-byte aligned")


def parse_profile(path):
    """Return (summary, {page: (hits, words)}, {line: hits})."""
    summary = {}
    pages = {}
    lines = {}
    page_hits = 0
    line_hits = 0
    expected_line_hits = {}
    with path.open("r", encoding="ascii") as f:
        if f.readline().rstrip("\n") != HEADER:
            raise ValueError(f"{path}: not a {HEADER} profile")
        for lineno, raw in enumerate(f, 2):
            text = raw.strip()
            if not text:
                continue
            if text.startswith("page "):
                m = PAGE_RE.match(text)
                if not m:
                    raise ValueError(f"{path}:{lineno}: bad page record")
                page = int(m.group(1), 16)
                hits = int(m.group(2))
                words = int(m.group(3))
                check_addr(page, PAGE_BYTES, "page")
                if page in pages:
                    raise ValueError(f"{path}:{lineno}: duplicate page")
                if hits <= 0 or words <= 0:
                    raise ValueError(f"{path}:{lineno}: empty page record")
                pages[page] = (hits, words)
                page_hits += hits
                expected_line_hits[page] = 0
            elif text.startswith("line "):
                m = LINE_RE.match(text)
                if not m:
                    raise ValueError(f"{path}:{lineno}: bad line record")
                line = int(m.group(1), 16)
                hits = int(m.group(2))
                check_addr(line, LINE_BYTES, "line")
                if line in lines:
                    raise ValueError(f"{path}:{lineno}: duplicate line")
                if hits <= 0:
                    raise ValueError(f"{path}:{lineno}: empty line record")
                page = line & ~(PAGE_BYTES - 1)
                if page not in pages:
                    raise ValueError(f"{path}:{lineno}: line outside any page record")
                lines[line] = hits
                line_hits += hits
                expected_line_hits[page] += hits
            elif "=" in text:
                key, _, value = text.partition("=")
                if key not in SUMMARY_KEYS or key in summary or not value.isdigit():
                    raise ValueError(f"{path}:{lineno}: bad summary line")
                summary[key] = int(value)
            else:
                raise ValueError(f"{path}:{lineno}: unknown record")
    if set(summary) != set(SUMMARY_KEYS):
        raise ValueError(f"{path}: incomplete summary")
    if not lines:
        raise ValueError(f"{path}: no line records (regenerate with the X1 profiler)")
    if page_hits != summary["instructions"] or line_hits != summary["instructions"]:
        raise ValueError(f"{path}: page/line hit sum does not match instructions")
    if len(pages) != summary["distinct_pages"]:
        raise ValueError(f"{path}: distinct_pages does not match page records")
    for page, (hits, _) in pages.items():
        if expected_line_hits[page] != hits:
            raise ValueError(f"{path}: lines of page {page:06X} do not sum to its hits")
    return summary, pages, lines


def parse_words(path):
    """Return a set of executed word addresses from a word-range dump."""
    words = set()
    with path.open("r", encoding="ascii") as f:
        if f.readline().rstrip("\n") != WORDS_HEADER:
            raise ValueError(f"{path}: not a {WORDS_HEADER} dump")
        previous_end = -1
        for lineno, raw in enumerate(f, 2):
            text = raw.strip()
            if not text:
                continue
            m = RANGE_RE.match(text)
            if not m:
                raise ValueError(f"{path}:{lineno}: bad range")
            start, end = int(m.group(1), 16), int(m.group(2), 16)
            check_addr(start, 2, "word range start")
            check_addr(end, 2, "word range end")
            if start > end or start <= previous_end:
                raise ValueError(f"{path}:{lineno}: ranges must be ordered and disjoint")
            previous_end = end
            words.update(range(start, end + 1, 2))
    if not words:
        raise ValueError(f"{path}: empty word dump")
    return words


def merge(profiles):
    """Sum hits and union words over runs; require one words dump per profile."""
    total = {}
    per_page = {}
    per_line = {}
    per_profile = {}
    all_words = set()
    for summary, pages, lines, words in profiles:
        total.setdefault("instructions", 0)
        total["instructions"] += summary["instructions"]
        for page, (hits, wc) in pages.items():
            per_page.setdefault(page, [0, 0])
            per_page[page][0] += hits
            per_page[page][1] = max(per_page[page][1], wc)
            per_profile[page] = per_profile.get(page, 0) + 1
        for line, hits in lines.items():
            per_line[line] = per_line.get(line, 0) + hits
        all_words |= words
    return total["instructions"], per_page, per_line, per_profile, all_words


def group_units(per_page, per_line, words):
    """Aggregate page/line hits and executed words per 16 KiB recompilation unit."""
    units = {}
    for page, (hits, _) in per_page.items():
        unit = unit_of(page)
        u = units.setdefault(unit, {"hits": 0, "pages": 0, "lines": 0, "words": 0})
        u["hits"] += hits
        u["pages"] += 1
        u["words"] += len([w for w in range(page, page + PAGE_BYTES, 2) if w in words])
    for line, hits in per_line.items():
        units[unit_of(line)]["lines"] += hits
    return units


def size_of(unit, opt):
    return UNIT_SIZE[opt]["bios" if region_of(unit) == "bios" else "game"]


def coverage_rows(units, total, thresholds):
    """Cumulative hot-unit coverage, sorting by dynamic hits."""
    ordered = sorted(units, key=lambda u: (-units[u]["hits"], u))
    rows = []
    running = 0
    index = 0
    for threshold in thresholds:
        while index < len(ordered) and running * 100 < total * threshold:
            running += units[ordered[index]]["hits"]
            index += 1
        chosen = ordered[:index]
        row = {
            "coverage": threshold,
            "units": len(chosen),
            "instructions": running,
            "share": running / total if total else 0.0,
        }
        for opt in OPT_LEVELS:
            row[opt] = sum(size_of(u, opt) for u in chosen)
        rows.append(row)
    return ordered, rows


def budget_selection(units, total, budget, opt):
    """Greedy hits per native byte; returns selected units and report."""
    ranked = sorted(units, key=lambda u: (-units[u]["hits"] / size_of(u, opt), u))
    used = 0
    kept = []
    for unit in ranked:
        cost = size_of(unit, opt)
        if used + cost > budget:
            continue
        used += cost
        kept.append(unit)
    hits = sum(units[u]["hits"] for u in kept)
    return {
        "opt": opt,
        "budget": budget,
        "bytes": used,
        "units": len(kept),
        "unit_addresses": [f"{u:06X}" for u in kept],
        "instructions": hits,
        "share": hits / total if total else 0.0,
        "fallback_share": 1.0 - (hits / total if total else 0.0),
    }


def report(profiles, words_paths, budget, thresholds, top, as_json):
    loaded = []
    for path, words_path in zip(profiles, words_paths):
        summary, pages, lines = parse_profile(path)
        words = parse_words(words_path) if words_path else set()
        if words:
            if len(words) != summary["distinct_words"]:
                raise ValueError(f"{path}: distinct_words does not match the word dump")
            for page, (_, wc) in pages.items():
                actual = len([w for w in range(page, page + PAGE_BYTES, 2) if w in words])
                if actual != wc:
                    raise ValueError(f"{path}: page {page:06X} word count does not match the dump")
        loaded.append((summary, pages, lines, words))
    total, per_page, per_line, per_profile, all_words = merge(loaded)
    units = group_units(per_page, per_line, all_words)

    regions = {}
    for page, (hits, words) in per_page.items():
        r = regions.setdefault(region_of(page), {"hits": 0, "pages": 0, "words": 0, "units": set()})
        r["hits"] += hits
        r["pages"] += 1
        r["words"] += len([w for w in range(page, page + PAGE_BYTES, 2) if w in all_words])
        r["units"].add(unit_of(page))
    for r in regions.values():
        r["units"] = len(r["units"])

    ordered, rows = coverage_rows(units, total, thresholds)
    out = {
        "profiles": [str(p) for p in profiles],
        "instructions": total,
        "distinct_pages": len(per_page),
        "distinct_words": len(all_words),
        "units": len(units),
        "unit_bytes": UNIT_BYTES,
        "regions": regions,
        "top_pages": [
            {"page": f"{p:06X}", "hits": h, "share": h / total, "words": w,
             "runs": per_profile[p]}
            for p, (h, w) in sorted(per_page.items(), key=lambda kv: (-kv[1][0], kv[0]))[:top]
        ],
        "top_units": [
            {"unit": f"{u:06X}", "hits": units[u]["hits"], "share": units[u]["hits"] / total,
             "pages": units[u]["pages"], "words": units[u]["words"],
             "native_os": size_of(u, "-Os"), "native_o2": size_of(u, "-O2")}
            for u in ordered[:top]
        ],
        "coverage": rows,
        "budget": [budget_selection(units, total, budget, opt) for opt in OPT_LEVELS],
    }

    if as_json:
        print(json.dumps(out, indent=2))
        return out

    print("# kof-guest-profile report (16 KiB recompilation units)")
    print(f"profiles: {len(profiles)}")
    print(f"instructions: {total:,}")
    print(f"distinct pages: {len(per_page):,}  units: {len(units):,}  "
          f"distinct executed words: {len(all_words):,}")
    for name in ("p1", "p2", "bios"):
        r = regions.get(name)
        if not r:
            continue
        print(f"region {name}: hits {r['hits']:,} ({r['hits'] / total:6.2%})  "
              f"pages {r['pages']}  units {r['units']}  executed bytes {r['words'] * 2:,}")

    print("\ntop pages:")
    for row in out["top_pages"]:
        print(f"  {row['page']}  hits {row['hits']:>10,}  {row['share']:6.2%}  "
              f"words {row['words']:>4}  runs {row['runs']}")

    print("\ntop units:")
    for row in out["top_units"]:
        print(f"  {row['unit']}  hits {row['hits']:>10,}  {row['share']:6.2%}  "
              f"pages {row['pages']:>2}  words {row['words']:>4}  "
              f"-Os {row['native_os']:>7,} B  -O2 {row['native_o2']:>7,} B")

    print("\ncumulative hot-unit coverage (needs no budget):")
    print("  cover  units  instructions   share       -Os bytes     -O2 bytes     -O3 bytes")
    for row in rows:
        print(f"  {row['coverage']:>4}%  {row['units']:>5}  {row['instructions']:>12,}  "
              f"{row['share']:6.2%}  {row['-Os']:>12,}  {row['-O2']:>12,}  {row['-O3']:>12,}")

    print(f"\nbudget selection (greedy hits/byte, budget {budget:,} B):")
    for sel in out["budget"]:
        print(f"  {sel['opt']}: units {sel['units']}  bytes {sel['bytes']:,}  "
              f"native share {sel['share']:6.2%}  interpreter fallback {sel['fallback_share']:6.2%}")
        print(f"        {', '.join(sel['unit_addresses'])}")
    return out


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("profiles", nargs="+", type=Path)
    p.add_argument("--words", type=Path, action="append", default=[],
                   help="one executed-word dump per profile, in the same order")
    p.add_argument("--budget", type=int, default=1500000,
                   help="native text budget in bytes (default: 1.5 MB)")
    p.add_argument("--coverage", type=int, nargs="+", default=[50, 80, 90, 95, 99])
    p.add_argument("--top", type=int, default=12)
    p.add_argument("--json", action="store_true")
    a = p.parse_args()
    try:
        if a.words and len(a.words) != len(a.profiles):
            raise ValueError("--words needs exactly one dump per profile")
        if a.budget <= 0 or a.top <= 0 or any(not 1 <= c <= 100 for c in a.coverage):
            raise ValueError("invalid budget, top or coverage value")
        report(a.profiles, a.words, a.budget, sorted(a.coverage), a.top, a.json)
    except (OSError, ValueError) as e:
        p.error(str(e))


if __name__ == "__main__":
    main()
