import re
import sys

import os
base = os.path.dirname(os.path.abspath(__file__)) + os.sep
p_r2 = base + "build-TestRun-192_168_0_103-20261015-ITEM010-r2.core-evidence_core-tests-20261015-ITEM010-r2.xml"
p_v1 = base + "build-TestRun-192_168_0_103-20261016-VERIFY-ITEM010-v1.core-evidence_core-tests-20261016-VERIFY-ITEM010-v1.xml"
xml_r2 = open(p_r2, encoding="utf-8", errors="replace").read()
xml_v1 = open(p_v1, encoding="utf-8", errors="replace").read()
m_r2 = re.search(r'<testsuites tests="(\d+)" failures="(\d+)"', xml_r2)
m_v1 = re.search(r'<testsuites tests="(\d+)" failures="(\d+)"', xml_v1)
print("r2 junit:", m_r2.group(0) if m_r2 else "N/A")
print("v1 junit:", m_v1.group(0) if m_v1 else "N/A")

import xml.etree.ElementTree as ET


def names(path):
    t = ET.parse(path)
    return [tc.get("name") for tc in t.iter("testcase")]


n_r2, n_v1 = names(p_r2), names(p_v1)
print("testcase counts:", len(n_r2), len(n_v1))
same = n_r2 == n_v1
print("testcase lists identical:", same)
if not same:
    sr, sv = set(n_r2), set(n_v1)
    print(" only-r2:", sorted(sr - sv)[:10])
    print(" only-v1:", sorted(sv - sr)[:10])

log_r2 = open(base + "build-TestRun-192_168_0_103-20261015-ITEM010-r2.core-evidence_core-tests-20261015-ITEM010-r2.log",
              encoding="utf-8", errors="replace").read()
log_v1 = open(base + "build-TestRun-192_168_0_103-20261016-VERIFY-ITEM010-v1.core-evidence_core-tests-20261016-VERIFY-ITEM010-v1.log",
              encoding="utf-8", errors="replace").read()


def norm(s):
    return re.sub(r"2026101" + r"[0-9]-[0-9A-Za-z-]+" + "", "STAMP", s)


lr2, lv1 = norm(log_r2).splitlines(), norm(log_v1).splitlines()
print("normalized log line counts:", len(lr2), len(lv1))
diffs = [(a, b) for a, b in zip(lr2, lv1) if a != b]
print("log diff lines:", len(diffs))
for a, b in diffs[:10]:
    print("  r2 : %s" % a)
    print("  v1 : %s" % b)
sys.exit(0)
