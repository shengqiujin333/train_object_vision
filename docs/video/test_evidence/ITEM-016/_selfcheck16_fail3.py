"""Offline re-derivation of the 3 failing vext cases from delivery source."""
import re
from pathlib import Path

ROOT = Path(r"E:/work/rail_issue/rk3588_handover")

core_src = (ROOT / "app/src/core/src/video_diagnostics.cpp").read_text(encoding="utf-8")
health_src = (ROOT / "app/src/core/src/health.cpp").read_text(encoding="utf-8")
maint_src = (ROOT / "app/src/core/src/maintenance.cpp").read_text(encoding="utf-8")


def contains(hay, needle):
    return hay.find(needle) != -1


def mask_value_after(s, key):
    pos = 0
    out = s
    while True:
        pos = out.find(key, pos)
        if pos == -1:
            break
        v = pos + len(key)
        if v >= len(out):
            break
        if out[v] == '"':
            end = out.find('"', v + 1)
            if end == -1:
                out = out[:v + 1]
                break
            out = out[:v + 1] + "***" + out[end + 1:]
            pos = v + 4
        else:
            end = v
            while end < len(out) and out[end] not in "& \t),":
                end += 1
            out = out[:v] + "***" + out[end:]
            pos = v + 3
    return out


def redact_error(text):
    s = text
    pos = 0
    while True:
        pos = s.find("://", pos)
        if pos == -1:
            break
        start = pos + 3
        at = s.find("@", start)
        stop = -1
        for i, ch in enumerate(s[start:], start=start):
            if ch in "/ \t\n":
                stop = i
                break
        if at != -1 and (stop == -1 or at < stop):
            s = s[:start] + "***" + s[at:]
            pos = start + 3
        else:
            pos = start
    for key in ["password=", "passwd=", "pwd=", "user-pw=", "token=", "secret=",
                "user-id=", "user=", "location=", "url="]:
        s = mask_value_after(s, key)
    if any(contains(s, w) for w in ["rtspsrc", "GstPipeline", "GST_MESSAGE", "pipeline0", "gst_pipeline"]):
        return "<redacted>"
    return s


def contains_credentials(text):
    pos = 0
    while True:
        pos = text.find("://", pos)
        if pos == -1:
            break
        start = pos + 3
        at = text.find("@", start)
        stop = -1
        for i, ch in enumerate(text[start:], start=start):
            if ch in "/ \t\n":
                stop = i
                break
        if at != -1 and (stop == -1 or at < stop):
            return True
        pos = start
    for key in ["password=", "passwd=", "pwd=", "user-pw=", "token=", "secret=", "user-id=", "user="]:
        if contains(text, key):
            return True
    if any(contains(text, w) for w in ["rtspsrc", "GstPipeline", "GST_MESSAGE", "pipeline0"]):
        return True
    return False


# g12 probe: redact "rtsp://bob:hunter2@cams.example:554/live failed" then contains_credentials
raw = "rtsp://bob:hunter2@cams.example:554/live failed"
r = redact_error(raw)
print("g11 redact:", repr(r))
print("g12 contains_credentials(redacted):", contains_credentials(r))
# matched@ after :// with *** before it → contains returns True (delivered semantics).
# vext g12 expectation is WRONG (fixture-side error). Also delivered test only checks "S3CRET"/"admin:" absent.

# g22: severity JSON. Delivered log_record_json uses to_string(r.severity) → "info" lowercase.
# My check accepted "\"severity\":\"info\"" or "\"severity\":\"Info\"" — but log_record_json actually
# uses key "level", not "severity"!! Confirmed in health.cpp: {"ts_ms":..,"level":"info",...
print()
print("g22 expected key: level, not severity  → fixture error")

# g44: log_day_key(0). Find actual implementation of log_day_key (in maintenance.cpp?).
i = health_src.find("log_day_key")
print()
print(health_src[max(0, i - 300):i + 300])
# also check definition
m = re.search(r"log_day_key\(long long ts_wall_ms\)\s*\{(.{0,600}?)\}", health_src + maint_src, re.S)
if m:
    print("log_day_key body:", m.group(0)[:500])
