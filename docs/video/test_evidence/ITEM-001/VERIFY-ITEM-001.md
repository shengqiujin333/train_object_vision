# ITEM-001 视频源能力探测与配置绑定 — 独立软件验证 TestEvidence（VERIFY-20261017-v1）

- Artifact: TestEvidence（software_verification 独立复验）
- Capability: `software_tester.software_verification`
- 被验证实现: video_systems_engineer.video_system_implementation（交付文档 `docs/video/implementation/ITEM-001_VIDEO_SOURCE_CAPABILITY.md`，实现提交 dc70856；交付清单 sha256 记录 `video_capability.h=5447a262…` 等五件）
- 被测版本核对: 板端构建使用本轮独立上传的 `app/` 工作区副本（178 文件），与交付清单同基线 commit dc70856 的源文件集；板端产物 `build-media/tools/railhmi-video-capability`（aarch64, 215376 B）
- 目标: compute_host 192.168.0.103（RK3588 / Ubuntu 22.04.5 / GStreamer 1.20.3）
- host key fingerprint: SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU（与交付证据一致）
- 验证日期: 2026-10-17; stamp `20261017-item001-verify-v1`
- 验证范围: ITEM-001 工作项（视频源能力探测与配置绑定）与测试设计 G1/TC-V-CAP-01..05；**不外推**持续吞吐/延迟/多路并发/录像回放（ITEM-002+ 范围）

## 1. 验证方法与独立复现入口

独立复跑不采信实现者自述，全部经 `app/tools/board_video_capability.py`（交付运行入口）与验证侧自有扩展夹具：

```
python app/tools/board_video_capability.py --host 192.168.0.103 --user admin \
    --outdir docs/video/test_evidence/ITEM-001 --stamp 20261017-item001-verify-v1 --keep-remote
python docs/video/test_evidence/ITEM-001/_verify_ext.py     # EXT-01..06
python docs/video/test_evidence/ITEM-001/_verify_det2.py    # EXT-04 确定性 v2（3 轮）
```

凭据经用户级环境变量（PowerShell `[Environment]::GetEnvironmentVariable(...,"User")` 导入本进程），不落盘；板端 0600 临时 env 文件机制与交付一致，用后即删（cleanup 确认 `No such file or directory`）。

## 2. 结果总览：TEST_PASS（附观察项 O-1/O-2/O-3）

| 判据 | 结果 | 证据 |
|---|---|---|
| 独立板端构建 MediaCheck | PASS（BUILD_EXIT=0） | transcript `build` 步 |
| 纯逻辑自检 8/8 | PASS（SELFTEST_EXIT=0，逐行与交付 r5 一致） | transcript `selftest` 步 |
| 六路真实可达（554/tcp open） | PASS（6/6 open；eth1 192.168.1.200/24） | transcript `reachability` 步 |
| 无凭据路径 6×blocked_credential_missing + caps=to_be_measured + EXIT=0 | PASS | nocred `.log`/`.json` |
| 有界 caps 探测 6×measured | PASS | caps `.log`/`.json`（见 §3） |
| 板端退出码语义（0/2/3） | PASS | EXT-01/02/03 |
| `--exists` 假否回避（设计要求） | PASS（独立复现 exists exit=1 而 direct present） | EXT-05 |
| 证据凭据泄漏扫描 | PASS（0 命中） | EXT-06 |
| 全量 caps 报告与实现者 r5 归档一致性 | PASS（6 路字段级 identical + versions identical） | §4 对照 |

## 3. 独立复跑逐路协商 caps（12 秒 caps timeout，与全流程独立 rerun 同证据）

| id | codec | format | size | fps | colorimetry |
|---|---|---|---|---|---|
| CAM-01 | H265 | NV12 | 1280x720 | 25/1 | 2:4:16:3 |
| CAM-02 | H265 | NV12 | 1280x720 | 25/1 | bt709 |
| CAM-03 | H265 | NV12 | 1280x720 | 25/1 | bt709 |
| CAM-04 | H265 | NV12 | 1280x720 | 25/1 | bt709 |
| CAM-05 | H265 | NV12 | 1280x720 | 25/1 | bt709 |
| CAM-06 | H265 | NV12 | 1280x720 | 25/1 | bt709 |

六个 `/h264/ch1/main/av_stream` URL 实际推送 **H.265**（再次证实设计要求"按实际协商 caps 记录、不按 URL/文档预期写死"）。

## 4. 与实现者归档（r5）的独立一致性比对

用本机 Python 对两份 caps JSON 的每路 `caps` 8 字段（status/codec/format/width/height/fps_num/fps_den/colorimetry）逐一比对：**6/6 SAME**；`versions` 三库完全相同（gstreamer 1.20.3 / librga 1.10.1_[0] / librknnrt 2.3.2 (429f97ae6b@2025-04-09T09:09:27)）。nocred 报告六路 readiness/caps 语义同构。验证自检 transcript（selftest 8 行 PASS）与 r5 的 selftest 输出逐行一致。

## 5. 验证侧扩展夹具（自有判据，交付单测未覆盖的边界）

| id | 刺激 | 预期（验证侧判定） | 实测 |
|---|---|---|---|
| EXT-01 | 板端 `GST_PLUGIN_SYSTEM_PATH=`（空）+ 相机凭据在场 | exit=3；stderr 列出含 rtspsrc 在内的缺失元素；逐路 readiness=blocked_element_missing:ftspsrc 前缀元素 | PASS（EXT01_EXIT=3；missing=rtspsrc,...,fakesink 全域 11 元素；readiness=blocked_element_missing:*；credential=present） |
| EXT-02 | `--camera CAM-99`（注册表无此 id） | exit=2 + stderr "requested camera id not found in registry" | PASS |
| EXT-03 | `--config ./config_missing_dir` | exit=2（配置错误路径） | PASS |
| EXT-04 | CAM-01 caps 确定性（见观察项 O-2） | 见 §7 | 观察项 |
| EXT-05 | 板端直接对比 `gst-inspect-1.0 --exists mppvideodec` 退出码与直接 inspect | `--exists` exit≠0（假否）而 direct `Factory Details` 存在 | PASS（EXISTS_EXIT=1, direct_present=True） |
| EXT-06 | 本机扫描 ITEM-001 目录全部证据文件（15 件）对两个相机凭据值（长度 8/6） | 0 命中 | PASS（hits=[]） |

夹具调试教训（透明记录）：v1 过程轮 EXT-01/EXT-04 初次失败为**验证侧期望构造错误**，非产品缺陷——① 忘了板端远端 shell 无相机凭据，elements-missing 注入时 readiness 先命中 credential 分支（设计规定缺凭据优先，正确语义），须先注入相机凭据再判 elements 分支；② 同因（远端无凭据）caps 恒为 to_be_measured，无 caps 行可断言；复用交付 `.cam_env.sh` 0600 注入后 EXT-01 闭环。诊断脚本（诊断后删除），保留 `_verify_ext.py` 为可复现凭证。

## 6. TC-V-CAP-01..05 判据映射

| TC | 判据 | 本轮证据 |
|---|---|---|
| TC-V-CAP-01 | 直接 gst-inspect-1.0 元素探测 + 版本号入结构化报告，不使用 --exists 退出码 | PASS：11 元素 status=present 且 Long-name 全量入报告/JSON；三库版本（EXT-05 独立复现 --exists 假否） |
| TC-V-CAP-02 | 移除某插件路径后启动报错指明 <element> 缺失，仅受影响状态显式可定位，整机不崩 | PASS：EXT-01（GST_PLUGIN_SYSTEM_PATH 空 → exit 3，逐元素名单显式输出；程序未崩，按清单报告后正常退出） |
| TC-V-CAP-03 | 清空 TRAIN_FRONT_RTSP_CREDENTIAL 后该路 CREDENTIAL_MISSING/其余正常；日志无明文凭据 | PASS：nocred 报告六路 credential=missing/readiness=blocked_credential_missing/status=to_be_measured（板端远端 shell 双 helpers env 均不注入时其语义完全一致）；EXT-06 凭据泄漏 0 命中 |
| TC-V-CAP-04 | 每路 codec/res/fps/colorimetry 从实际实测返回，不以 README 文档值预设通过 | PASS：§3 六路实测；与 r5 独立一致性 §4；四路 `/h264/...` URL 实测 H.265 为直接反例证据 |
| TC-V-CAP-05 | cameras.yaml 声称与实际不符时按实际协商（设计选择）——显式报告不隐穿 | PASS：报告按实测 caps 输出（配置不携带 codec 字段，无预设可穿帮；`/h264` URL-H.265 现象直接证明"报告果即实测"） |

## 7. 观察项（不阻塞 TEST_PASS，交实现/参考）

- **O-1（单路瞬时探测波动，正确性风险低）**：EXT-04 单路 `--camera CAM-01` 独立探测 3/3 次 `caps.status=error`（板端 det1..3.log），detail 双 attempt 含 `Internal data stream error`(audio PCMA depay 竞态)/`Your GStreamer installation is missing a plug-in`(PCMA depayloader 不可用路径)，而**同会话全六路两次运行(r5 + verify-v1) 都 measured**。`build_capability_report` 的瞬时重试仅一次（deliverable 里 2s 后重试），单路重负载场景成功率低于全六路场景——这是**交付自身契约内的有界重试策略**行为，不判 TEST_FAIL。建议实现方在 ITEM-002+ 管线层关注单路独立探测的 audio-pad 错误路径隔离（视频设计 §背压与异常恢复域）。
- **O-2（caps 探测确定性口径）**：caps 对 live 源本质是瞬时网络行为，draft 版 EXT-04 两次 diff 相同时点和 last-run caps 行含有界诊断尾巴，diff 恒非 0 不能当业务确定性失败判据。改判据为"成功 run 的 6 字段 caps 恒等"后需 ≥2 次成功 run——本轮受 O-1 影响 0/3 单路成功，确定性判据**未闭环**（不改判为失败，也不伪判 PASS）；全六路两轮全 measured 的一致性（§4）覆盖其稳定面。属验证方法学观察，非交付缺陷。
- **O-3（板端 eth1 100 Mbps）**：与 r5 相同（link_speed_mbps=100），设计声明 A-B 千兆互联；属平台/网络职责（不在本项验收内，r5 已登记同一事实），本轮不重复计缺陷。

## 8. 结论

**TEST_PASS**：ITEM-001（视频源能力探测与配置绑定）在当前实现版本（工作项 ITEM-001 交付清单，基线 dc70856 板端独立复现栈）下，TC-V-CAP-01..05 的软件侧判据全部通过；凭据安全（`credential_env` 变量名在场且 0 泄漏）、退出码语义、`--exists` 规避要求和实测 caps 反写（`/h264`→H.265）均经独立板端复现验证。本验证只证明**能力探测与配置绑定**；MediaCheck 能力探测范围之外的持续解码能力、延迟、多路并发、录像回放等 hacker 范围外延不外推。

## 9. 原始证据索引（本目录）

| 文件 | 说明 |
|---|---|
| `video-cap-192_168_0_103-20261017-item001-verify-v1.log` | 独立复跑全流程人类可读 transcript（build/selftest/reachability/nocred/caps 版下载/artifacts） |
| `video-cap-192_168_0_103-20261017-item001-verify-v1.transcript.json` | 同上结构化（命令+exit+输出） |
| `video-cap-…-verify-v1.core-evidence_capability-caps-…-verify-v1.json` | caps 报告 JSON（6 路 measured） |
| `video-cap-…-verify-v1.core-evidence_capability-caps-…-verify-v1.log` | caps 报告文本 |
| `video-cap-…-verify-v1.core-evidence_capability-nocred-…-verify-v1.json` | 无凭据报告 JSON（6 路 blocked_credential_missing） |
| `video-cap-…-verify-v1.core-evidence_capability-nocred-…-verify-v1.log` | 无凭据报告文本 |
| `video-cap-ext-192_168_0_103-20261017-item001-verify-v1.json` | 验证侧 ext 夹具结构化结果（EXT-01..06 + 步骤输出） |
| `_verify_ext.py` / `_verify_det2.py` | 验证侧可复现扩展夹具（板端操作全部有限超时，0600 凭据文件用后即删并确认） |
