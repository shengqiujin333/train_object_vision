import json
import re
from pathlib import Path

HERE = Path(__file__).resolve().parent


def load_steps(f):
    d = json.load(open(f, encoding="utf-8"))
    return {s["step"]: s.get("output", "") for s in d["steps"]}


v = load_steps(HERE / "diag-192_168_0_103-20261017-item016-verify-v1.transcript.json")
e = load_steps(HERE / "diag-ext-192_168_0_103-20261017-item016-verify-v1.transcript.json")

main = v["run-diag"]
exd = e.get("EXD-run2", "")


def lines(text, pref):
    return [l for l in text.splitlines() if l.startswith(pref)]


for pref in ("type=acceptance", "type=capability", "type=element", "type=redaction"):
    a = lines(main, pref)
    b = lines(exd, pref)
    print(pref, "identical:", a == b, "counts", len(a), len(b))

# acceptance/capability/element/redaction rows are state-free (no wall clock) —
# they must be constant across runs; metric rows fluctuate (mono/wall/drops) by design.
