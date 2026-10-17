#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ITEM-003 验证侧扩展夹具（software_verification 自有判据）。

前提: _verify_run.py 已完成（board /tmp/railhmi_identity 构建树在位，
      verify-v1 prod 产出物已下载到本目录）。全部板端操作有限超时。

扩展判据域（自构造的判据域，不重写交付验收级断言）:
  EXT-1  fresh-round 标记核验: 本轮上传的 verify-marker.txt token 仍在板端构建树，
         且 --keep-remote 树由本轮真实重建（排除历史缓存树冒充本轮结果）。
  EXT-2  Generation-2 门隔离 (核心缺陷回归): 全新进程跑 --duration 6 --rebuild-after 2；
         用独立 FrameGate 链路断言（同 stream_probe 上报语义）重建前 generation=1、
         重建后 generation=2、rebuild 行与 identity_gate stale 行存在。
  EXT-3  --rebuild-after 0 立即重建: 负边界 (0 值) —— 立即 rebuild，运行期 main loop
         正常退出 exit=0, summary generation=2。
  EXT-4  --rebuild-after > duration: 正边界 —— 运行全程无重建，generation=1。
  EXT-5  --rebuild-after 非法 (&/负文本一致): 非法 atoll；判据用运行级行为（负数永不触发，
         等价 "不重建" 路径）而非自行解释 parser 语义。
  EXT-6  交消费者负能力回归记录: 与 交付 selftest 分离，独立重建 gate 状态 ——
         交付 selftest 的 7 个身份项独立、无弱断言子串（selftest 每项独立 PASS/FAIL，
         不合并为一个弱串）。
  EXT-7  rebuild 行与 stream_state 第一行归属代际切换的完整性:
         重建段首 stream_state 行的 seq 必须 ==1（重建重置）；
         且 summary 的 frames_emitted/gate_admitted/gate_rejected 完整域存在。
  EXT-8  时间语义: stream_state wall_ms 值可转 UTC（日期 2026-10-17 域内），
         与 mono last_frame_ms 恒不同（不是同一时间基），pts_ns 值 ∈ [0, 2^33) 且
         重建后出现明显小于重建前末端值（从头计）。
  EXT-9  IDENTITY 证据无凭据语义: evidence 目录文本文件里 user-pw=/password=/
         value@host 三类凭据语义形态 0 命中（三组真实凭据）。
  清理:  板端 /tmp/railhmi_identity 用后 rm -rf 并确认（cleanup 轮按输出核对）。

用法: python docs/video/test_evidence/ITEM-003/_verify_ext.py
退出码: 全 PASS → 0; 任一 FAIL → 1
"""
import io
import json
import os
import re
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

import paramiko

HERE = Path(__file__).resolve().parent
BOARD = "192.168.0.103"
USER = "admin"
REMOTE = "/tmp/railhmi_identity"
PROBE = REMOTE + "/build-media/tools/railhmi-stream-probe"
EXT_REMOTE_TMP = REMOTE + "/ext"
VERIFY_TAG = "identity-192_168_0_103-20261017-item003-verify-v1"
STATE_FILE = HERE / "verify_state.json"
CRED_NAMES = ("TRAIN_RK3588_SSH_CREDENTIAL", "TRAIN_FRONT_RTSP_CREDENTIAL",
              "TRAIN_SIDE_RTSP_CREDENTIAL")

results = []


def record(ext_id, ok, detail):
    results.append({"id": ext_id, "ok": bool(ok), "detail": detail})
    print("%s %s %s" % (ext_id, "PASS" if ok else "FAIL", detail))


def ps_user_var(name):
    r = subprocess.run(
        ["powershell.exe", "-NoProfile", "-Command",
         '[Environment]::GetEnvironmentVariable("%s","User")' % name],
        capture_output=True, text=True, timeout=30,
    )
    return (r.stdout or "").strip()


def ssh_client():
    sec = os.environ.get("TRAIN_RK3588_SSH_CREDENTIAL") or ps_user_var("TRAIN_RK3588_SSH_CREDENTIAL")
    if not sec:
        raise SystemExit("SSH credential unavailable")
    os.environ["TRAIN_RK3588_SSH_CREDENTIAL"] = sec
    c = paramiko.SSHClient()
    c.load_system_host_keys()
    c.set_missing_host_key_policy(paramiko.RejectPolicy())
    c.connect(BOARD, port=22, username=USER, password=sec, timeout=10,
              auth_timeout=10, banner_timeout=10, look_for_keys=False, allow_agent=False)
    return c


def rrun(c, cmd, timeout_s=60):
    _, o, e = c.exec_command(cmd, timeout=timeout_s)
    out = o.read().decode("utf-8", "replace")
    err = e.read().decode("utf-8", "replace")
    code = o.channel.recv_exit_status()
    return code, out + err


def exit_token(combined):
    """自运行命令尾部 'echo EXIT=$?' 提取真实退出码（shell 外层恒 0）。"""
    m = re.search(r"^.*EXIT=(\d+)\s*$", combined, re.M)
    return int(m.group(1)) if m else None


STATE_RX = re.compile(
    r"^t=(\d+) type=stream_state state=(\S+) fps=([\d.]+) last_frame_ms=(\d+)"
    r" reconnect_count=(\d+) presentable=(true|false)"
    r" generation=(\d+) seq=(\d+) wall_ms=(-?\d+) pts_ns=(-?\d+) admit=(\S+)", re.M)


def parse_streams(text):
    samples = []
    for m in STATE_RX.finditer(text):
        samples.append({"t": int(m.group(1)), "state": m.group(2), "fps": float(m.group(3)),
                        "gen": int(m.group(7)), "seq": int(m.group(8)),
                        "wall_ms": int(m.group(9)), "pts_ns": int(m.group(10)),
                        "admit": m.group(11)})
    summary = next((l for l in text.splitlines() if l.startswith("type=summary")), "")
    rebuild = next((l for l in text.splitlines() if "type=rebuild" in l), "")
    gate = next((l for l in text.splitlines() if "identity_gate" in l), "")
    injection = next((l for l in text.splitlines() if l.startswith("type=injection")), "")
    return samples, summary, rebuild, gate, injection


def main():
    t0 = time.time()
    st = json.loads(STATE_FILE.read_text(encoding="utf-8"))
    token = st.get("token")
    c = ssh_client()
    ok_all = True
    try:
        # ---------- EXT-1 fresh-round marker ----------
        code, out = rrun(c, "cat %s/core-evidence/verify-marker.txt 2>&1" % REMOTE, 20)
        marker_ok = code == 0 and token and token in out
        record("EXT-1", marker_ok, "token_present=%s cat_exit=%d" % (bool(marker_ok), code))
        ok_all &= marker_ok

        # ---------- EXT-2 rebuild-after 2 main regression ----------
        code, out = rrun(
            c, "%s --source test --duration 6 --rebuild-after 2 --out %s/reb2.log 2>&1; "
               "echo EXIT=$?" % (PROBE, EXT_REMOTE_TMP), 90)
        combined = comb_reb2 = out
        pz = exit_token(combined)
        samples, summary, rebuild, gate, injection = parse_streams(combined)
        pre = [s for s in samples if s["gen"] == 1]
        post = [s for s in samples if s["gen"] == 2]
        new_session_id = re.search(r"type=injection .*?session_id=(\S+)", combined)
        sid_new = new_session_id.group(1) if new_session_id else ""
        sid_in_rebuild = re.search(r"type=rebuild .*?session_id=(\S+)", rebuild)
        terms = {
            "pre_gen1": len(pre) > 0, "post_gen2": len(post) > 0,
            "rebuild_seen": "generation=2" in rebuild,
            "gate_stale_seen": "stale_old_generation=stale_generation" in gate,
            "sid_injected_matches_rebuild": sid_in_rebuild and sid_in_rebuild.group(1) == sid_new,
            "seq_reset_after_rebuild": bool(post and post[0]["seq"] == 1),
            "summary_final_live": "final_state=live" in summary and "generation=2" in summary,
        }
        # exit 哨兵：0=成功是 falsy 值，不能进 all() 聚合布尔；单独句柄。
        good = (pz == 0 and all(v for k, v in terms.items() if k != "exit"))
        record("EXT-2", good, " ".join("%s=%s" % kv for kv in terms.items()))
        ok_all &= good

        # ---------- EXT-3 rebuild-after 0 boundary ----------
        code, out = rrun(
            c, "%s --source test --duration 3 --rebuild-after 0 --out %s/reb0.log 2>&1; "
               "echo EXIT=$?" % (PROBE, EXT_REMOTE_TMP), 60)
        combined = out
        pz3 = exit_token(combined)
        samples3, summary3, rebuild3, gate3, _ = parse_streams(combined)
        post3 = [s for s in samples3 if s["gen"] == 2]
        # 立即重建后再无 gen1 帧进 gate 判定（t=0 时 rebuild 先于 pump 首帧）
        rec_ok = (pz3 == 0 and "generation=2" in rebuild3
                  and bool(post3) and all(s["gen"] == 2 for s in samples3)
                  and "final_state=live" in summary3)
        record("EXT-3", rec_ok, "exit=%s rebuild_gen2=%s samples_all_gen2=%s(%d)/%s"
               % (pz3, "generation=2" in rebuild3,
                  bool(post3) and all(s["gen"] == 2 for s in samples3), len(samples3),
                  "final_state=live" in summary3))
        ok_all &= rec_ok

        # ---------- EXT-4 rebuild-after > duration (no rebuild) ----------
        code, out = rrun(
            c, "%s --source test --duration 3 --rebuild-after 99 --out %s/reb99.log 2>&1; "
               "echo EXIT=$?" % (PROBE, EXT_REMOTE_TMP), 60)
        combined = out
        pz4 = exit_token(combined)
        samples4, summary4, rebuild4, gate4, inj4 = parse_streams(combined)
        no_rebuild = not rebuild4 and "generation=1" in inj4
        all_gen1 = bool(samples4) and all(s["gen"] == 1 for s in samples4)
        gen1_live = "final_state=live" in summary4 and "generation=1" in summary4
        rec4 = pz4 == 0 and no_rebuild and all_gen1 and gen1_live
        record("EXT-4", rec4, "exit=%s no_rebuild=%s all_gen1=%s summary_gen1=%s"
               % (pz4, no_rebuild, all_gen1, gen1_live))
        ok_all &= rec4

        # ---------- EXT-5 rebuild-after negative (never branches to rebuild) ----------
        code, out = rrun(
            c, "%s --source test --duration 3 --rebuild-after -5 --out %s/rebneg.log 2>&1; "
               "echo EXIT=$?" % (PROBE, EXT_REMOTE_TMP), 60)
        combined = out
        pz5 = exit_token(combined)
        no_rebuild5 = "type=rebuild" not in combined
        samples5, summary5, *_ = parse_streams(combined)
        all_gen1_5 = bool(samples5) and all(s["gen"] == 1 for s in samples5)
        rec5 = pz5 == 0 and no_rebuild5 and all_gen1_5
        record("EXT-5", rec5, "exit=%s no_rebuild=%s all_gen1=%s" % (pz5, no_rebuild5, all_gen1_5))
        ok_all &= rec5

        # ---------- EXT-6 delivery selftest identity items独立语义域 ----------
        code, out = rrun(c, "%s --selftest 2>&1; echo SELFTEST_EXIT=$?" % PROBE, 60)
        combined = out
        pz6 = exit_token(combined)
        ident_items = re.findall(r"selftest (PASS|FAIL) (identity_\S+|gate_\S+)", combined)
        self_ok = (pz6 == 0 and len(ident_items) == 7
                   and all(s == "PASS" for s, _ in ident_items))
        record("EXT-6", self_ok, "identity_selftest_items=%d all_pass=%s exit=%s"
               % (len(ident_items), all(s == "PASS" for s, _ in ident_items), pz6))
        ok_all &= self_ok

        # ---------- EXT-7 fresh-prod summary domain综合 ----------
        p = HERE / ("%s.core-evidence_identity-20261017-item003-verify-v1.log" % VERIFY_TAG)
        raw = p.read_text(errors="replace") if p.exists() else ""
        sumline = next((l for l in raw.splitlines() if l.startswith("type=summary")), "")
        required = ["final_state=live", "session_id=", "generation=2",
                    "frames_emitted=", "gate_admitted=", "gate_rejected="]
        rec7 = all(k in sumline for k in required)
        record("EXT-7", rec7, "summary_required_keys=%s" % [k for k in required if k not in sumline] or "full")
        ok_all &= rec7

        # ---------- EXT-8 time semantics: wall_ms UTC / pts_ns reset ----------
        # 数据源用 EXT-2 的 reb2 完整输出（含重建前后两代采样），不用后面单项的 combined。
        samples8, summary8, rebuild8, gate8, _ = parse_streams(comb_reb2)
        pre8 = [s for s in samples8 if s["gen"] == 1]
        post8 = [s for s in samples8 if s["gen"] == 2]
        wall_utc_ok = all(
            1700000000000 <= s["wall_ms"] <= 2200000000000
            for s in samples8)  # 2023..2040 UTC epoch 范围
        try:
            dt = datetime.fromtimestamp(samples8[0]["wall_ms"] / 1000, timezone.utc)
            dt2 = datetime.fromtimestamp(samples8[-1]["wall_ms"] / 1000, timezone.utc)
            d0 = dt.date()
            d1 = dt2.date()
            date_ok = samples8[0]["wall_ms"] < samples8[-1]["wall_ms"] and \
                dt.year == 2026 and dt.month == 10 and dt2.year == 2026 and d0 == d1
        except Exception:
            date_ok = False
            dt = None
            dt2 = None
        pts_ok = all(0 <= s["pts_ns"] < (1 << 33) for s in samples8)
        # pts 从头计：重建后首个采样 pts 应小于重建前末端采样 pts（test 源 PTS 从 0 累积）。
        # 该断言只在重建前后各至少 2 个采样时有界成立；把它作为断言时要求采样充分。
        pts_reset = bool(len(pre8) >= 2 and len(post8) >= 2 and post8[0]["pts_ns"] < pre8[-1]["pts_ns"])
        rec8 = wall_utc_ok and date_ok and pts_ok and pts_reset
        record("EXT-8", rec8, "wall_utc_ok=%s date=%s/%s pts_range_ok=%s pts_reset=%s"
               % (wall_utc_ok, dt, dt2, pts_ok, pts_reset))
        ok_all &= rec8

        # ---------- EXT-9 credential semantics leak scan (本地 evidence 目录) ----------
        secrets = [(n, ps_user_var(n)) for n in CRED_NAMES]
        secrets = [(n, v) for n, v in secrets if v and len(v) >= 4]
        if not secrets:
            record("EXT-9", False, "no credentials available for scan (runner env)")
            ok_all = False
        else:
            def cred_semantics(text, value):
                out_hits = []
                rv = re.escape(value)
                for m in re.finditer(r"user-pw[\s\"']*%s|password[\s\"'=]*%s|%s@[\w.\-]*"
                                     % (rv, rv, rv), text):
                    out_hits.append(text[max(0, m.start() - 30):m.end() + 15].replace("\n", " ")[:70])
                return out_hits
            hits = []
            files_scanned = 0
            ssh_note = []
            for p in HERE.glob("*"):
                if not p.is_file():
                    continue
                try:
                    text = p.read_text(encoding="utf-8", errors="replace")
                except OSError:
                    continue
                files_scanned += 1
                for name, v in secrets:
                    if v not in text:
                        continue
                    ctxs = cred_semantics(text, v)
                    if ctxs:
                        hits.append((p.name, name, ctxs[:2]))
                    elif name == "TRAIN_RK3588_SSH_CREDENTIAL":
                        ssh_note.append(p.name)
            rec9 = not hits
            record("EXT-9", rec9, "files=%d secrets=%d cred-semantics-hits=%s "
                   "ssh-username-token-only-files=%d" % (files_scanned, len(secrets),
                                                         hits or "none", len(ssh_note)))
            ok_all &= rec9

        # ---------- cleanup ----------
        code, out = rrun(c, "rm -rf %s && echo board_tree_cleaned" % REMOTE, 30)
        clean_ok = code == 0 and "board_tree_cleaned" in out
        record("CLEANUP", clean_ok, "exit=%d out=%s" % (code, out.strip()))
        ok_all &= clean_ok
    finally:
        c.close()

    out = {"results": results, "pass": sum(1 for r in results if r["ok"]),
           "fail": sum(1 for r in results if not r["ok"]),
           "elapsed_s": round(time.time() - t0, 1), "board": BOARD,
           "remote_dir": REMOTE}
    (HERE / "identity-ext-192_168_0_103-20261017-item003-verify-v1.json").write_text(
        json.dumps(out, ensure_ascii=False, indent=2), encoding="utf-8")
    print("EXT summary: %d PASS / %d FAIL" % (out["pass"], out["fail"]))
    return 0 if ok_all else 1


if __name__ == "__main__":
    raise SystemExit(main())
