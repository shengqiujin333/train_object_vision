# -*- coding: utf-8 -*-
"""ITEM-018 verification round — independent board-side re-run (verifier-owned).

Registered access: access.rk3588_inspect via Paramiko SSH (environment.yaml).
Credentials from the named User-env var only; never logged; fingerprint
checked against the user-verified SHA256 before any work.

Steps (all finite timeouts):
  1. Re-run the deliverable board_build.py --mode TestRun entry as child
     process with verifier stamp + --keep-remote (core-tests incl. new
     alarm-center cases, tokens selftest, ui scan, sim-inject, status/ai/
     alarm probes) and archive fresh logs + JUnit XML.
  2. Line-by-line [PASS] comparison: fresh core-tests log vs implementer
     archived ITEM018-r1 log + independent JUnit parse (512/0) with
     alarm-domain case enumeration.
  3. Independent assertions on the fresh alarm-probe log (never trusting only
     the probe's own acceptance line): 4 rows, default sort L1-first +
     time-desc within L1, time strings, camera id+name, record three-state
     with inline annotation, filter counts (level/unacked/window/combo-empty
     empty state), batch ack changed=3 with post-ack labels, events preserved
     (all 4 rows still present), maint log action/target/detail, options
     counts; cross-round line-identity vs implementer's archived probe log.
  4. Verifier ext fixture (verify_ai18_ext.cpp) compiled against THIS round's
     build-core/src/core/librailhmi_core.a, pure logic: half-open window,
     day_start negative floor, parse_date edges, stable-sort ties, duplicate
     id ack + revert to unacked, unknown type fallback, level>3 clamp,
     em-dash time text, filter combos, options dedup, query non-mutation,
     empty/multi-id ack log.
  5. Negative CLI edge: unknown arg -> exit 2; also probe rerun determinism.
  6. Fault-injection regression from the same TestRun archive present
     (sim-inject, status-probe files).
  7. Cleanup /tmp/railhmi_build and verifier workdir by name.
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


# key=value tokens; greedy value keeps '=' inside the last token so that
# detail=count=3 parses as detail="count=3" (learned in ITEM-017: values may
# contain '='; simple (\S+)=(\S+) splits them).
KV_PAIR = re.compile(r"(\S+?)=(\S+)(?:\s+(.*))?$")


def kv(s: str) -> dict:
    out = {}
    text = s
    while text:
        m = KV_PAIR.match(text)
        if not m:
            break
        key, val, rest = m.group(1), m.group(2), m.group(3)
        out[key] = val
        if rest is None:
            break
        text = rest
    return out


# row id=1 level=L1 type=... time=<date time> cam=ID(name) ack=.. record=.. snapshot=.. desc=..
ROW_RE = re.compile(r"row id=(\d+) level=(L\d) type=(\S+) time=(\S+ \S+) "
                    r"cam=(\S+)\((\S+)\) ack=(\S+) record=(\S+) snapshot=(\S+) desc=(.*)")


def parse_alarm_probe(text: str) -> dict:
    p = {"rows": [], "post_ack": [], "filters": [], "ack": None, "maint": None,
         "options": None, "acceptance": None}
    for line in text.splitlines():
        s = line.strip()
        m = ROW_RE.match(s)
        if m:
            p["rows"].append(list(m.groups()))
            continue
        if s.startswith("row id=") and "ack_label=" in s:
            p["post_ack"].append(kv(" ".join(s.split()[1:])))
        elif s.startswith("type=filter"):
            p["filters"].append(kv(s))
        elif s.startswith("type=ack"):
            p["ack"] = kv(s)
        elif s.startswith("type=maint-log"):
            p["maint"] = kv(s)
        elif s.startswith("type=options"):
            p["options"] = kv(s)
        elif s.startswith("type=acceptance"):
            p["acceptance"] = kv(s)
    return p


def assert_alarm_probe(p: dict) -> list:
    fails = []
    rows = p["rows"]
    if len(rows) != 4:
        fails.append("probe rows=%d want 4" % len(rows))
    order = [r[0] for r in rows]
    if order != ["1", "4", "3", "2"]:
        fails.append("default sort=%s want [1,4,3,2]" % order)
    levels = {r[0]: r[1] for r in rows}
    if levels != {"1": "L1", "4": "L1", "3": "L2", "2": "L3"}:
        fails.append("levels=%s" % levels)
    times = {r[0]: r[3] for r in rows}
    if times != {"1": "2026-10-16 00:00:05", "4": "2026-10-16 00:00:01",
                 "3": "2026-10-16 00:00:09", "2": "2026-10-16 00:00:03"}:
        fails.append("time map=%s" % times)
    l1 = [times["1"], times["4"]]
    if l1 != sorted(l1, reverse=True):
        fails.append("L1 not time-desc: %s" % l1)
    cams = {r[0]: (r[4], r[5]) for r in rows}
    if cams != {"1": ("CAM-01", "前方"), "4": ("CAM-05", "车底"),
                "3": ("CAM-04", "车顶"), "2": ("CAM-02", "左侧")}:
        fails.append("cam map=%s" % cams)
    recs = {r[0]: r[7] for r in rows}
    if recs != {"1": "available", "4": "available", "3": "incomplete", "2": "unavailable"}:
        fails.append("record map=%s" % recs)
    descs = {r[0]: r[9] for r in rows}
    if "录像不可用" not in descs.get("2", ""):
        fails.append("inline 录像不可用 annotation missing on row 2")
    if "录像不完整" not in descs.get("3", ""):
        fails.append("inline 录像不完整 annotation missing on row 3")
    f = {}
    for item in p["filters"]:
        f[item.get("name")] = item
    if not (f.get("level_L1", {}).get("count") == "2"):
        fails.append("filter level_L1 count")
    if not (f.get("unacked", {}).get("count") == "3"):
        fails.append("filter unacked count")
    if not (f.get("window", {}).get("count") == "2"):
        fails.append("filter window count")
    if not (f.get("combo_empty", {}).get("count") == "0"
            and f.get("combo_empty", {}).get("empty") == "true"):
        fails.append("combo empty state")
    if p["ack"] is None or p["ack"].get("changed") != "3":
        fails.append("batch ack changed")
    if len(p["post_ack"]) != 4:
        fails.append("post-ack rows=%d want 4 (events preserved)" % len(p["post_ack"]))
    for r in p["post_ack"]:
        if r.get("ack") != "acked" or r.get("ack_label") != "已确认":
            fails.append("post-ack row state: %r" % r)
    if p["maint"] is None or p["maint"].get("action") != "ack_alarm" \
            or p["maint"].get("target") != "1,2,3" \
            or p["maint"].get("detail") != "count=3":
        fails.append("maint log")
    if p["options"] is None or p["options"].get("cameras") != "4" \
            or p["options"].get("types") != "4" or p["options"].get("levels") != "3":
        fails.append("options counts")
    acc = p["acceptance"]
    if acc is None or acc.get("batch_ack_changed") != "3" \
            or acc.get("events_preserved") != "true" \
            or acc.get("empty_state_ok") != "true" \
            or acc.get("result") != "PASS":
        fails.append("acceptance line")
    return fails


def parse_core_tests_log(text: str) -> dict:
    return {"pass": [l.strip() for l in text.splitlines() if l.startswith("[PASS]")],
            "fail": [l.strip() for l in text.splitlines() if l.startswith("[FAIL]")],
            "result": next((l for l in text.splitlines() if l.startswith("RESULT:")), "")}


def main() -> int:
    ap = argparse.ArgumentParser(description="ITEM-018 verification fixtures")
    ap.add_argument("--host", required=True)
    ap.add_argument("--port", type=int, default=22)
    ap.add_argument("--user", required=True)
    ap.add_argument("--credential-env", default="TRAIN_RK3588_SSH_CREDENTIAL")
    ap.add_argument("--expected-fingerprint", required=True)
    ap.add_argument("--workdir", default="/tmp/railhmi_verify018")
    ap.add_argument("--cpp", required=True)
    ap.add_argument("--outdir", required=True)
    ap.add_argument("--stamp", required=True)
    ap.add_argument("--impl-testrun-log", required=True)
    ap.add_argument("--impl-alarmprobe-log", required=True)
    ap.add_argument("--connect-timeout", type=int, default=10)
    ap.add_argument("--timeout-sec", type=int, default=1200)
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
                        "notes": ["aborts logic on fingerprint mismatch; no credentials logged"],
                        "steps": []}
    ok = True

    def note(step, res):
        transcript["steps"].append({"step": step, **res})

    def save():
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
        stamp = args.stamp
        cmd = [sys.executable, str(Path(args.app_root) / "tools" / "board_build.py"),
               "--host", args.host, "--user", args.user,
               "--credential-env", args.credential_env,
               "--mode", "TestRun",
               "--outdir", str(outdir),
               "--stamp", stamp,
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

        # 1b. fresh artifact paths for this round
        host_u = args.host.replace(".", "_")
        tl = outdir / ("build-TestRun-%s-%s.core-evidence_core-tests-%s.log"
                       % (host_u, stamp, stamp))
        tx = outdir / ("build-TestRun-%s-%s.core-evidence_core-tests-%s.xml"
                       % (host_u, stamp, stamp))
        pl = outdir / ("build-TestRun-%s-%s.core-evidence_alarm-probe-%s.log"
                       % (host_u, stamp, stamp))
        if not (tl.is_file() and tx.is_file() and pl.is_file()):
            note("fresh-artifacts", {"exit": None,
                 "output": "missing fresh artifacts: %s %s %s"
                           % (tl.name, tx.name, pl.name)})
            ok = False
        else:
            note("fresh-artifacts", {"exit": 0,
                 "output": tl.name + " " + tx.name + " " + pl.name})

        # 2. line-by-line [PASS] comparison + independent JUnit parse
        if tl.is_file() and args.impl_testrun_log:
            impl = parse_core_tests_log(Path(args.impl_testrun_log).read_text(
                encoding="utf-8", errors="replace"))
            ver = parse_core_tests_log(tl.read_text(encoding="utf-8", errors="replace"))
            same = impl["pass"] == ver["pass"]
            note("log-compare", {"exit": 0 if same and not ver["fail"] else 1,
                 "output": "impl=%s verify=%s impl_pass=%d verify_pass=%d "
                           "identical=%s verify_fails=%d result=%s"
                           % (os.path.basename(args.impl_testrun_log), tl.name,
                              len(impl["pass"]), len(ver["pass"]),
                              same, len(ver["fail"]), ver["result"])})
            if not same or ver["fail"]:
                ok = False
        if tx.is_file():
            xtext = tx.read_text(encoding="utf-8", errors="replace")
            m = re.search(r'tests="(\d+)"[^>]*failures="(\d+)"', xtext, re.S)
            n_tests, n_fail = (m.group(1), m.group(2)) if m else (None, None)
            alarm_cases = [n2 for n2 in re.findall(r'name="([^"]+)"', xtext)
                           if re.search(r"alarm|record|sort|filter|ack|option|date|"
                                        r"day|snapshot|level", n2, re.I)]
            junit_ok = (n_tests is not None and n_fail == "0" and len(alarm_cases) >= 40)
            note("junit-parse", {"exit": 0 if junit_ok else 1,
                 "output": "junit %s tests=%s failures=%s alarm_cases=%d"
                           % (tx.name, n_tests, n_fail, len(alarm_cases))})
            if not junit_ok:
                ok = False
        else:
            note("junit-parse", {"exit": None, "output": "no fresh junit xml"})
            ok = False

        # 3. independent assertions on the fresh alarm-probe log
        if pl.is_file():
            vp = parse_alarm_probe(pl.read_text(encoding="utf-8", errors="replace"))
            fails = assert_alarm_probe(vp)
            note("alarm-probe-assert", {"exit": 0 if not fails else 1,
                 "output": json.dumps({"fails": fails}, ensure_ascii=False)})
            if fails:
                ok = False
            # cross-round identity: fresh probe vs implementer's archived probe
            impl_probe_text = Path(args.impl_alarmprobe_log).read_text(
                encoding="utf-8", errors="replace")
            identical = impl_probe_text.splitlines() == \
                pl.read_text(encoding="utf-8", errors="replace").splitlines()
            note("alarm-probe-compare", {"exit": 0 if identical else 1,
                 "output": "deterministic reproduction identical=%s" % identical})
            if not identical:
                ok = False
        else:
            note("alarm-probe-assert", {"exit": None, "output": "missing fresh alarm-probe"})
            ok = False

        # 4. ext fixture compile+run against THIS round's librailhmi_core.a
        if ext_run(client, args, wd, remote_dir, note) != 0:
            ok = False

        # 5. negative CLI edges on the deliverable probe
        n1 = rrun(client, "cd %s && ./build-core/tools/railhmi-alarm-probe --nope; "
                          "echo NARG_EXIT=$?" % remote_dir, 30)
        note("alarm-probe-negative-arg", n1)
        if "NARG_EXIT=2" not in n1.get("output", ""):
            ok = False
        # determinism second run of the probe (same stamp output compare)
        n2 = rrun(client, "cd %s && ./build-core/tools/railhmi-alarm-probe "
                          "--out verify/probe-rep.log >/dev/null 2>&1; "
                          "echo REPL_EXIT=$?" % remote_dir, 30)
        n2b = rrun(client, "cd %s && ./build-core/tools/railhmi-alarm-probe | "
                           "diff - %s/core-evidence/alarm-probe-%s.log && "
                           "echo IDENTICAL" % (remote_dir, remote_dir, stamp), 30)
        note("alarm-probe-determinism", n2b)
        if "IDENTICAL" not in n2b.get("output", ""):
            ok = False

        # 6. fault-injection regression artifacts from the same TestRun
        reg = rrun(client, "cd %s && ls core-evidence/ | grep -E "
                           "'^(sim-inject|status-probe|ai-probe)' | tr '\n' ' '" % remote_dir, 30)
        note("regression-artifacts", reg)
        reg_ok = (reg.get("exit") == 0
                  and "sim-inject" in reg.get("output", "")
                  and "status-probe" in reg.get("output", ""))
        if not reg_ok:
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
    return 0 if ok else 1


def ext_run(client, args, wd, remote_dir, note) -> int:
    res = rrun(client, "mkdir -p %s" % wd, 30)
    note("mkdir-workdir", res)
    sftp = client.open_sftp()
    sftp.get_channel().settimeout(120)
    try:
        sftp.put(args.cpp, "%s/verify_ai18_ext.cpp" % wd)
        note("upload-fixture", {"exit": 0, "output": args.cpp})
    finally:
        sftp.close()
    # test_util.h comes from the uploaded app tree (this round's source of truth)
    compile_cmd = ("cd %s && g++ -std=c++17 -O1 -Wall -Wextra "
                   "-Isrc/core/include -Itests/core "
                   "%s/verify_ai18_ext.cpp build-core/src/core/librailhmi_core.a "
                   "-o %s/verify_ai18_ext 2>&1; echo GXX_EXIT=$?"
                   % (remote_dir, wd, wd))
    cext = rrun(client, compile_cmd, 240)
    note("compile-ext", cext)
    if cext.get("exit") != 0 or "GXX_EXIT=0" not in cext.get("output", ""):
        return 1
    rext = rrun(client, "cd %s && %s/verify_ai18_ext; echo EXT_EXIT=$?"
                % (remote_dir, wd), 180)
    note("run-ext", rext)
    m = re.search(r"RESULT: (\w+) passed=(\d+) failed=(\d+)", rext.get("output", ""))
    if rext.get("exit") != 0 or "EXT_EXIT=0" not in rext.get("output", "") or \
            not (m and m.group(1) == "PASS" and int(m.group(3)) == 0):
        return 1
    note("ext-summary", {"exit": 0 if m and m.group(1) == "PASS" else 1,
         "output": "ext passed=%s failed=%s" % (m.group(2), m.group(3))})
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
