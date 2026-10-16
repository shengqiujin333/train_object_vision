#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Host-side pretest of the fixed NEG_FIX_PY (paths substituted to repo layout)."""
from __future__ import annotations

import re
import sys

HERE = __import__("os").path.dirname(__file__)
DRIVER = HERE + "/verify_packaging25_fixround.py"
body = open(DRIVER, encoding="utf-8").read()
m = re.search(r"NEG_FIX_PY = r'''\n(.*?)'''", body, re.S)
script = m.group(1)
script = script.replace("/tmp/railhmi_build/tools/check_packaging.py",
                        r"E:/work/rail_issue/rk3588_handover/app/tools/check_packaging.py")
script = script.replace("/tmp/railhmi_build/src",
                        r"E:/work/rail_issue/rk3588_handover/app/src")
script = script.replace("/tmp/railhmi_build/packaging",
                        r"E:/work/rail_issue/rk3588_handover/packaging")
script = script.replace("/tmp/railhmi_verify025f/neg",
                        r"C:/Users/123/AppData/Local/Temp/negtest025")
tmp = r"C:\Users\123\AppData\Local\Temp\negtest025_local.py"
open(tmp, "w", encoding="utf-8").write(script)
rc = __import__("subprocess").run([sys.executable, tmp]).returncode
print("LOCAL_NEG_EXIT=%d" % rc)
sys.exit(rc)
