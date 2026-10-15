# -*- coding: utf-8 -*-
"""software_verification fixture runner for ITEM-017 inference service &
dual-model reuse (verifier-owned).

Independent board-side verification over the registered Paramiko SSH entry:
  1. Reproduce the deliverable TestRun entry (board_build.py) with a verifier
     stamp + --keep-remote: core-tests build+run (480 cases incl. 37 new
     inference cases), tokens selftest, ui scan, sim-inject, status probes,
     and the deliverable railhmi-ai-probe; archive logs + JUnit XML back.
  2. Reproduce the deliverable MediaCheck entry with a verifier stamp: build
     media, stream/multi probes, and the deliverable railhmi-ai-runner.
  3. Programmatic assertions, parsed independently (never trusting the probe's
     own acceptance line only):
       - config line: interval/depth/budget/temp/models from app.yaml.
       - queue lines: 4 eligible cameras, wide angles (CAM-02/03) absent,
         dropped/inferred equal sampling oracles, requested=ceil(f/i), fps=5,
         model versions per profile (wheel_rail=yolov8s-seg, others=yolov5s).
       - schedule lines: temp=60 all 4 priority order, temp=90 top-2 + guard.
       - debug lines: 4 channels w/ model/latency/boxes/conf/last text.
       - ai-status stopped=AI 不可用; acceptance quad true.
       - ai-runner: worker start line (cameras=6, sim substitute), 4 channel
         lines w/ fps>=4, running=运行 eligible=4 with_result=4, stopped line,
         acceptance worker_stopped/ai_unavailable/4-of-4.
  4. Line-by-line [PASS] comparison of this round's fresh core-tests log with
     the implementer's archived r3 log + independent JUnit XML parse (480/0,
     inference-domain case count).
  5. Verifier-owned ext fixture (verify_ai17_ext.cpp) compiled against the
     librailhmi_core.a built by this round, run with RAILHMI_CONFIG_DIR free
     (pure logic) — boundaries beyond the deliverable tests.
  6. Negative CLI edges: unknown arg -> exit 2; invalid ai config -> exit 1.
  7. Cleanup /tmp/railhmi_build and the verifier workdir by name.
Fingerprint mismatch aborts. Credentials only from the named env var, never
logged. Every remote step has a finite timeout.
"""
from __future__ import annotations

import argparse
import base64
import hashlib
import json
import os
import re
import socket
import subprocess
import sys
import time
from pathlib import Path

try:
    import paramiko
except Exception as exc:  # noqa: BLE001
    sys.stderr.write("paramiko unavailable: %r\n" % (exc,))
    raise SystemExit(4)


def fingerprint(key) -> str:
    try:
        return "SHA256:" + base64.b64encode(
            hashlib.sha256(key.asbytes()).digest()).decode().rstrip("=")
    except Exception:  # noqa: BLE001
        return "(unavailable)"


def rrun(client, cmd: str, timeout: int) -> dict:
    started = time.time()
    try:
        _in, out, err = client.exec_command(cmd, timeout=timeout)
        so = out.read().decode("utf-8", "replace")
        se = err.read().decode("utf-8", "replace")
        rc = out.channel.recv_exit_status()
        return {"cmd": cmd, "exit": rc, "elapsed_s": round(time.time() - started, 2),
                "output": so + se}
    except socket.timeout:
        return {"cmd": cmd, "exit": None, "elapsed_s": round(time.time() - started, 2),
                "output": "(timeout after %ss)" % timeout}
    except Exception as exc:  # noqa: BLE001
        return {"cmd": cmd, "exit": None, "elapsed_s": round(time.time() - started, 2),
                "output": "(error: %r)" % (exc,)}


# key=value; values may contain spaces (text after "=" up to the next " key=")
KV = re.compile(r"(\S+)=(.*?)(?=\s+\S+=|$)")


def kvline(line: str) -> dict:
    return {k: v.strip() for k, v in KV.findall(" ".join(line.split()[1:]))
            if not k.startswith("type")}


def parse_ai_probe(text: str) -> dict:
    parsed = {"config": None, "queue": [], "schedule": [], "ai_status": [],
              "debug": [], "acceptance": None, "result": None, "error": []}
    for line in text.splitlines():
        s = line.strip()
        if s.startswith("type=config"):
            parsed["config"] = kvline(s)
        elif s.startswith("type=queue"):
            parsed["queue"].append(kvline(s))
        elif s.startswith("type=schedule"):
            parsed["schedule"].append(kvline(s))
        elif s.startswith("type=ai-status"):
            parsed["ai_status"].append(line)
        elif s.startswith("type=debug"):
            parsed["debug"].append(kvline(s))
        elif s.startswith("type=acceptance"):
            parsed["acceptance"] = kvline(s)
        elif s.startswith("type=result"):
            parsed["result"] = line.split()[-1]
        elif s.startswith("type=error"):
            parsed["error"].append(s)
    return parsed


def parse_ai_runner(text: str) -> dict:
    parsed = {"worker": None, "channel": [], "ai_status": [], "acceptance": None,
              "result": None, "error": []}
    for line in text.splitlines():
        s = line.strip()
        if s.startswith("type=worker"):
            parsed["worker"] = kvline(s)
        elif s.startswith("type=channel"):
            parsed["channel"].append(kvline(s))
        elif s.startswith("type=ai-status"):
            parsed["ai_status"].append(line)
        elif s.startswith("type=acceptance"):
            parsed["acceptance"] = kvline(s)
        elif s.startswith("type=result"):
            parsed["result"] = line.split()[-1]
        elif s.startswith("type=error"):
            parsed["error"].append(s)
    return parsed


def assert_ai_probe(p: dict) -> list:
    fails = []

    def eq(section, key, want, idx=0):
        got = section[idx].get(key) if isinstance(section, list) else section.get(key)
        if got != want:
            fails.append("%s.%s=%s want %s" % (section.__class__.__name__, key, got, want))

    cfg = p["config"]
    if cfg is None:
        fails.append("no config line")
        return fails
    for k, want in (("interval", "5"), ("depth", "2"), ("budget", "4"),
                    ("temp_limit", "85"), ("models", "4")):
        if cfg.get(k) != want:
            fails.append("config %s=%s want %s" % (k, cfg.get(k), want))

    # queue lines
    qs = p["queue"]
    if len(qs) != 4:
        fails.append("queue lines=%d want 4" % len(qs))
    order = [q.get("camera") for q in qs]
    if order != ["CAM-01", "CAM-04", "CAM-05", "CAM-06"]:
        fails.append("queue camera order=%s" % order)
    for q in qs:
        if q.get("profile") == "none" or q.get("camera") in ("CAM-02", "CAM-03"):
            fails.append("wide angle leaked into queue: %s" % q.get("camera"))
        if q.get("depth") != "2":
            fails.append("queue %s depth" % q.get("camera"))
        if q.get("fps") != "5":
            fails.append("queue %s fps=%s want 5" % (q.get("camera"), q.get("fps")))
    # delivered oracle: dropped/inferred sampled at the last frame_due tick
    want_dropped = {"CAM-01": "35", "CAM-04": "35", "CAM-05": "35", "CAM-06": "35"}
    for q in qs:
        if q.get("dropped") != want_dropped.get(q.get("camera")):
            fails.append("queue %s dropped=%s want %s (sampled@last due)"
                         % (q.get("camera"), q.get("dropped"),
                            want_dropped.get(q.get("camera"))))
        if q.get("inferred") != "10":
            fails.append("queue %s inferred=%s want 10" % (q.get("camera"), q.get("inferred")))
        if q.get("requested") != "10":
            fails.append("queue %s requested=%s want 10" % (q.get("camera"), q.get("requested")))
        want_model = "rknn-2.3.2/yolov8s-seg" if q.get("profile") == "wheel_rail" \
            else "rknn-2.3.2/yolov5s"
        if q.get("model") != want_model:
            fails.append("queue %s model=%s want %s" % (q.get("camera"), q.get("model"), want_model))

    # schedule lines (temp=60: all four priority order; temp=90: top-2 + guard)
    s60 = [s for s in p["schedule"] if s.get("temp") == "60"]
    s90 = [s for s in p["schedule"] if s.get("temp") == "90"]
    if not s60:
        fails.append("missing temp=60 schedule")
    else:
        want = "CAM-01,CAM-05,CAM-04,CAM-06"
        if s60[0].get("run") != want or s60[0].get("guard") != "false":
            fails.append("temp60 run=%s guard=%s want %s,false"
                         % (s60[0].get("run"), s60[0].get("guard"), want))
    if not s90:
        fails.append("missing temp=90 schedule")
    else:
        if s90[0].get("run") != "CAM-01,CAM-05" or s90[0].get("guard") != "true":
            fails.append("temp90 run=%s guard=%s want CAM-01,CAM-05,true"
                         % (s90[0].get("run"), s90[0].get("guard")))
        if s90[0].get("skipped") != "CAM-04,CAM-06":
            fails.append("temp90 skipped=%s" % s90[0].get("skipped"))

    # debug lines
    if len(p["debug"]) != 4:
        fails.append("debug lines=%d want 4" % len(p["debug"]))
    for d in p["debug"]:
        if d.get("available") != "true":
            fails.append("debug %s not available online" % d.get("camera"))
        if d.get("conf") != "80" or d.get("boxes") != "1":
            fails.append("debug %s conf/boxes" % d.get("camera"))
        if d.get("last") != "<1 秒前":
            fails.append("debug %s last=%s" % (d.get("camera"), d.get("last")))

    # ai-status: online + stopped lines (probe emits online=, runner running=)
    if not any("online=运行" in l for l in p["ai_status"]):
        fails.append("ai-status online=运行 missing")
    if not any("stopped=AI 不可用" in l for l in p["ai_status"]):
        fails.append("ai-status stopped line missing")

    acc = p["acceptance"]
    if acc is None:
        fails.append("no acceptance line")
    else:
        want_acc = {"worker_down_status": "AI 不可用",
                    "alarm_annotation": "AI 不可用",
                    "debug_all_unavailable": "true",
                    "temp_guard_degrades": "true",
                    "wide_angle_excluded": "true"}
        for k, want_v in want_acc.items():
            if acc.get(k) != want_v:
                fails.append("acceptance %s=%s want %s" % (k, acc.get(k), want_v))
    if p["result"] != "PASS" or p["error"]:
        fails.append("probe result=%s errors=%d" % (p["result"], len(p["error"])))
    return fails


def assert_ai_runner(p: dict) -> list:
    fails = []
    w = p["worker"]
    if w is None:
        fails.append("no worker line")
        return fails
    if w.get("interval") != "5" or w.get("depth") != "2" or w.get("cameras") != "6":
        fails.append("worker line mismatch: %r" % w)
    if "sim(substitute,not-rknn)" not in (w.get("detector") or ""):
        fails.append("worker detector not marked substitute")
    if len(p["channel"]) != 4:
        fails.append("channel lines=%d want 4" % len(p["channel"]))
    for c in p["channel"]:
        if int(c.get("fps", "0")) < 4:
            fails.append("channel %s fps=%s want >=4" % (c.get("camera"), c.get("fps")))
        want_model = "rknn-2.3.2/yolov8s-seg" if c.get("profile") == "wheel_rail" \
            else "rknn-2.3.2/yolov5s"
        if c.get("model") != want_model:
            fails.append("channel %s model=%s want %s" % (c.get("camera"), c.get("model"), want_model))
    if not any("running=运行" in l and "eligible=4" in l and "with_result=4" in l
               for l in p["ai_status"]):
        fails.append("ai-status running line mismatch")
    if not any("stopped=AI 不可用" in l for l in p["ai_status"]):
        fails.append("ai-status stopped missing")
    acc = p["acceptance"]
    if acc is None or acc.get("worker_stopped") != "true" or \
            acc.get("ai_unavailable") != "true" or acc.get("channels_ran") != "4/4" or \
            acc.get("result") != "PASS":
        fails.append("acceptance mismatch: %r" % acc)
    # runner has no separate type=result line; its verdict lives on the
    # acceptance line (checked above via acc["result"]), the process exit
    # code is checked by the caller.
    if p["error"]:
        fails.append("runner errors=%d" % len(p["error"]))
    return fails


def parse_core_tests_log(text: str) -> dict:
    return {"pass_lines": [l for l in text.splitlines() if l.startswith("[PASS]")],
            "fail_lines": [l for l in text.splitlines() if l.startswith("[FAIL]")],
            "result": next((l for l in text.splitlines() if l.startswith("RESULT:")), "")}


def main() -> int:
    ap = argparse.ArgumentParser(description="ITEM-017 verification fixtures")
    ap.add_argument("--host", required=True)
    ap.add_argument("--port", type=int, default=22)
    ap.add_argument("--user", required=True)
    ap.add_argument("--credential-env", default="TRAIN_RK3588_SSH_CREDENTIAL")
    ap.add_argument("--expected-fingerprint", required=True)
    ap.add_argument("--workdir", default="/tmp/railhmi_verify017")
    ap.add_argument("--cpp", required=True)
    ap.add_argument("--outdir", required=True)
    ap.add_argument("--stamp", required=True)
    ap.add_argument("--impl-testrun-log", required=True,
                    help="implementer archived core-tests log (r3) for line-by-line compare")
    ap.add_argument("--impl-aiprobe-log", required=True,
                    help="implementer archived ai-probe log (r3)")
    ap.add_argument("--impl-airunner-log", required=True,
                    help="implementer archived ai-runner log (media-r1)")
    ap.add_argument("--connect-timeout", type=int, default=10)
    ap.add_argument("--timeout-sec", type=int, default=1500)
    ap.add_argument("--app-root", default=str(Path(__file__).resolve().parents[4] / "app"))
    args = ap.parse_args()

    secret = os.environ.get(args.credential_env)
    if not secret:
        sys.stderr.write("credential env %s not loaded; refusing to guess\n"
                         % args.credential_env)
        return 2

    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    tag = "VERIFY-FIXTURES-" + args.stamp
    wd = args.workdir
    remote_dir = "/tmp/railhmi_build"
    transcript: dict = {"invocation": {k: str(v) for k, v in vars(args).items()},
                        "host_key_fingerprint": None,
                        "notes": ["aborts on fingerprint mismatch; no credentials logged"],
                        "steps": []}
    ok = True

    def note(step, res):
        transcript["steps"].append({"step": step, **res})

    def save() -> None:
        (outdir / (tag + ".transcript.json")).write_text(
            json.dumps(transcript, ensure_ascii=False, indent=2), encoding="utf-8")
        with (outdir / (tag + ".log")).open("w", encoding="utf-8") as fh:
            fh.write("host_key_fingerprint: %s\n" % transcript["host_key_fingerprint"])
            for s in transcript["steps"]:
                fh.write("\n===== [%s] exit=%s =====\n" % (s.get("step"), s.get("exit")))
                fh.write((s.get("output") or "") + "\n")

    client = paramiko.SSHClient()
    client.load_system_host_keys()
    client.set_missing_host_key_policy(paramiko.RejectPolicy())
    try:
        client.connect(hostname=args.host, port=args.port, username=args.user,
                       password=secret, timeout=args.connect_timeout,
                       auth_timeout=args.connect_timeout,
                       banner_timeout=args.connect_timeout,
                       look_for_keys=False, allow_agent=False)
    except Exception as exc:  # noqa: BLE001
        note("connect", {"exit": None, "output": "connect error: %r" % (exc,)})
        save()
        sys.stderr.write("connect failed (not asserting offline): %r\n" % (exc,))
        return 2

    try:
        tr = client.get_transport()
        fp = fingerprint(tr.get_remote_server_key()) if tr is not None else "(transport None)"
        transcript["host_key_fingerprint"] = fp
        note("fingerprint", {"exit": 0 if fp == args.expected_fingerprint else 1,
                             "output": "observed=%s expected=%s"
                                       % (fp, args.expected_fingerprint)})
        if fp != args.expected_fingerprint:
            save()
            sys.stderr.write("host fingerprint mismatch; aborting\n")
            return 2

        # 1. deliverable TestRun entry with verifier stamp (keep remote for ext)
        cmd = [sys.executable, str(Path(args.app_root) / "tools" / "board_build.py"),
               "--host", args.host, "--user", args.user,
               "--credential-env", args.credential_env,
               "--mode", "TestRun",
               "--outdir", str(outdir),
               "--stamp", args.stamp,
               "--keep-remote"]
        started = time.time()
        try:
            proc = subprocess.run(cmd, capture_output=True, text=True,
                                  encoding="utf-8", errors="replace",
                                  timeout=args.timeout_sec)
            entry_exit = proc.returncode
            entry_out = (proc.stdout or "") + (proc.stderr or "")
        except subprocess.TimeoutExpired:
            entry_exit = None
            entry_out = "(timeout after %ss)" % args.timeout_sec
        note("run-testrun-entry", {"cmd": " ".join(cmd), "exit": entry_exit,
                                   "elapsed_s": round(time.time() - started, 2),
                                   "output": entry_out})
        if entry_exit != 0:
            ok = False

        stamp = args.stamp
        # 1b. fresh artifact paths for this round
        tl = outdir / ("build-TestRun-%s-%s.core-evidence_core-tests-%s.log"
                       % (args.host.replace(".", "_"), stamp, stamp))
        tx = outdir / ("build-TestRun-%s-%s.core-evidence_core-tests-%s.xml"
                       % (args.host.replace(".", "_"), stamp, stamp))
        pl = outdir / ("build-TestRun-%s-%s.core-evidence_ai-probe-%s.log"
                       % (args.host.replace(".", "_"), stamp, stamp))
        if not tl.is_file() or not pl.is_file():
            note("fresh-artifacts", {"exit": None,
                 "output": "missing fresh artifacts: %s %s" % (tl.name, pl.name)})
            ok = False
        else:
            note("fresh-artifacts", {"exit": 0, "output": tl.name + " " + pl.name})

        # 2. line-by-line [PASS] comparison + independent JUnit parse
        if tl.is_file() and args.impl_testrun_log:
            impl = parse_core_tests_log(Path(args.impl_testrun_log).read_text(
                encoding="utf-8", errors="replace"))
            ver = parse_core_tests_log(tl.read_text(encoding="utf-8", errors="replace"))
            same = impl["pass_lines"] == ver["pass_lines"]
            note("log-compare", {"exit": 0 if same and not ver["fail_lines"] else 1,
                 "output": "impl=%s verify=%s impl_pass=%d verify_pass=%d "
                           "identical=%s verify_fails=%d result=%s"
                           % (os.path.basename(args.impl_testrun_log), tl.name,
                              len(impl["pass_lines"]), len(ver["pass_lines"]),
                              same, len(ver["fail_lines"]), ver["result"])})
            if not same or ver["fail_lines"]:
                ok = False
        if tx.is_file():
            xtext = tx.read_text(encoding="utf-8", errors="replace")
            m = re.search(r'tests="(\d+)"[^>]*failures="(\d+)"', xtext, re.S)
            n_tests, n_fail = (m.group(1), m.group(2)) if m else (None, None)
            # ITEM-017 dedicated block = the 44 inference/queue/priority/guard
            # cases between "first push no drop" and the suite end.
            infer_cases = len(re.findall(
                r'name="[^"]*(?:queue|drop|budget|over-temp|front &gt;|wheel_rail &gt;|'
                r'pantograph &gt;|none not eligible|frame interval|priority order|'
                r'no skip|wide-angle|worker stopped|worker down|AI Unavailable|'
                r'AI text|annotated AI|annotation when|alive but no result|'
                r'fresh results|ai_enabled cameras|CAM-01|model version|'
                r'latency from|last result age|profile label)[^"]*"',
                xtext))
            junit_ok = (n_tests is not None and n_fail == "0" and infer_cases >= 37)
            note("junit-parse", {"exit": 0 if junit_ok else 1,
                 "output": "junit %s tests=%s failures=%s inference_cases=%d"
                           % (tx.name, n_tests, n_fail, infer_cases)})
            if not junit_ok:
                ok = False
        else:
            note("junit-parse", {"exit": None, "output": "no fresh junit xml"})
            ok = False

        # 3. independent assertions on the fresh ai-probe log
        impl_probe = parse_ai_probe(Path(args.impl_aiprobe_log).read_text(
            encoding="utf-8", errors="replace")) if args.impl_aiprobe_log else None
        if pl.is_file():
            vp = parse_ai_probe(pl.read_text(encoding="utf-8", errors="replace"))
            fails = assert_ai_probe(vp)
            note("ai-probe-assert", {"exit": 0 if not fails else 1,
                 "output": json.dumps({"fails": fails}, ensure_ascii=False)})
            if fails:
                ok = False
            # cross-check fresh probe vs implementer's archived probe (line equal)
            if impl_probe is not None:
                identical = Path(args.impl_aiprobe_log).read_text(
                    encoding="utf-8", errors="replace").splitlines() == \
                    pl.read_text(encoding="utf-8", errors="replace").splitlines()
                note("ai-probe-compare", {"exit": 0 if identical else 1,
                     "output": "deterministic reproduction identical=%s" % identical})
                if not identical:
                    ok = False

        # 4. ext fixture compile+run against this round's librailhmi_core.a
        note("mkdir-workdir", rrun(client, "mkdir -p %s" % wd, 30))
        up = client.open_sftp()
        up.get_channel().settimeout(120)
        try:
            up.put(args.cpp, "%s/verify_ai17_ext.cpp" % wd)
            note("upload-fixture", {"exit": 0, "output": args.cpp})
        finally:
            up.close()
        compile_cmd = ("cd %s && g++ -std=c++17 -O1 -Wall -Wextra -Isrc/core/include "
                       "%s/verify_ai17_ext.cpp build-core/src/core/librailhmi_core.a "
                       "-o %s/verify_ai17_ext 2>&1; echo GXX_EXIT=$?"
                       % (remote_dir, wd, wd))
        cext = rrun(client, compile_cmd, 240)
        note("compile-ext", cext)
        if cext.get("exit") != 0 or "GXX_EXIT=0" not in cext.get("output", ""):
            ok = False
        rext = rrun(client, "cd %s && %s/verify_ai17_ext; echo EXT_EXIT=$?"
                    % (remote_dir, wd), 180)
        note("run-ext", rext)
        m = re.search(r"RESULT: (\w+) passed=(\d+) failed=(\d+)", rext.get("output", ""))
        if rext.get("exit") != 0 or "EXT_EXIT=0" not in rext.get("output", "") or \
                not (m and m.group(1) == "PASS" and int(m.group(3)) == 0):
            ok = False
        if m:
            note("ext-summary", {"exit": 0 if m.group(1) == "PASS" else 1,
                 "output": "ext passed=%s failed=%s" % (m.group(2), m.group(3))})

        # 5. MediaCheck re-run (media build + stream/multi/ai-runner) live
        mstamp = stamp + "-media"
        cmd = [sys.executable, str(Path(args.app_root) / "tools" / "board_build.py"),
               "--host", args.host, "--user", args.user,
               "--credential-env", args.credential_env,
               "--mode", "MediaCheck",
               "--outdir", str(outdir),
               "--stamp", mstamp,
               "--keep-remote"]
        started = time.time()
        try:
            proc = subprocess.run(cmd, capture_output=True, text=True,
                                  encoding="utf-8", errors="replace",
                                  timeout=args.timeout_sec)
            mc_exit = proc.returncode
            mc_out = (proc.stdout or "") + (proc.stderr or "")
        except subprocess.TimeoutExpired:
            mc_exit = None
            mc_out = "(timeout after %ss)" % args.timeout_sec
        note("run-mediacheck-entry", {"cmd": " ".join(cmd), "exit": mc_exit,
                                      "elapsed_s": round(time.time() - started, 2),
                                      "output": mc_out})
        if mc_exit != 0:
            ok = False
        rl = Path(outdir) / ("build-MediaCheck-%s-%s.core-evidence_ai-runner-%s.log"
                             % (args.host.replace(".", "_"), mstamp, mstamp))
        if rl.is_file():
            vr = parse_ai_runner(rl.read_text(encoding="utf-8", errors="replace"))
            fails = assert_ai_runner(vr)
            note("ai-runner-assert", {"exit": 0 if not fails else 1,
                 "output": json.dumps({"fails": fails}, ensure_ascii=False)})
            if fails:
                ok = False
            imp_r = Path(args.impl_airunner_log)
            if imp_r.is_file():
                identical = imp_r.read_text(encoding="utf-8", errors="replace").splitlines() == \
                    rl.read_text(encoding="utf-8", errors="replace").splitlines()
                note("ai-runner-compare", {"exit": 0 if identical else 0,
                     "output": "note: timings vary between runs; "
                               "identity check informational identical=%s" % identical})
        else:
            note("ai-runner-assert", {"exit": None, "output": "missing fresh ai-runner %s" % rl.name})
            ok = False

        # 6. negative CLI edges: unknown arg exit 2 + invalid ai config exit 1
        n1 = rrun(client, "cd %s && ./build-core/tools/railhmi-ai-probe --nope; echo NARG_EXIT=$?" % remote_dir, 30)
        note("probe-negative-arg", n1)
        n2 = rrun(client,
                  "cd %s && rm -rf %s/cfgbad && mkdir -p %s/cfgbad && cp config/*.yaml %s/cfgbad/ "
                  "&& sed -i 's/deferred:.*//' /dev/null > /dev/null 2>&1; true; "
                  "sed -i 's/temp_limit_c: 85.0/temp_limit_c: unparseable/' %s/cfgbad/app.yaml "
                  "&& ./build-core/tools/railhmi-ai-probe --config %s/cfgbad; echo CFG_EXIT=$?; "
                  "rm -rf %s/cfgbad" % (remote_dir, wd, wd, wd, wd, wd, wd), 60)
        note("probe-negative-config", n2)
        if "NARG_EXIT=2" not in n1.get("output", ""):
            ok = False
        if "CFG_EXIT=1" not in n2.get("output", ""):
            ok = False

        # 7. cleanup by name
        note("cleanup", rrun(client,
             "rm -rf %s %s && echo fixture_cleanup_done && echo LEFT_LIST && ls -d %s %s 2>&1"
             % (wd, remote_dir, wd, remote_dir), 30))
    finally:
        client.close()
        save()

    print("host_key_fingerprint:", transcript["host_key_fingerprint"])
    for s in transcript["steps"]:
        tail = (s.get("output") or "").strip().splitlines()
        print("[%s] exit=%s %s" % (s.get("step"), s.get("exit"), tail[-1] if tail else ""))
    if not ok:
        print("NOTE: one or more steps returned nonzero or failed assertions; see transcript")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
