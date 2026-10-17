# 视频端到端测试计划（E2EPlan）

- Artifact: E2EPlan
- Capability: `software_tester.software_test_design`
- 日期: 2026-10-17
- 伴生: `docs/video/VIDEO_TEST_DESIGN.md`（本文不重复用例细节，只定义完整旅程与升降级门）
- 上游: `docs/video/VIDEO_SYSTEM_DESIGN.md`、`docs/interfaces/INTERFACE_CONTRACT.md` v1.0.0、`docs/hmi/E2E_PLAN.md` v2

---
## 1 环境分层（对齐 VIDEO_TEST_DESIGN §2）

ENV-A0（主机 core/media 单测）→ ENV-B（板端 gstreamer/librga 真库 + 本地 mp4 离线媒体注入）→ ENV-C（板端 file/test 源六路）→ ENV-D（板端真实 RTSP 六路）。
每条 E2E 旅程注明其最低环境档："ENV-A0 通过是 ENV-C 的前置条件；ENV-C 通过是 ENV-D 的前置条件。不跨档外推。"

---
## 2 E2E 旅程

### E2-V-01 单路取流到显示呈现（file 源）— ENV-C
准备: 板端构建后 + cameras.yaml 指定 file 源。刺激: 启动应用 → P1 观察。观测: 30s 内出帧、presence stable、lease 释放、no crash，帧率符合 display_fps。判据: 六态机 connecting→live；`may_present_as_live` 判真。清理: 退出应用；杀 feeder；归档日志。证据: `docs/video/test_evidence/E2-V-01/*.json|md|log`。
### E2-V-02 六路并发负载（ENV-C → ENV-D）
准备: 六路同源类型文件（ENV-C）或真实 RTSP（ENV-D）。刺激: 全六路同开 5 分钟，观察 FI-V-05 背压注入、FI-V-02 断流注入于第 3 分钟。观测: 每路 fps、CPU/RSS/温度采样、断流一路后其余五路帧率不跌 >10%、恢复时长记录。判据: 单路失败隔离 + 无阻塞传播；数值记录为基准。
### E2-V-03 保护片段闭环（ENV-C → ENV-D）
准备: 触发一次报警（用 FI 序列或 UI 按钮模拟 trigger）。刺激: ClipRequest 一次到位。观测: 片段生成（ffprobe 时长 ±1s）、actual_t0/actual_t1 上报、缺口记录正确；Transient/partial/unavailable 三态呈现。判据: 无 .json 冒充片段；幂等 request_id 行为按 TC-V-CLIP-01。
### E2-V-04 故障马拉松（ENV-C）
准备: FI-V-01..07 顺序注入，每类 5 次，间隔 30s。观测: 无内存增长（RSS 5min 内 <5% 变化）、无 fd 泄漏（`/proc/self/fd` 稳态）、无响应丢失、各计数器准确累计。判据: 马拉松后 apply 退出码 0 + DB 一致性 spot-check。
### E2-V-05 时延与录播同步（ENV-D）
准备: 六路真实源。刺激: 触发回放三联同屏。观测: `max−min` 偏差、latency p50/p95 分布归档。判据: 分布记录为主（PRD §12 指标未冻结）；只在"无法得到分布"时判 fail。
### E2-V-06 脱敏审计（ENV-B/C/D 全档）
准备: 运行任一来回，包含一次 AUTH_FAILED。刺激: 启动+正常运行+一次鉴权失败。观测: 全日志 grep 凭据/含鉴权 URL/pipeline str。判据: 零命中；按 TC-V-LOG-01。

---
## 3 升降级准则

- 升级: 上一档全绿可进入下一档；ENV-D 前需 E-10 凭据核验记录。
- 降级: ENV-D 前置缺口时常常调查记录为"待执行"，不标 fail 不标 pass。
- 证据: 按 VIDEO_TEST_DESIGN §7 命名归档；三源（真实/文件/模拟）不合并。

---
## 4 版本

- v1: 初版对齐 video test design v1，六条 E2E 旅程。
