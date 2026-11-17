#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""railhmi 视频流自动重连接线静态核对（现场断流修复）

源码级核对（无 Qt/GStreamer 环境可运行）：
  F-01 生产链路必须有真实重建入口：VideoRuntime 逐路调用 MultiStreamManager::request_restart。
  F-02 触发条件是「距上一帧时间」而非 last_error 文本（现场 UDP 静默停帧没有 bus ERROR）。
  F-03 单路独立 + 指数退避 + 不设总次数上限；出帧后复位退避。
  F-04 鉴权/凭据/格式/不支持类错误不自动反复重建（契约 §10）。
  F-05 启动失败的路（manager().started(i)==false）不参与重建，保留显式失败文案。
  F-06 worker 侧确有 stop(NULL/释放) → start(重建) 实现，不是只改状态。
  F-07 接口契约已登记该接线，不再把「无重建」写成现状。

退出码: 0 通过；1 发现缺陷；2 路径错误。
"""
from __future__ import annotations

import argparse
import os
import sys


def read(path: str) -> str:
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        return fh.read()


def main() -> int:
    ap = argparse.ArgumentParser(description="railhmi stream recovery wiring check")
    ap.add_argument("--root", default="app/src/ui")
    ap.add_argument("--media", default="app/src/media")
    ap.add_argument("--contract", default="docs/interfaces/INTERFACE_CONTRACT.md")
    args = ap.parse_args()
    if not os.path.isdir(args.root) or not os.path.isdir(args.media):
        sys.stderr.write("root/media not found\n")
        return 2

    runtime = read(os.path.join(args.root, "src", "video_runtime.cpp"))
    worker = read(os.path.join(args.media, "src", "camera_worker.cpp"))
    fails = []

    for token, what in {
        "kRebuildNoFrameMs": "no-frame threshold (F-02)",
        "kRebuildBaseBackoffMs": "base backoff (F-03)",
        "kRebuildMaxBackoffMs": "backoff cap (F-03)",
        "last_frame_ms_seen": "per-channel frame progress tracking (F-02/F-03)",
        "next_rebuild_ms": "per-channel rebuild deadline (F-03)",
        "rebuild_backoff_ms": "per-channel exponential backoff (F-03)",
        "rebuild_count": "rebuild attempt counter (F-03)",
        "request_restart(i)": "real pipeline rebuild call (F-01)",
        "video watchdog: rebuild": "rebuild audit log (F-03)",
        "classify_source_error": "non-retryable error classification (F-04)",
        "SourceErrorKind::AuthFailed": "auth failure excluded (F-04)",
        "SourceErrorKind::CredentialMissing": "credential failure excluded (F-04)",
        "manager().started(i)": "failed-start channel excluded (F-05)",
    }.items():
        if token not in runtime:
            fails.append("video_runtime.cpp missing %s (%s)" % (what, token))

    # 必须是「出帧复位」而不是「出错复位」：静默停帧没有错误文本可依赖。
    if "s.last_frame_ms > 0 && s.last_frame_ms != impl_->last_frame_ms_seen[i]" not in runtime:
        fails.append("watchdog is not driven by real frame progress (F-02)")
    if "impl_->rebuild_backoff_ms[i] = kRebuildBaseBackoffMs" not in runtime:
        fails.append("backoff reset on new frame missing (F-03)")

    for token, what in {
        "restart_req.exchange(false)": "restart request consumed in worker loop (F-06)",
        "stream->stop()": "pipeline torn down to NULL before rebuild (F-06)",
        "stream->start(&e)": "pipeline actually rebuilt (F-06)",
    }.items():
        if token not in worker:
            fails.append("camera_worker.cpp missing %s (%s)" % (what, token))

    if os.path.exists(args.contract):
        contract = read(args.contract)
        if "pump 只有 tick、无重建" in contract:
            fails.append("contract still claims no rebuild exists (F-07)")
        if "看门狗" not in contract:
            fails.append("contract does not document the watchdog (F-07)")
    else:
        fails.append("interface contract not found: %s" % args.contract)

    print("[check_stream_recovery_wiring] watchdog + real rebuild + non-retryable gate checked")
    if fails:
        for f in fails:
            print("[FAIL] %s" % f)
        print("[check_stream_recovery_wiring] FAIL (%d)" % len(fails))
        return 1
    print("[check_stream_recovery_wiring] PASS (F-01..F-07 wired)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
