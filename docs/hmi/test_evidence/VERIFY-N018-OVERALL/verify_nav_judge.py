import json, re
from pathlib import Path
t = json.load(open("docs/hmi/test_evidence/VERIFY-N018-OVERALL/ui-nav-transcript.json", encoding="utf-8"))
cap = [s for s in t["steps"] if "capture.sh" in s["cmd"]][0]["output"]
m = re.search(r"=== app\.log qml-error count ===\n(.*?)(APPLOG_LINES=\d+)", cap, re.S)
print("app.log tail + counts:\n", (m.group(1) + m.group(2))[:900])
# pairwise pixel-difference of six pages on host
sizes = [("p1_realtime", 52401), ("p2_alarm_center", 91697), ("p3_replay", 86110),
         ("p4_all_cameras", 54542), ("p5_ai_debug", 96044), ("p6_maintenance", 154030)]
d = "docs/hmi/test_evidence/VERIFY-N018-OVERALL/"
import struct, zlib
def chg(path):
    try:
        from PIL import Image
        im = Image.open(path); return im.convert("L")
    except ImportError:
        return None
imgs = [(n, chg(d + n + ".png")) for n, _ in sizes]
if imgs[0][1] is None:
    print("PIL unavailable; size-uniqueness only")
else:
    import itertools
    for (a, ia), (b, ib) in itertools.combinations(imgs, 2):
        if ia.size != ib.size:
            print(a, b, "SIZE-DIFF"); continue
        hist_a = ia.histogram(); hist_b = ib.histogram()
        diff = sum(abs(x - y) for x, y in zip(hist_a, hist_b)) / (ia.size[0] * ia.size[1])
        print(a, b, "hist_diff=%.4f" % diff, "OK" if diff > 0.03 else "SAME?")
