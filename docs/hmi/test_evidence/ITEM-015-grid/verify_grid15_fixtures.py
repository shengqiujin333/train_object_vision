#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""software_verification fixture runner for ITEM-015 six-channel concurrency &
all-cameras grid (verifier-owned).

Independent board-side round over the registered Paramiko SSH entry:
  1. Reproduce the implementer's authoritative core round: upload the snapshot
     of app/, run the deliverable entry `bash tests/run_core_tests.sh`
     (builds railhmi_core incl. grid.cpp/channel_view.cpp, runs 411 cases),
     archive log + JUnit XML back; compare PASS lines with the implementer's
     archived log line-by-line (programmatic); parse the XML independently and
     require >=12 ITEM-015 grid/channel-view cases present.
  2. Regression round (tokens selftest + no-hardcode scan + sim-inject).
  3. Deliverable media round: `bash build.sh MediaCheck` + the deliverable
     CLI `railhmi-multi-probe` re-run by the verifier with INDEPENDENT
     parameterizations beyond the implementer's single default run:
       - cut-index 1 (left/second channel instead of default 3), 6 channels
       - cut-index 0 (first channel), 6 channels
       - count 2 with cut-index 1 (two-channel config)
       - count 0 (argument edge; must still be a well-formed exit, not a crash)
     each output log parsed programmatically for:
       all cells live/presentable before cut, cut cell not presentable with
       frozen last_frame_ms, order (idx->id map) unchanged across the cut,
       other cells still presentable, summary counts, occurrences of the
       final acceptance line.
  4. Verifier-owned core ext fixtures (verify_grid15_ext.cpp) linked against
     the librailhmi_core.a built by the entry itself — assertions beyond the
     implementer's test_grid.cpp:
       exact hand-computed 3x2 cell geometry (all fields, incl. remainder
       edge identity + gap=0 + 2x3 + zero-size clamp)
       criterion discrimination (overlapping rects / swapped slots must be
       rejected; edge-adjacent accepted)
       full six-state Chinese labels pairwise distinct (三重编码文字)
       text-format edge domains (fps/last-frame) incl. negative-age safety
       partial-runtime order robustness, extra-runtime ignored, determinism
       stale threshold boundary (strictly greater), no-frame '—'
       config-driven views from real cameras.yaml baseline: names/badges/
       order; cut on CAM-04 identity; CAM-02 removal -> 5 views follow
       config order; rename -> view name follows config
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
import sys
import tarfile
import time
from pathlib import Path

try:
    import paramiko
except Exception as exc:  # pragma: no cover
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


def parse_multi_log(text: str, *, count: int, cut_index: int, cut_after_s: int) -> dict:
    """Programmatic parse of a railhmi-multi-probe log (verifier-owned criteria).

    Acceptance semantics re-derived independently from the per-cell log lines;
    does not trust the delivery's own acceptance line (but reports it).
    """
    rounds = []            # each: {"t_ms": int, "cells": {idx: cell}}
    cell_ids = {}          # idx -> id (first seen)
    acceptance = None
    summary = None
    header = None
    injection_t = None
    cur = None

    for line in text.splitlines():
        toks = dict(t.split("=", 1) for t in line.split() if "=" in t)
        if "type=multi" in line:
            header = toks
        elif "type=cell" in line and line.startswith("t="):
            if cur is None:
                cur = {"t_ms": int(toks["t"]), "cells": {}}
            idx = int(toks["idx"])
            cell_ids.setdefault(idx, toks["id"])
            cur["cells"][idx] = {
                "id": toks["id"], "state": toks["state"], "fps": float(toks["fps"]),
                "last_frame_ms": int(toks["last_frame_ms"]),
                "reconnect": int(toks["reconnect"]),
                "presentable": toks["presentable"] == "true"}
        elif "type=grid" in line and line.startswith("t=") and cur is not None:
            cur["t_ms"] = int(toks["t"])
            rounds.append(cur)
            cur = None
        elif "type=injection" in line and line.startswith("t="):
            injection_t = int(toks["t"])
        elif "type=summary" in line:
            summary = toks
        elif "type=acceptance" in line:
            acceptance = toks

    if count <= 0:
        return {"rounds": len(rounds), "acceptance": acceptance, "summary": summary,
                "header": header}

    cut_round = cut_after_s * 1000
    pre = [r for r in rounds if r["t_ms"] < cut_round]
    post = [r for r in rounds if injection_t is not None and r["t_ms"] > injection_t]

    all_live_before = bool(pre) and all(
        len(r["cells"]) == count and
        all(c["presentable"] and c["state"] == "live" for c in r["cells"].values())
        for r in pre)

    cut_not_presentable = False
    cut_froze = False
    cut_last = None
    others_ok = True
    order_unchanged = True
    for r in post:
        if cut_index in r["cells"]:
            c = r["cells"][cut_index]
            if not c["presentable"]:
                cut_not_presentable = True
            if cut_last is None:
                cut_last = c["last_frame_ms"]
            elif c["last_frame_ms"] == cut_last:
                cut_froze = True
            if c["id"] != cell_ids.get(cut_index):
                order_unchanged = False
        for idx, c in r["cells"].items():
            if idx == cut_index:
                continue
            if not c["presentable"]:
                others_ok = False
            if c["id"] != cell_ids.get(idx):
                order_unchanged = False
        if len(r["cells"]) != count:
            order_unchanged = False

    return {
        "rounds": len(rounds),
        "acceptance": acceptance,
        "summary": summary,
        "header": header,
        "all_live_before_cut": all_live_before,
        "cut_not_presentable": cut_not_presentable,
        "cut_last_frame_frozen": cut_froze,
        "others_still_presentable": others_ok,
        "order_unchanged": order_unchanged,
        "cut_cell_id": cell_ids.get(cut_index),
    }


def main() -> int:
    ap = argparse.ArgumentParser(description="ITEM-015 verification fixtures")
    ap.add_argument("--host", required=True)
    ap.add_argument("--port", type=int, default=22)
    ap.add_argument("--user", required=True)
    ap.add_argument("--credential-env", default="TRAIN_RK3588_SSH_CREDENTIAL")
    ap.add_argument("--expected-fingerprint", required=True)
    ap.add_argument("--workdir", default="/tmp/railhmi_verify015")
    ap.add_argument("--cpp", required=True)
    ap.add_argument("--outdir", required=True)
    ap.add_argument("--stamp", required=True, help="stamp; bump per verification round")
    ap.add_argument("--impl-log", default=None,
                    help="implementer archived core-tests log for line-by-line comparison")
    ap.add_argument("--connect-timeout", type=int, default=10)
    ap.add_argument("--timeout-sec", type=int, default=1200,
                    help="finite ceiling for the entry build+test step")
    ap.add_argument("--media-timeout-sec", type=int, default=900,
                    help="finite ceiling for each MediaCheck build+probe step")
    ap.add_argument("--app-root", default=str(Path(__file__).resolve().parents[4] / "app"),
                    help="local app/ tree to upload and build on the board")
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
    board_wd = wd + "_board"
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
        local_out = outdir / ("%s.%s" % (tag, local_name))
        try:
            sftp.get("%s/app/%s" % (board_wd, remote_rel), str(local_out))
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

        # 0. upload snapshot of app/ into an isolated dir
        note("mkdir", rrun(client, "rm -rf %s %s && mkdir -p %s/app %s"
                           % (wd, board_wd, board_wd, wd), 30))
        buf = io.BytesIO()
        with tarfile.open(fileobj=buf, mode="w:gz") as tf:
            for p in sorted(Path(args.app_root).rglob("*")):
                if p.is_file():
                    tf.add(str(p), arcname=str(p.relative_to(args.app_root)))
        data = buf.getvalue()
        buf.seek(0)
        sftp = client.open_sftp()
        sftp.get_channel().settimeout(120)
        try:
            remote_tar = "%s/app.tgz" % wd
            sftp.putfo(buf, remote_tar, len(data))
            note("upload-app", {"exit": 0,
                 "output": "uploaded app tree (%d bytes compressed)" % len(data)})
            remote_yaml = "%s/design_tokens.yaml" % wd
            sftp.put(args.tokens_yaml, remote_yaml)
            note("upload-tokens-yaml", {"exit": 0,
                 "output": "uploaded %s" % (args.tokens_yaml,)})
            remote_cpp = "%s/verify_grid15_ext.cpp" % wd
            sftp.put(args.cpp, remote_cpp)
            note("upload-fixture", {"exit": 0,
                 "output": "uploaded %s -> %s" % (args.cpp, remote_cpp)})
        finally:
            sftp.close()

        note("extract", rrun(client,
             "cd %s && tar xzf %s/app.tgz -C %s/app && cp %s/design_tokens.yaml %s/app/design_tokens.yaml "
             "&& echo APP_FILES=$(cd %s/app && find . -type f | wc -l)"
             % (board_wd, wd, board_wd, wd, board_wd, board_wd), 60))

        # 1. deliverable entry: build core (+grid/channel_view) + full test set
        entry = rrun(client,
            "cd %s/app && bash tests/run_core_tests.sh --outdir core-evidence "
            "--stamp %s 2>&1; echo ENTRY_EXIT=$?" % (board_wd, args.stamp),
            args.timeout_sec)
        note("run-entry", entry)
        if entry["exit"] != 0 or "ENTRY_EXIT=0" not in entry.get("output", "") or \
                "RESULT: PASS passed=" not in entry.get("output", ""):
            ok = False

        sftp = client.open_sftp()
        sftp.get_channel().settimeout(120)
        try:
            remote_log = "core-evidence/core-tests-%s.log" % args.stamp
            ok &= dl(sftp, remote_log, "core-evidence_core-tests-%s.log" % args.stamp)
            ok &= dl(sftp, "core-evidence/core-tests-%s.xml" % args.stamp,
                     "core-evidence_core-tests-%s.xml" % args.stamp)
        finally:
            sftp.close()
        local_log = outdir / ("%s.core-evidence_core-tests-%s.log" % (tag, args.stamp))

        # 1b. programmatic line-by-line comparison with implementer's archived log
        if local_log.is_file() and args.impl_log and os.path.isfile(args.impl_log):
            try:
                impl_lines = [l for l in Path(args.impl_log).read_text(
                    encoding="utf-8", errors="replace").splitlines() if l.startswith("[PASS]")]
                ver_lines = [l for l in local_log.read_text(
                    encoding="utf-8", errors="replace").splitlines() if l.startswith("[PASS]")]
                same = impl_lines == ver_lines
                note("log-compare", {
                    "exit": 0 if same else 1,
                    "output": "impl_pass_lines=%d verify_pass_lines=%d identical=%s"
                              % (len(impl_lines), len(ver_lines), same)})
                if not same:
                    ok = False
            except Exception as exc:  # noqa: BLE001
                note("log-compare", {"exit": None, "output": "compare error: %r" % (exc,)})
                ok = False

        # 1c. programmatic grid-domain check from JUnit XML (independent parse)
        local_xml = outdir / ("%s.core-evidence_core-tests-%s.xml" % (tag, args.stamp))
        if local_xml.is_file():
            try:
                xtext = local_xml.read_text(encoding="utf-8", errors="replace")
                m = re.search(r'tests="(\d+)".*?failures="(\d+)"', xtext, re.S)
                if not m:
                    m = re.search(r'failures="(\d+)".*?tests="(\d+)"', xtext, re.S)
                    if m:
                        n_tests, n_fail = m.group(2), m.group(1)
                    else:
                        n_tests, n_fail = None, None
                else:
                    n_tests, n_fail = m.group(1), m.group(2)
                grid_cases = len(re.findall(
                    r'name="[^"]*(?:3x2|2x3|slot|unstarted|cut:|placeholder|'
                    r'last-frame|fps text|grid)[^"]*"', xtext))
                domains_ok = (n_tests is not None and n_fail == "0" and grid_cases >= 12)
                note("junit-parse", {"exit": 0 if domains_ok else 1,
                     "output": "junit tests=%s failures=%s grid_case_hint=%d"
                               % (n_tests, n_fail, grid_cases)})
                if not domains_ok:
                    ok = False
            except Exception as exc:  # noqa: BLE001
                note("junit-parse", {"exit": None, "output": "junit parse error: %r" % (exc,)})
                ok = False

        # 2. tokens selftest + no-hardcode scan + sim-inject regression
        tok = rrun(client,
            "cd %s/app && python3 tools/gen_tokens.py --selftest --yaml design_tokens.yaml; "
            "echo TOKENS_EXIT=$?" % board_wd, 120)
        note("tokens-selftest", tok)
        if tok["exit"] != 0 or "TOKENS_EXIT=0" not in tok.get("output", ""):
            ok = False

        scan = rrun(client,
            "cd %s/app && python3 tools/check_no_hardcoded_ui.py --root src/ui; echo SCAN_EXIT=$?"
            % board_wd, 120)
        note("ui-scan", scan)
        if scan["exit"] != 0 or "SCAN_EXIT=0" not in scan.get("output", ""):
            ok = False

        sim = rrun(client,
            "cd %s/app && ./build-core/tools/railhmi-sim-inject "
            "--fi 01,02,03,04,05,10 --duration 6000 "
            "--out core-evidence/sim-inject-%s.log "
            "--json core-evidence/sim-inject-%s.json 2>&1; echo SIM_EXIT=$?"
            % (board_wd, args.stamp, args.stamp), 60)
        note("sim-inject", sim)
        if sim["exit"] != 0 or "SIM_EXIT=0" not in sim.get("output", ""):
            ok = False
        sftp = client.open_sftp()
        sftp.get_channel().settimeout(120)
        try:
            ok &= dl(sftp, "core-evidence/sim-inject-%s.log" % args.stamp,
                     "core-evidence_sim-inject-%s.log" % args.stamp)
            ok &= dl(sftp, "core-evidence/sim-inject-%s.json" % args.stamp,
                     "core-evidence_sim-inject-%s.json" % args.stamp)
        finally:
            sftp.close()

        # 3. deliverable media round (build MediaCheck once via build.sh)
        mbuild = rrun(client,
            "cd %s/app && bash build.sh MediaCheck; echo MBUILD_EXIT=$?"
            % board_wd, args.media_timeout_sec)
        note("media-build", mbuild)
        if mbuild["exit"] != 0 or "MBUILD_EXIT=0" not in mbuild.get("output", ""):
            ok = False

        # independent parameterizations of the deliverable multi-probe
        variants = [
            ("cut1_count6", "--count 6 --source test --duration 8 --cut-index 1 --cut-after 4"),
            ("cut0_count6", "--count 6 --source test --duration 8 --cut-index 0 --cut-after 4"),
            ("count2_cut1", "--count 2 --source test --duration 6 --cut-index 1 --cut-after 3"),
        ]
        media_results = {}
        for vname, vargs in variants:
            vlog = "core-evidence/multi-%s-%s.log" % (vname, args.stamp)
            vjson = "core-evidence/multi-%s-%s.json" % (vname, args.stamp)
            vrun = rrun(client,
                "cd %s/app && mkdir -p core-evidence && ./build-media/tools/railhmi-multi-probe "
                "%s --out %s --json %s; echo VEXIT_%s=$?" % (board_wd, vargs, vlog, vjson, vname),
                180)
            note("multi-" + vname, vrun)
            try:
                s2 = client.open_sftp()
                s2.get_channel().settimeout(120)
                local_v = outdir / ("%s.multi-%s.log" % (tag, vname))
                try:
                    s2.get("%s/app/%s" % (board_wd, vlog), str(local_v))
                    note("download", {"exit": 0, "output": local_v.name})
                except OSError as exc:
                    note("download", {"exit": None, "output": "%s: %r" % (vlog, exc)})
                    ok = False
                finally:
                    s2.close()
                if local_v.is_file():
                    cnt = 6 if "count6" in vname else (2 if vname == "count2_cut1" else 0)
                    ci = int(re.search(r"--cut-index (\d+)", vargs).group(1))
                    ca = int(re.search(r"--cut-after (\d+)", vargs).group(1))
                    parsed = parse_multi_log(local_v.read_text(encoding="utf-8",
                                                                errors="replace"),
                                             count=cnt, cut_index=ci, cut_after_s=ca)
                    media_results[vname] = parsed
                    aa = parsed["acceptance"]
                    good = (vrun.get("exit") == 0
                            and ("VEXIT_%s=0" % vname) in vrun.get("output", "")
                            and aa is not None and aa.get("result") == "PASS"
                            and parsed["all_live_before_cut"] is True
                            and parsed["cut_not_presentable"] is True
                            and parsed["cut_last_frame_frozen"] is True
                            and parsed["others_still_presentable"] is True
                            and parsed["order_unchanged"] is True
                            and parsed["rounds"] >= 6)
                    note("multi-parse-" + vname, {"exit": 0 if good else 1,
                         "output": json.dumps(parsed, ensure_ascii=False)})
                    if not good:
                        ok = False
            except Exception as exc:  # noqa: BLE001
                note("multi-parse-" + vname, {"exit": None, "output": "parse error: %r" % (exc,)})
                ok = False

        # negative edge: count 0 must fail cleanly (exit 3), not crash
        neg = rrun(client,
            "cd %s/app && ./build-media/tools/railhmi-multi-probe --count 0 --source test "
            "--duration 1; echo NEG_EXIT=$?" % board_wd, 60)
        note("multi-negative", neg)
        if "NEG_EXIT=3" not in neg.get("output", ""):
            ok = False

        # 4. verifier-owned core ext fixtures
        probe = rrun(client, "cd %s/app && ls build-core/src/core/librailhmi_core.a; "
                             "echo PROBE_EXIT=$?" % board_wd, 60)
        note("probe-lib", probe)
        lib_path = ("%s/app/build-core/src/core/librailhmi_core.a" % board_wd)
        if probe.get("exit") != 0:
            alt = rrun(client,
                       "find %s/app/build-core -name 'librailhmi_core.a' 2>/dev/null | head -1"
                       % board_wd, 60)
            first = (alt.get("output") or "").strip().splitlines()
            lib_path = first[0].strip() if first else lib_path
        compile_cmd = ("cd %s/app && g++ -std=c++17 -O1 -Wall -Wextra -Isrc/core/include "
                       "%s/verify_grid15_ext.cpp %s -o %s/verify_grid15_ext 2>&1; "
                       "echo GXX_EXIT=$?" % (board_wd, wd, lib_path, wd))
        note("compile-ext", rrun(client, compile_cmd, 240))
        run_ext = rrun(client,
                       "cd %s/app && %s/verify_grid15_ext; echo EXT_EXIT=$?"
                       % (board_wd, wd), 180)
        note("run-ext", run_ext)
        if run_ext.get("exit") != 0 or "RESULT: PASS passed=" not in run_ext.get("output", ""):
            ok = False

        # 5. cleanup by name
        note("cleanup", rrun(client,
            "rm -rf %s %s && echo fixture_cleanup_done && echo LEFT_LIST && ls -d %s %s 2>&1"
            % (wd, board_wd, wd, board_wd), 30))
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
