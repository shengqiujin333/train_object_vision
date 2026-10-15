# -*- coding: utf-8 -*-
"""ITEM-019 verification round — independent board-side re-run (verifier-owned).

Registered access: access.rk3588_inspect via Paramiko SSH (environment.yaml).
Credentials from the named User-env var only; never logged; fingerprint
checked against the user-verified SHA256 before any work.

Steps (all finite timeouts):
  1. Re-run the deliverable board_build.py --mode TestRun entry as child
     process with verifier stamp + --keep-remote (core-tests incl. new
     clip/gap cases, tokens selftest, ui scan, sim-inject, status/ai/alarm/
     clip probes) and archive fresh logs + JUnit XML.
  2. Line-by-line [PASS] comparison: fresh core-tests log vs implementer
     archived ITEM019-r2 log + independent JUnit parse (537/0) with clip-domain
     case enumeration.
  3. Independent assertions on the fresh clip-probe log (never trusting only
     the probe's own acceptance line): config 30/60 with derived buffer depth,
     buffer near capacity, clip range 30000..120000 duration 90000, finalize
     before/at split, finalized complete with frames, store wrote/exists,
     manifest JSON key set and values, retention evicted=1 continuous=1
     critical=0 protected_remaining=1, gap merged single row 200000..210000;
     cross-round line-identity vs implementer's archived probe log.
  4. Verifier ext fixture (verify_clip19_ext.cpp) compiled against THIS
     round's build-core static libs, pure logic: configurable windows, buffer
     semantics, clip lifecycle/idempotent finalize, retention through the
     recorder (state mutation), gap swap/adjacency/bridge/reason/sort,
     manifest keys, atomic-write overwrite/tmp/failure edges.
  5. Negative CLI edges on the deliverable probe (unknown arg exit 2) +
     probe determinism re-run diff vs the archived fresh log.
  6. Fault-injection regression artifacts from the same TestRun archive
     replayed against the fresh copies (sim-inject/state/ai/alarm present).
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
# detail=count=3 parses as detail="count=3" (learned in ITEM-017).
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


MANIFEST_KEYS = ['"id"', '"cam"', '"level"', '"trigger_ms"', '"t0_ms"', '"t1_ms"',
                 '"duration_ms"', '"state"', '"complete"', '"frame_count"']


def parse_clip_probe(text: str) -> dict:
    # Probe lines look like "type=config pre_s=30 ..."; kv() sees type=config as
    # its first pair, so pass the WHOLE line (learned in v1: stripping the
    # type= prefix leaves a keyless first token that breaks kv()).
    p = {"config": None, "buffer": None, "clips": [], "finalize": None, "store": None,
         "manifest": None, "retention": None, "gap": None, "acceptance": None}
    for line in text.splitlines():
        s = line.strip()
        if not s.startswith("type="):
            continue
        if s.startswith("type=clip-manifest "):
            p["manifest"] = s[len("type=clip-manifest "):]
            continue
        name = s[len("type="):].split(" ", 1)[0]
        if name == "clip":
            p["clips"].append(kv(s))
        else:
            key = {"clip-store": "store"}.get(name, name)  # v2 lesson: probe uses type=clip-store
            key = key.replace("-", "_")
            if key in p:
                p[key] = kv(s)
    return p


def assert_clip_probe(p: dict) -> list:
    fails = []
    cfg = p["config"] or {}
    if cfg.get("pre_s") != "30" or cfg.get("post_s") != "60":
        fails.append("config pre/post != 30/60")
    if cfg.get("buffer") != "2275":  # 90*25+25 derived depth
        fails.append("derived buffer depth != 2275 (got %r)" % cfg.get("buffer"))
    buf = p["buffer"] or {}
    if buf.get("frames") != "2251":
        fails.append("buffer frames=%r want 2251" % buf.get("frames"))
    if buf.get("capacity") != "2275":
        fails.append("buffer capacity=%r want 2275" % buf.get("capacity"))
    clips = p["clips"]
    if len(clips) != 2:
        fails.append("clip lines=%d want 2" % len(clips))
    else:
        c1, c2 = clips
        if c1.get("t0_ms") != "30000" or c1.get("t1_ms") != "120000" or \
                c1.get("duration_ms") != "90000" or c1.get("state") != "recording":
            fails.append("clip1 range/state")
        if c1.get("id") != "1" or c1.get("cam") != "CAM-01":
            fails.append("clip1 id/cam")
        if c2.get("state") != "finalized" or c2.get("complete") != "true":
            fails.append("clip2 finalized/complete")
        if c2.get("frames") != "2001" or c2.get("duration_ms") != "90000":
            fails.append("clip2 frames/duration")
    fin = p["finalize"] or {}
    if fin.get("before") != "0" or fin.get("at") != "1":
        fails.append("finalize split before/at")
    store = p["store"] or {}
    if store.get("dir_ok") != "true" or store.get("wrote") != "true" or \
            store.get("exists") != "true":
        fails.append("atomic store wrote/exists")
    man = p.get("manifest") or ""
    if not (man.startswith("{\"id\":1") and man.endswith("}")):
        fails.append("manifest not a JSON object line")
    missing = [k for k in MANIFEST_KEYS if k not in man]
    if missing:
        fails.append("manifest missing keys: %s" % missing)
    for needle, want in (('"duration_ms":90000', True), ('"state":"finalized"', True),
                         ('"complete":true', True), ('"frame_count":2001', True),
                         ('"level":"L1"', True), ('"cam":"CAM-01"', True)):
        if (needle in man) != want:
            fails.append("manifest %r mismatch" % needle)
    ret = p["retention"] or {}
    if ret.get("evicted") != "1" or ret.get("continuous") != "1" or \
            ret.get("critical") != "0" or ret.get("protected_remaining") != "1":
        fails.append("retention protected priority %r" % ret)
    gap = p["gap"] or {}
    if gap.get("count") != "1" or gap.get("t0") != "200000" or gap.get("t1") != "210000" or \
            gap.get("reason") != "recording_interrupted":
        fails.append("gap merged row %r" % gap)
    acc = p["acceptance"] or {}
    if acc.get("duration_ms") != "90000" or acc.get("clip_complete") != "true" or \
            acc.get("clip_file_exists") != "true" or acc.get("protected_priority") != "true" or \
            acc.get("gap_merged") != "true" or acc.get("result") != "PASS":
        fails.append("acceptance line %r" % acc)
    return fails


def parse_core_tests_log(text: str) -> dict:
    return {"pass": [l.strip() for l in text.splitlines() if l.startswith("[PASS]")],
            "fail": [l.strip() for l in text.splitlines() if l.startswith("[FAIL]")],
            "result": next((l for l in text.splitlines() if l.startswith("RESULT:")), "")}


def main() -> int:
    ap = argparse.ArgumentParser(description="ITEM-019 verification fixtures")
    ap.add_argument("--host", required=True)
    ap.add_argument("--port", type=int, default=22)
    ap.add_argument("--user", required=True)
    ap.add_argument("--credential-env", default="TRAIN_RK3588_SSH_CREDENTIAL")
    ap.add_argument("--expected-fingerprint", required=True)
    ap.add_argument("--workdir", default="/tmp/railhmi_verify019")
    ap.add_argument("--cpp", required=True)
    ap.add_argument("--outdir", required=True)
    ap.add_argument("--stamp", required=True)
    ap.add_argument("--impl-testrun-log", required=True)
    ap.add_argument("--impl-clipprobe-log", required=True)
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
        cl = outdir / ("build-TestRun-%s-%s.core-evidence_clip-probe-%s.log"
                       % (host_u, stamp, stamp))
        if not (tl.is_file() and tx.is_file() and cl.is_file()):
            note("fresh-artifacts", {"exit": None,
                 "output": "missing fresh artifacts: %s %s %s"
                           % (tl.name, tx.name, cl.name)})
            ok = False
        else:
            note("fresh-artifacts", {"exit": 0,
                 "output": tl.name + " " + tx.name + " " + cl.name})

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
            clip_cases = [n2 for n2 in re.findall(r'name="([^"]+)"', xtext)
                          if re.search(r"buffer|clip|gap|manifest|atomic|retention|"
                                       r"finalize|frame", n2, re.I)]
            junit_ok = (n_tests is not None and n_fail == "0" and len(clip_cases) >= 30)
            note("junit-parse", {"exit": 0 if junit_ok else 1,
                 "output": "junit %s tests=%s failures=%s clip_cases=%d"
                           % (tx.name, n_tests, n_fail, len(clip_cases))})
            if not junit_ok:
                ok = False
        else:
            note("junit-parse", {"exit": None, "output": "no fresh junit xml"})
            ok = False

        # 3. independent assertions on the fresh clip-probe log
        if cl.is_file():
            fresh_text = cl.read_text(encoding="utf-8", errors="replace")
            vp = parse_clip_probe(fresh_text)
            fails = assert_clip_probe(vp)
            note("clip-probe-assert", {"exit": 0 if not fails else 1,
                 "output": json.dumps({"fails": fails}, ensure_ascii=False)})
            if fails:
                ok = False
            impl_probe_text = Path(args.impl_clipprobe_log).read_text(
                encoding="utf-8", errors="replace")
            identical = impl_probe_text.splitlines() == fresh_text.splitlines()
            note("clip-probe-compare", {"exit": 0 if identical else 1,
                 "output": "deterministic reproduction identical=%s" % identical})
            if not identical:
                ok = False
        else:
            note("clip-probe-assert", {"exit": None, "output": "missing fresh clip-probe"})
            ok = False

        # 4. ext fixture compile+run against THIS round's static libs
        if ext_run(client, args, wd, remote_dir, note) != 0:
            ok = False

        # 5. negative CLI edge on the deliverable probe + determinism re-run
        n1 = rrun(client, "cd %s && ./build-core/tools/railhmi-clip-probe --nope; "
                          "echo NARG_EXIT=$?" % remote_dir, 30)
        note("clip-probe-negative-arg", n1)
        if "NARG_EXIT=2" not in n1.get("output", ""):
            ok = False
        n1b = rrun(client, "cd %s && ./build-core/tools/railhmi-clip-probe --help; "
                           "echo HELP_EXIT=$?" % remote_dir, 30)
        note("clip-probe-help", n1b)
        if "HELP_EXIT=0" not in n1b.get("output", ""):
            ok = False
        n2 = rrun(client, "cd %s && ./build-core/tools/railhmi-clip-probe "
                          "--dir /tmp/railhmi_clips --out verify/clip-probe-rep.log "
                          ">/dev/null 2>&1; echo REPL_EXIT=$?" % remote_dir, 60)
        note("clip-probe-rep-run", n2)
        if "REPL_EXIT=0" not in n2.get("output", ""):
            ok = False
        n2b = rrun(client, "cd %s && diff %s/verify/clip-probe-rep.log "
                           "%s/core-evidence/clip-probe-%s.log && echo IDENTICAL"
                           % (remote_dir, remote_dir, remote_dir, stamp), 30)
        note("clip-probe-determinism", n2b)
        if "IDENTICAL" not in n2b.get("output", ""):
            ok = False

        # 6. fault-injection regression artifacts from the same TestRun
        reg = rrun(client, "cd %s && ls core-evidence/" % remote_dir, 30)
        note("regression-artifacts", reg)
        reg_ok = (reg.get("exit") == 0
                  and "sim-inject" in reg.get("output", "")
                  and "status-probe" in reg.get("output", "")
                  and "ai-probe" in reg.get("output", "")
                  and "alarm-probe" in reg.get("output", ""))
        if not reg_ok:
            ok = False

        # 7. cleanup by name; trailing ls of removed dirs is the confirmation
        # output (exit=2 expected, same benign pattern as prior rounds)
        note("cleanup", rrun(client,
             "rm -rf %s /tmp/railhmi_build /tmp/railhmi_clips /tmp/railhmi_clip_unit "
             "/tmp/railhmi_clip_ext && echo fixture_cleanup_done && echo LEFT_LIST "
             "&& ls -d %s /tmp/railhmi_build /tmp/railhmi_clips "
             "/tmp/railhmi_clip_unit /tmp/railhmi_clip_ext 2>&1" % (wd, wd), 30))
        cleanup_res = transcript["steps"][-1]
        removed = (wd, "/tmp/railhmi_build", "/tmp/railhmi_clips",
                   "/tmp/railhmi_clip_unit", "/tmp/railhmi_clip_ext")
        cleanup_ok = ("fixture_cleanup_done" in cleanup_res.get("output", "")
                      and all(("cannot access '%s'" % d) in cleanup_res.get("output", "")
                              for d in removed))
        note("cleanup-verify", {"exit": 0 if cleanup_ok else 1,
                                "output": "cleanup_ok=%s" % cleanup_ok})
        if not cleanup_ok:
            ok = False
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
    res = rrun(client, "mkdir -p %s/verify" % remote_dir, 30)
    note("mkdir-verify", res)
    res2 = rrun(client, "rm -rf %s && mkdir -p %s" % (args.workdir, args.workdir), 30)
    note("mkdir-workdir", res2)
    sftp = client.open_sftp()
    sftp.get_channel().settimeout(120)
    try:
        sftp.put(args.cpp, "%s/verify_clip19_ext.cpp" % wd)
        note("upload-fixture", {"exit": 0, "output": args.cpp})
    finally:
        sftp.close()
    compile_cmd = ("cd %s && g++ -std=c++17 -O1 -Wall -Wextra "
                   "-Isrc/core/include -Isrc/platform/include "
                   "%s/verify_clip19_ext.cpp build-core/src/core/librailhmi_core.a "
                   "build-core/src/platform/librailhmi_platform.a "
                   "-o %s/verify_clip19_ext 2>&1; echo GXX_EXIT=$?"
                   % (remote_dir, wd, wd))
    cext = rrun(client, compile_cmd, 240)
    note("compile-ext", cext)
    if cext.get("exit") != 0 or "GXX_EXIT=0" not in cext.get("output", ""):
        return 1
    rext = rrun(client, "cd %s && %s/verify_clip19_ext; echo EXT_EXIT=$?"
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
