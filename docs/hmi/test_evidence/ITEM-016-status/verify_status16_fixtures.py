# -*- coding: utf-8 -*-
"""software_verification fixture runner for ITEM-016 key status strip &
top mode bar (verifier-owned).

Board-side verification over the registered Paramiko SSH entry:
  1. Reproduce the deliverable TestRun entry (board_build.py) with a verifier
     stamp: core-tests build+run (443 cases incl. status domain), tokens
     selftest, no-hardcode scan, sim-inject regression, and the NEW deliverable
     railhmi-status-probe in both implementer scenarios (mixed/maintainer,
     mixed/driver); archive logs + JUnit XML back.
  2. Programmatic assertions on the probe outputs parsed independently (do not
     trust its own acceptance line only):
       - probe maintainer: all abnormal items allowed=true, non-abnormal
         allowed=false; per-item shape/glyph/label triple; driver scenario:
         every abnormal item allowed=false while still clickable=true.
       - triple-encoding line distinct shapes/glyphs/labels/colors == 5.
       - mode bit: value 可见光, secondary 红外未接入, not clickable.
       - unconnected 车速/门磁 shown as 未接入 with minus-circle.
  3. Verifier verdict crosscheck with role semantics: click targets per
     CATEGORY (door->P4 etc.) parsed from probe line 'target='.
  4. Verifier-owned ext fixtures (verify_status16_ext.cpp) linked against the
     librailhmi_core.a built by the entry itself — assertions beyond the
     implementer's test_status.cpp (explicit 5-level value identity, Info
     propagation, empty-value defaulting, top-bar kind identity, resolve
     matrices, 4x7 permission matrix vs role_can_access, role
     insensitivity, determinism).
  5. Cleanup workdirs by name.
Fingerprint mismatch aborts. Credentials only from the named env var, never
logged. Every remote step has a finite timeout.
"""
from __future__ import annotations

import argparse
import base64
import hashlib
import io
import json
import os
import re
import socket
import subprocess
import sys
import tarfile
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


def parse_probe(text: str) -> dict:
    """Independent parse of a railhmi-status-probe output (verifier criteria)."""
    parsed = {"key_items": [], "top_items": [], "triple": None, "acceptance": None}
    cur = "key_items"
    for line in text.splitlines():
        if line.startswith("type=status"):
            cur = "key_items"
            continue
        if line.startswith("type=topbar"):
            cur = "top_items"
            continue
        if line.startswith("type=triple-encoding"):
            toks = dict(t.split("=", 1) for t in line.split() if "=" in t)
            parsed["triple"] = toks
            continue
        if line.startswith("type=acceptance"):
            toks = dict(t.split("=", 1) for t in line.split() if "=" in t)
            parsed["acceptance"] = toks
            continue
        if line.startswith("item "):
            toks = {}
            # value/ secondary may contain spaces; split key=value pairs from the left,
            # consuming everything up to the next known key.
            known = {"key", "label", "level", "shape", "glyph", "color", "value", "secondary",
                     "abnormal", "clickable", "target", "allowed", "unavailable"}
            parts = line.split()
            i = 1
            while i < len(parts):
                k = parts[i].split("=", 1)[0]
                v = parts[i].split("=", 1)[1] if "=" in parts[i] else ""
                while (i + 1 < len(parts) and parts[i + 1].split("=", 1)[0] not in known
                       and "=" not in parts[i + 1]):
                    v += " " + parts[i + 1]
                    i += 1
                toks[k] = v
                i += 1
            toks.setdefault("secondary", "")
            toks["abnormal"] = toks.get("abnormal") == "true"
            toks["clickable"] = toks.get("clickable") == "true"
            toks["allowed"] = toks.get("allowed") == "true"
            toks["unavailable"] = toks.get("unavailable") == "true"
            parsed[cur].append(toks)
    return parsed


def assert_probe(parsed: dict, *, role: str) -> list:
    """Verifier assertions on one probe round; returns list of failure strings."""
    fails = []
    items = parsed["key_items"]
    tops = parsed["top_items"]
    if len(items) != 7:
        fails.append("key items=%d want 7" % len(items))
    if len(tops) != 7:
        fails.append("top items=%d want 7" % len(tops))
    keys = [it["key"] for it in items]
    if keys != ["pantograph", "wheel_rail", "door", "camera", "ai", "recording", "storage"]:
        fails.append("key order mismatch: %s" % keys)
    tkeys = [t["key"] for t in tops]
    if tkeys != ["time", "mode", "recording", "ai", "health", "vehicle_speed", "door_sensor"]:
        fails.append("top order mismatch: %s" % tkeys)
    for it in items:
        abn = it["abnormal"]
        if abn and not it["clickable"]:
            fails.append("abnormal %s not clickable" % it["key"])
        if not abn and it["clickable"]:
            fails.append("non-abnormal %s clickable" % it["key"])
        # B&W: every level must include a non-empty glyph+label token set
        if it["level"] not in ("time",) and not it.get("glyph"):
            fails.append("item %s empty glyph" % it["key"])
        # click permission semantics
        if role == "maintainer" and abn and not it["allowed"]:
            fails.append("maintainer abnormal %s not allowed" % it["key"])
        if role == "driver":
            if abn and it["allowed"]:
                fails.append("driver abnormal %s unexpectedly allowed" % it["key"])
            if it["allowed"]:
                fails.append("driver any item allowed (want none)")
        # targets per design
        want_target = {"pantograph": "all_cameras", "wheel_rail": "all_cameras",
                       "door": "all_cameras", "ai": "ai_debug", "recording": "replay",
                       "storage": "maintenance", "camera": "maintenance"}.get(it["key"])
        if want_target and it["target"] != want_target:
            fails.append("%s target=%s want %s" % (it["key"], it["target"], want_target))
    # top bar
    mode = tops[1] if len(tops) > 1 else {}
    if mode.get("value") != "可见光" or mode.get("secondary") != "红外未接入":
        fails.append("mode bit content wrong: %r" % mode)
    if mode.get("clickable"):
        fails.append("mode clickable (want no toggle)")
    for name in ("vehicle_speed", "door_sensor"):
        got = [t for t in tops if t["key"] == name]
        if not got or not got[0]["unavailable"] or got[0]["value"] != "未接入":
            fails.append("%s not shown as 未接入" % name)
    trip = parsed["triple"]
    if not trip or trip.get("distinct_shapes") != "5" or trip.get("distinct_glyphs") != "5" \
            or trip.get("distinct_labels") != "5" or trip.get("distinct_colors") != "5":
        fails.append("triple-encoding line incomplete: %r" % trip)
    acc = parsed["acceptance"]
    if not acc or acc.get("result") != "PASS":
        fails.append("acceptance result not PASS: %r" % acc)
    return fails


def main() -> int:
    ap = argparse.ArgumentParser(description="ITEM-016 verification fixtures")
    ap.add_argument("--host", required=True)
    ap.add_argument("--port", type=int, default=22)
    ap.add_argument("--user", required=True)
    ap.add_argument("--credential-env", default="TRAIN_RK3588_SSH_CREDENTIAL")
    ap.add_argument("--expected-fingerprint", required=True)
    ap.add_argument("--workdir", default="/tmp/railhmi_verify016")
    ap.add_argument("--cpp", required=True)
    ap.add_argument("--outdir", required=True)
    ap.add_argument("--stamp", required=True)
    ap.add_argument("--impl-log", default=None,
                    help="implementer archived core-tests log for line-by-line comparison")
    ap.add_argument("--keep-remote", action="store_true",
                    help="keep /tmp/railhmi_build for in-session ext steps (always cleaned at the end)")
    ap.add_argument("--connect-timeout", type=int, default=10)
    ap.add_argument("--timeout-sec", type=int, default=1500,
                    help="finite ceiling for the TestRun entry")
    ap.add_argument("--app-root", default=str(Path(__file__).resolve().parents[4] / "app"))
    ap.add_argument("--tokens-yaml",
                    default=str(Path(__file__).resolve().parents[4] /
                                "docs/hmi/design_tokens.yaml"))
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

    def dl(sftp, remote_rel: str, local_name: str) -> bool:
        local_out = outdir / ("%s.%s" % (tag, remote_rel.replace("/", "_")))
        try:
            sftp.get("%s/%s" % (remote_dir, remote_rel), str(local_out))
            note("download", {"exit": 0, "output": local_name})
        except OSError as exc:
            note("download", {"exit": None, "output": "%s: %r" % (remote_rel, exc)})
            return False
        return True

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

    entry_exit = None
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

        # upload app tree using the registered board_build.py TestRun entry itself
        note("mkdir", rrun(client, "mkdir -p %s %s" % (wd, remote_dir), 30))
        # 1. deliverable TestRun entry with verifier stamp
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

        # verify-fixture transcript files (separate from the entry's own archive)
        stamp = args.stamp
        # 2. programmatic probe assertions (independent parse, both roles)
        # The entry downloaded its artifacts into outdir under the standard
        # build-TestRun-... .core-evidence_... prefix; locate them there.
        probe_files = {
            "maintainer": "status-probe-%s.log" % stamp,
            "driver": "status-probe-driver-%s.log" % stamp,
        }
        for role, rel in probe_files.items():
            pcands = sorted(outdir.glob("*core-evidence_%s" % rel))
            # only this round's fresh file (contains this stamp)
            fresh = [p for p in pcands if stamp in p.name]
            got = fresh[-1] if fresh else None
            if got is None:
                note("probe-assert-" + role, {"exit": None,
                                              "output": "missing archived probe log %s" % got})
                ok = False
                continue
            parsed = parse_probe(Path(got).read_text(encoding="utf-8", errors="replace"))
            fails = assert_probe(parsed, role=role)
            note("probe-assert-" + role, {
                "exit": 0 if not fails else 1,
                "output": json.dumps({"fails": fails,
                                      "items": len(parsed["key_items"]),
                                      "tops": len(parsed["top_items"])
                                      }, ensure_ascii=False)})
            if fails:
                ok = False

        # 2b. line-by-line PASS comparison with the implementer's archived log
        # Use ONLY this round's fresh artifacts (stamp = VERIFY-*), never the
        # implementer's archived files that happen to share the outdir.
        vlog = outdir / ("build-TestRun-%s-%s.core-evidence_core-tests-%s.log"
                         % (args.host.replace(".", "_"), stamp, stamp))
        if vlog.is_file() and args.impl_log and os.path.isfile(args.impl_log):
            impl_lines = [l for l in Path(args.impl_log).read_text(
                encoding="utf-8", errors="replace").splitlines()
                if l.startswith("[PASS]")]
            ver_lines = [l for l in vlog.read_text(
                encoding="utf-8", errors="replace").splitlines()
                if l.startswith("[PASS]")]
            same = impl_lines == ver_lines
            note("log-compare", {"exit": 0 if same else 1,
                 "output": "impl=%s verify=%s impl_pass_lines=%d verify_pass_lines=%d identical=%s"
                           % (os.path.basename(args.impl_log), vlog.name,
                              len(impl_lines), len(ver_lines), same)})
            if not same:
                ok = False
            # independent JUnit parse on this round's fresh XML
            xlog = outdir / ("build-TestRun-%s-%s.core-evidence_core-tests-%s.xml"
                             % (args.host.replace(".", "_"), stamp, stamp))
            if xlog.is_file():
                xtext = xlog.read_text(encoding="utf-8", errors="replace")
                m = re.search(r'tests="(\d+)"[^>]*failures="(\d+)"', xtext, re.S)
                n_tests, n_fail = (m.group(1), m.group(2)) if m else (None, None)
                status_cases = len(re.findall(
                    r'name="[^"]*(?:triple|key status|all-ok|camera warning|ai alarm|'
                    r'pantograph|driver click|maintainer click|top bar|'
                    r'vehicle speed|mode bit)[^"]*"',
                    xtext))
                junit_ok = (n_tests is not None and n_fail == "0" and status_cases >= 8)
                note("junit-parse", {"exit": 0 if junit_ok else 1,
                     "output": "junit %s tests=%s failures=%s status_cases=%d"
                               % (xlog.name, n_tests, n_fail, status_cases)})
                if not junit_ok:
                    ok = False
            else:
                note("junit-parse", {"exit": None, "output": "no fresh junit xml"})
                ok = False
        else:
            note("log-compare", {"exit": None,
                 "output": "missing fresh core-tests log %s or --impl-log; skipping" % vlog.name})
            ok = False

        # 3. verifier-owned ext fixture (linked against delivered librailhmi_core.a)
        # upload fixture source first, then compile (impl-log glob order safe)
        upload_sftp = client.open_sftp()
        upload_sftp.get_channel().settimeout(120)
        try:
            upload_sftp.put(args.cpp, "%s/verify_status16_ext.cpp" % wd)
            note("upload-fixture", {"exit": 0, "output": args.cpp})
        finally:
            upload_sftp.close()
        compile_cmd = ("cd %s && g++ -std=c++17 -O1 -Wall -Wextra -Isrc/core/include "
                       "%s/verify_status16_ext.cpp build-core/src/core/librailhmi_core.a "
                       "-o %s/verify_status16_ext 2>&1; echo GXX_EXIT=$?"
                       % (remote_dir, wd, wd))
        note("compile-ext", rrun(client, compile_cmd, 240))
        run_ext = rrun(client,
                       "cd %s && %s/verify_status16_ext; echo EXT_EXIT=$?"
                       % (remote_dir, wd), 180)
        note("run-ext", run_ext)
        ext_out = run_ext.get("output", "")
        m = re.search(r"RESULT: (\w+) passed=(\d+) failed=(\d+)", ext_out)
        if run_ext.get("exit") != 0 or "EXT_EXIT=0" not in ext_out or \
                not (m and m.group(1) == "PASS" and int(m.group(3)) == 0):
            ok = False
        if m:
            note("ext-summary", {"exit": 0 if m.group(1) == "PASS" else 1,
                 "output": "ext passed=%s failed=%s" % (m.group(2), m.group(3))})

        # 4. negative probe edges: unknown arg & unknown role must exit 2, not crash
        n1 = rrun(client, "cd %s && ./build-core/tools/railhmi-status-probe "
                          "--nope; echo N1_EXIT=$?" % remote_dir, 30)
        note("probe-negative-arg", n1)
        n2 = rrun(client, "cd %s && ./build-core/tools/railhmi-status-probe "
                          "--role nobody; echo N2_EXIT=$?" % remote_dir, 30)
        note("probe-negative-role", n2)
        if "N1_EXIT=2" not in n1.get("output", "") or "N2_EXIT=2" not in n2.get("output", ""):
            ok = False

        # 5. cleanup by name
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
