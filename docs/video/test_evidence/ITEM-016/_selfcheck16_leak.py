import re
from pathlib import Path

HERE = Path(__file__).resolve().parent
patterns = [
    re.compile(r"password=\S+"),
    re.compile(r"rtsp://[^\s/]+:[^\s/@]+@"),
    re.compile(r"location=rtsp://[^\s]*?"),
    re.compile(r"pwd=\S+"),
    re.compile(r"token=\S+"),
]
body = (HERE / "ITEM-016-ENV-A0-C-20261017-r1.md").read_text(encoding="utf-8", errors="ignore")
for pat in patterns:
    hits = []
    for mm in pat.finditer(body):
        frag = mm.group(0)[:60]
        excluded = re.fullmatch(r"password=(admin|\*\*\*)|pwd=(admin|\*\*\*|password)", frag)
        marker = "HIT" if not excluded and "***" not in frag else "excluded"
        hits.append((frag, marker))
    print(pat.pattern, len(hits))
    for h in hits:
        print("   ", h)
