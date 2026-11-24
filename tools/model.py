"""Single place that turns analysis/ + records/ into the function table everything else reads.

Status ladder (see AGENTS.md):
    located -> named -> typed -> decompiled -> compiles -> matches -> integrated

`located` comes from analysis/<build>/functions.tsv. `named` comes from records/names.tsv (there is no build
with symbols, so every name is an identification with recorded evidence) or from an import's name. Everything
above that comes only from records/functions.tsv.
"""
import csv
import tomllib
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LADDER = ["located", "named", "typed", "decompiled", "compiles", "matches", "integrated"]
csv.field_size_limit(1 << 30)


def read_tsv(path: Path):
    if not path.exists():
        return []
    with open(path, newline="") as f:
        return list(csv.DictReader(f, delimiter="\t", quoting=csv.QUOTE_NONE))


@dataclass
class Fn:
    build: str
    va: str
    size: int = 0
    name: str = ""
    cls: str = ""         # owning class, when known
    subsystem: str = ""   # e.g. engine.graphics; "_import" for import stubs, "_unknown" otherwise
    status: str = "located"
    src: str = ""         # authored reconstruction file, once one exists
    note: str = ""


def config():
    return tomllib.loads((ROOT / "target.toml").read_text())


def load(build: str) -> dict[str, Fn]:
    """Every function of a build, keyed by address. Raises ValueError on records that contradict analysis."""
    fns: dict[str, Fn] = {}
    for r in read_tsv(ROOT / "analysis" / build / "functions.tsv"):
        f = fns[r["va"]] = Fn(build=build, va=r["va"], size=int(r["size"] or 0), subsystem="_unknown")
        if r["name_source"] == "IMPORTED":
            f.name, f.subsystem, f.status = r["name"], "_import", "named"
    problems = []
    for r in read_tsv(ROOT / "records" / "names.tsv"):
        if r["build"] != build or r["kind"] != "function":
            continue
        f = fns.get(r["va"])
        if f is None:
            if fns:
                problems.append(f"names.tsv: {r['va']} {r['name']} is not a function in analysis/{build}")
            continue
        f.name, f.cls, f.subsystem, f.status = r["name"], r["class"], r["subsystem"] or "_unknown", "named"
    for r in read_tsv(ROOT / "records" / "functions.tsv"):
        if r["build"] != build:
            continue
        f = fns.get(r["va"])
        if f is None:
            problems.append(f"functions.tsv: {r['va']} is not a function in analysis/{build}")
            continue
        if r["status"] not in LADDER:
            problems.append(f"functions.tsv: {r['va']} has unknown status {r['status']}")
            continue
        f.status, f.src, f.note = r["status"], r["src"], r["note"]
        if LADDER.index(f.status) >= LADDER.index("decompiled") and not (f.src and (ROOT / f.src).exists()):
            problems.append(f"functions.tsv: {r['va']} is {f.status} but its source file {f.src!r} does not exist")
    if problems:
        raise ValueError("\n".join(problems))
    return fns
