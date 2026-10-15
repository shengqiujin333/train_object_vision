#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""railhmi 渲染路径选择判据 (ITEM-002)

读取 render_spike 对 `--path gl` 与 `--path cpu` 的实测 JSON，按固定判据决定主路径。
本脚本不产生测量数据；没有实测输入时返回 UNMEASURED，不假定任何路径成立。

判据 (可调参数见 --min-fps / --max-spread):
  某路径可用 =
      playing == true                       # 管线到达 PLAYING
      and fps_source >= min_fps             # 源侧帧率
      and fps_display >= min_fps            # 该路径实际出图帧率
      and color.pass == true                # NV12->BGR 色彩校验通过
      and 无 fatal error (除已知 warning 外)
  选择: gl 可用 -> gl ; 否则 cpu 可用 -> cpu ; 否则 none
  无实测数据 -> UNMEASURED

用法:
  analyze.py --gl gl.json --cpu cpu.json [--min-fps 20] [--out selection.json]
  analyze.py --selftest
"""

from __future__ import annotations

import argparse
import json
import sys

FATAL_KEYS = ("error", "fail", "missing", "not available", "did not reach playing")


def path_usable(m, min_fps: float, max_spread: float):
    """返回 (usable: bool, reasons: list[str])"""
    reasons = []
    if not m:
        return False, ["no measurement"]
    playing = bool(m.get("playing"))
    fps_source = float(m.get("fps_source", 0) or 0)
    fps_display = float(m.get("fps_display", 0) or 0)
    color = m.get("color") or {}
    color_pass = bool(color.get("pass")) if color.get("valid") else False
    spread = color.get("spread")
    errors = [str(e) for e in (m.get("errors") or [])]

    if not playing:
        reasons.append("not playing (negotiation failed)")
    if fps_source < min_fps:
        reasons.append("fps_source %.1f < %.1f" % (fps_source, min_fps))
    if fps_display < min_fps:
        reasons.append("fps_display %.1f < %.1f" % (fps_display, min_fps))
    if not color_pass:
        reasons.append("color check failed")
    if spread is not None and float(spread) > max_spread:
        reasons.append("color spread %.1f > %.1f" % (float(spread), max_spread))
    for e in errors:
        low = e.lower()
        if any(k in low for k in FATAL_KEYS):
            reasons.append("fatal error: %s" % e)

    return (len(reasons) == 0), reasons


def decide(gl, cpu, min_fps=20.0, max_spread=12.0) -> dict:
    if gl is None and cpu is None:
        return {"status": "UNMEASURED", "selected": None,
                "note": "no board measurement provided; no path assumed (ITEM-002 requires real run)"}
    gl_ok, gl_reasons = path_usable(gl, min_fps, max_spread)
    cpu_ok, cpu_reasons = path_usable(cpu, min_fps, max_spread)
    if gl_ok:
        selected, status = "gl", "SELECTED"
    elif cpu_ok:
        selected, status = "cpu", "SELECTED"
    else:
        selected, status = None, "NONE_USABLE"
    return {
        "status": status,
        "selected": selected,
        "min_fps": min_fps,
        "max_spread": max_spread,
        "gl": {"usable": gl_ok, "reasons": gl_reasons,
               "fps_source": (gl or {}).get("fps_source"),
               "fps_display": (gl or {}).get("fps_display"),
               "cpu_pct_one_core": (gl or {}).get("cpu_pct_one_core"),
               "gpu_load_avg": (gl or {}).get("gpu_load_avg")},
        "cpu": {"usable": cpu_ok, "reasons": cpu_reasons,
                "fps_source": (cpu or {}).get("fps_source"),
                "fps_display": (cpu or {}).get("fps_display"),
                "cpu_pct_one_core": (cpu or {}).get("cpu_pct_one_core"),
                "gpu_load_avg": (cpu or {}).get("gpu_load_avg")},
    }


def _ok(playing=True, fps=25.0, spread=3.0, errors=None):
    return {"playing": playing, "fps_source": fps, "fps_display": fps,
            "color": {"valid": True, "pass": spread < 12.0, "spread": spread},
            "errors": errors or [], "cpu_pct_one_core": 60.0, "gpu_load_avg": 20.0}


def selftest() -> int:
    cases = [
        ("gl 可用 -> 选 gl", _ok(), _ok(), "SELECTED", "gl"),
        ("gl 协商失败 -> 回退 cpu", _ok(playing=False, fps=0),
         _ok(), "SELECTED", "cpu"),
        ("两条都失败 -> none", _ok(playing=False, fps=0),
         _ok(playing=False, fps=0), "NONE_USABLE", None),
        ("色彩不通过 -> 不可用", _ok(spread=30.0), _ok(spread=30.0), "NONE_USABLE", None),
        ("帧率不足 -> 不可用", _ok(fps=5.0), _ok(fps=5.0), "NONE_USABLE", None),
        ("无数据 -> UNMEASURED", None, None, "UNMEASURED", None),
    ]
    failed = 0
    for name, gl, cpu, exp_status, exp_sel in cases:
        r = decide(gl, cpu)
        ok = (r["status"] == exp_status and r["selected"] == exp_sel)
        print("[%s] %s -> status=%s selected=%s" %
              ("PASS" if ok else "FAIL", name, r["status"], r["selected"]))
        if not ok:
            failed += 1
    print("selftest: %d/%d passed" % (len(cases) - failed, len(cases)))
    return 0 if failed == 0 else 1


def main() -> int:
    ap = argparse.ArgumentParser(description="railhmi render path decision")
    ap.add_argument("--gl", help="path=gl measurement JSON")
    ap.add_argument("--cpu", help="path=cpu measurement JSON")
    ap.add_argument("--min-fps", type=float, default=20.0)
    ap.add_argument("--max-spread", type=float, default=12.0)
    ap.add_argument("--out", help="write decision JSON")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()

    if args.selftest:
        return selftest()

    def load(p):
        if not p:
            return None
        try:
            with open(p, "r", encoding="utf-8") as fh:
                return json.load(fh)
        except Exception as e:  # noqa: BLE001
            print("cannot read %s: %r" % (p, e), file=sys.stderr)
            return None

    result = decide(load(args.gl), load(args.cpu), args.min_fps, args.max_spread)
    text = json.dumps(result, ensure_ascii=False, indent=2)
    print(text)
    if args.out:
        with open(args.out, "w", encoding="utf-8") as fh:
            fh.write(text)
    if result["status"] == "UNMEASURED":
        return 2
    if result["status"] == "NONE_USABLE":
        return 3
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
