#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""VERIFY-REPAIR-n008-v2 — file_manifest.txt 追加本轮证据条目（幂等，已存在则跳过）。"""
import io
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent.parent.parent  # docs/hmi/test_evidence/<dir>/ -> repo root

DIR = "docs/hmi/test_evidence/VERIFY-REPAIR-n008-v2"
ENTRIES = [
 ("VERIFY-REPAIR-n008-v2.md", "验证轮 v2 验证记录（修复轮 2 F-01/F-02 复核）：独立六页导航 0 QML 错误 + TestRun 670/0 与 repair-r4 逐字节一致 + stream-ext 40/40 + p3-ext 16/16 + NEG_F01 反例有效"),
 ("release-build-192_168_0_103-20261017-verify-v2.json", "验证轮 v2 R1 目标 Qt Release 构建归档（exit=0、ELF aarch64 944016B、smoke_exit=124 无 QML 错误）"),
 ("smoke-20261017-verify-v2.log", "验证轮 v2 R1 offscreen 装载冒烟日志"),
 ("build-TestRun-192_168_0_103-20261017-verify-v2.log", "验证轮 v2 R2 TestRun 独立复跑总日志（670/0 + 19 项 EXIT=0 全绿）"),
 ("build-TestRun-192_168_0_103-20261017-verify-v2.transcript.json", "验证轮 v2 R2 TestRun transcript"),
 ("build-TestRun-192_168_0_103-20261017-verify-v2.core-evidence_core-tests-20261017-verify-v2.log", "验证轮 v2 core-evidence core-tests 日志（PASS 670 行与实现方 repair-r4 identical）"),
 ("build-TestRun-192_168_0_103-20261017-verify-v2.core-evidence_core-tests-20261017-verify-v2.xml", "验证轮 v2 core-evidence JUnit XML（与 repair-r4 逐字节一致）"),
 ("build-TestRun-192_168_0_103-20261017-verify-v2.core-evidence_sim-inject-20261017-verify-v2.log", "验证轮 v2 core-evidence sim-inject 日志（逐字节一致）"),
 ("build-TestRun-192_168_0_103-20261017-verify-v2.core-evidence_sim-inject-20261017-verify-v2.json", "验证轮 v2 core-evidence sim-inject JSON（逐字节一致）"),
 ("build-TestRun-192_168_0_103-20261017-verify-v2.core-evidence_status-probe-20261017-verify-v2.log", "验证轮 v2 core-evidence status-probe 日志（逐字节一致）"),
 ("build-TestRun-192_168_0_103-20261017-verify-v2.core-evidence_status-probe-driver-20261017-verify-v2.log", "验证轮 v2 core-evidence status-probe-driver 日志（逐字节一致）"),
 ("build-TestRun-192_168_0_103-20261017-verify-v2.core-evidence_ai-probe-20261017-verify-v2.log", "验证轮 v2 core-evidence ai-probe 日志（逐字节一致）"),
 ("build-TestRun-192_168_0_103-20261017-verify-v2.core-evidence_alarm-probe-20261017-verify-v2.log", "验证轮 v2 core-evidence alarm-probe 日志（逐字节一致）"),
 ("build-TestRun-192_168_0_103-20261017-verify-v2.core-evidence_clip-probe-20261017-verify-v2.log", "验证轮 v2 core-evidence clip-probe 日志（逐字节一致）"),
 ("build-TestRun-192_168_0_103-20261017-verify-v2.core-evidence_replay-probe-20261017-verify-v2.log", "验证轮 v2 core-evidence replay-probe 日志（逐字节一致）"),
 ("build-TestRun-192_168_0_103-20261017-verify-v2.core-evidence_maintenance-probe-20261017-verify-v2.log", "验证轮 v2 core-evidence maintenance-probe 日志（逐字节一致）"),
 ("build-TestRun-192_168_0_103-20261017-verify-v2.core-evidence_permissions-probe-20261017-verify-v2.log", "验证轮 v2 core-evidence permissions-probe 日志（逐字节一致）"),
 ("build-TestRun-192_168_0_103-20261017-verify-v2.core-evidence_health-probe-20261017-verify-v2.log", "验证轮 v2 core-evidence health-probe 日志（逐字节一致）"),
 ("build-TestRun-192_168_0_103-20261017-verify-v2.core-evidence_fault-probe-20261017-verify-v2.log", "验证轮 v2 core-evidence fault-probe 日志（逐字节一致）"),
 ("board-ui-probe-probe-20261017-v2.json", "验证轮 v2 板上 UI 观察前提探针（JSON）"),
 ("board-ui-probe-probe-20261017-v2.md", "验证轮 v2 板上 UI 观察前提探针（文本）"),
 ("verify_nav_capture.py", "验证轮 v2 自有夹具：Xvfb 六页真实装载导航截屏（独立于评审工具；坐标按 Theme.qml 真值）"),
 ("ui-nav-transcript.json", "验证轮 v2 权威轮导航观察 transcript（构建 sha256/六页 CAP/app.log 4 行 0 QML 错误/WIN_UP=1）"),
 ("ui-nav-transcript-v1-drifted.json", "验证轮 v2 过程轮 v1（坐标漂移）留痕 transcript"),
 ("ui-nav-20261017-verify-v2-r3.log", "验证轮 v2 导航夹具过程日志（v1 漂移→坐标修正重跑）"),
 ("nav-app.log", "验证轮 v2 权威轮应用运行日志（4 行 INFO，chipName/TypeError/Cannot open 全 0 命中）"),
 ("nav-xvfb.log", "验证轮 v2 权威轮 Xvfb 日志"),
 ("p1_realtime.png", "验证轮 v2 权威轮 P1 实时监控截屏（1920x1080）"),
 ("p2_alarm_center.png", "验证轮 v2 权威轮 P2 报警中心截屏（含级别/录像可用性徽标）"),
 ("p3_replay.png", "验证轮 v2 权威轮 P3 历史回放截屏（三联标题 CAM-02/01/03 正确 + 关键帧 — 占位，F-01/F-02 修复特征）"),
 ("p4_all_cameras.png", "验证轮 v2 权威轮 P4 全摄像头截屏（六槽位+检测职责徽标）"),
 ("p5_ai_debug.png", "验证轮 v2 权威轮 P5 AI 调试截屏"),
 ("p6_maintenance.png", "验证轮 v2 权威轮 P6 设备维护截屏（权限矩阵/PTZ）"),
 ("nav-v1-drifted/p1_realtime.png", "验证轮 v2 过程轮 v1 漂移截屏留痕（p1，P1 正确）"),
 ("nav-v1-drifted/p2_alarm_center.png", "验证轮 v2 过程轮 v1 漂移截屏留痕（p2，P2 正确）"),
 ("nav-v1-drifted/p3_replay.png", "验证轮 v2 过程轮 v1 漂移截屏留痕（p3，P3 正确）"),
 ("nav-v1-drifted/p4_all_cameras.png", "验证轮 v2 过程轮 v1 漂移截屏留痕（实为 P5，坐标漂移）"),
 ("nav-v1-drifted/p5_ai_debug.png", "验证轮 v2 过程轮 v1 漂移截屏留痕（实为 P6）"),
 ("nav-v1-drifted/p6_maintenance.png", "验证轮 v2 过程轮 v1 漂移截屏留痕（y 越界仍停留 P6）"),
 ("nav-v1-drifted/nav-app.log", "验证轮 v2 过程轮 v1 应用日志留痕（同权威轮 0 QML 错误）"),
 ("nav-v1-drifted/nav-xvfb.log", "验证轮 v2 过程轮 v1 Xvfb 日志留痕"),
 ("verify_p3_ext.cpp", "验证轮 v2 新增 ext 夹具源：P3 数据域（registry 名称/关键帧 thumb 约定/sync_snapshot）"),
 ("run_p3_ext_fixtures.py", "验证轮 v2 p3-ext 夹具运行器（Paramiko 上传 + 板端 g++ 连 librailhmi_core.a）"),
 ("VERIFY-FIXTURES-20261017-verify-v2-p3ext.log", "验证轮 v2 p3-ext 板端运行日志（16/16 PASS + 清理确认）"),
 ("VERIFY-FIXTURES-20261017-verify-v2-ext.log", "验证轮 v2 stream ext 板端复跑日志（40/40 + fx1/fx2/fx3）"),
 ("VERIFY-FIXTURES-20261017-verify-v2-ext.transcript.json", "验证轮 v2 stream ext transcript"),
 ("build-CoreTests-192_168_0_103-20261017-verify-v2-core.log", "验证轮 v2 CoreTests 板端复跑总日志（p3-ext 前置重建，KEEP 重建）"),
 ("build-CoreTests-192_168_0_103-20261017-verify-v2-core.transcript.json", "验证轮 v2 CoreTests transcript"),
]


def main() -> int:
    manifest = REPO / "file_manifest.txt"
    lines = manifest.read_text(encoding="utf-8").splitlines()
    existing = set()
    for ln in lines:
        if "|" in ln:
            existing.add(ln.split("|")[0].strip())
    added = 0
    with manifest.open("a", encoding="utf-8", newline="\n") as fh:
        for name, note in ENTRIES:
            path = "%s/%s" % (DIR, name)
            if path not in existing:
                fh.write("%s | active | %s\n" % (path, note))
                existing.add(path)
                added += 1
    print("manifest added:", added)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
