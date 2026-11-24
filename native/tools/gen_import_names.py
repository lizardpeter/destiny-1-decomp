#!/usr/bin/env python3
"""Write import_names.inc ({"NID", "name"}, ...) from records/import_names.tsv."""
import csv
import sys

src, out = sys.argv[1], sys.argv[2]
rows = {}
for r in csv.DictReader(open(src, newline=""), delimiter="\t"):
    if r["name"]:
        rows[r["nid"]] = r["name"]
with open(out, "w") as f:
    for nid, name in sorted(rows.items()):
        f.write('{"%s", "%s"},\n' % (nid, name.replace("\\", "\\\\").replace('"', '\\"')))
