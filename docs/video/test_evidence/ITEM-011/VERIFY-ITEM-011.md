# ITEM-011 录像压缩分支与滚动缓冲 — 软件验证 TestEvidence（VERIFY-20261017-v1）

- Artifact: TestEvidence
- Capability: `software_tester.software_verification`
- 被测交付: `video_systems_engineer.video_system_implementation` ITEM-011（基线 commit `9091b2c`，实现印章 `20261017-item011-r2`）
  — `app/src/core/{include/railhmi/core/compressed_ring.h, src/compressed_ring.cpp, include/railhmi/core/recording_source.h, src/recording_source.cpp}`、
  `app/src/media/{include/railhmi/media/recording_tap.h, src/recording_tap.cpp, include/railhmi/media/pipeline_factory.h, src/pipeline_factory.cpp, include/railhmi/media/camera_stream.h, src/camera_stream.cpp, tools/recording_tap_probe.cpp}`、
  `app/tools/board_recording_tap_check.py`、`app/tests/core/test_compressed_ring.cpp`/`test_recording_source.cpp`、
  `app/src/core/CMakeLists.txt`、`app/src/media/CMakeLists.txt`、`app/tests/core/{CMakeLists.txt, main.cpp}`
- 日期: 2026-10-17；stamp `20261017-item011-verify-v1`
- 目标: compute_host `192.168.0.103`（RK3588 / aarch64 / GStreamer 1.20.3）；host key `SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU`
- 结论: **TEST_PASS**（解码前压缩 tap、字节+时长双上限滚动、最旧淘汰计数、独立于显示分支、NVR 未配置显式回落，全部独立判据成立）
- 覆盖范围: core 滚动缓冲/来源策略纯逻辑 + media 压缩 tap 管线映射 + 板端真实压缩管线双阶段运行；
  **不含**真实相机 RTSP 压缩 tap 取证（ENV-D 交对应档）、片段固化/原子发布（ITEM-012）、provider/回放（ITEM-013）

## 1. 独立复跑（不采信实现方 r2 归档）

交付入口 `app/tools/board_recording_tap_check.py --keep-remote` 独立复跑（上传 226 files → 双 build → 双测试 → 双阶段运行 → 取证）：

| 步骤 | 结果 |
|---|---|
| build-core / core-tests | exit=0；**`==783 passed, 0 failed==`**（与实现方 r2 逐指标一致） |
| build-media / selftest | exit=0；**`selftest OK (0 failure(s))`**（9/9：tap 位置/h265 parse/record_sink 排序/显示分支保留/auto RTP tap/
  关闭无 record_sink/raw 不附加/来源回落/双上限） |
| run-rec（时长绑定，max_bytes=1 MiB） | REC_EXIT=0；acceptance 全 true result=PASS |
| run-rec-byte（字节绑定，max_bytes=200 KiB） | RECB_EXIT=0；acceptance 全 true result=PASS |

## 2. 独立解析比对（`_verify_compare.py`，10/10 PASS）

实现方 r2 归档 vs 验证 fresh 复跑；判据全部独立重推导，不采信交付自身 acceptance 行：

| Check | 结果 |
|---|---|
| semantics_impl-r2-dur / -byte（独立重推导双阶段语义） | PASS ×2 |
| semantics_verify-v1-dur / -byte | PASS ×2 |
| stable_source_acc_dur / _byte（source/acceptance 行跨轮逐字段恒等） | PASS ×2 |
| stable_config_dur / _byte（config 有效字段跨轮恒等：pre30/eff3/dur8/fps25/pull200） | PASS ×2 |
| received_window_dur（199 vs 200，|Δ|≤8） | PASS |
| received_window_byte（199 vs 199，|Δ|=0） | PASS |

独立语义判据（两轮四相各自推导）：
1. **双上限**：`bytes ≤ max_bytes`；`span_ms ≤ effective_pre×1000+100ms`（独立收紧到 +100ms，严于探针自身的 +1000ms 容差）。时长阶段 `span_ms=3000`（=clip_pre_seconds 精确）、字节阶段 `bytes=196093/199618 ≤ 204800`。
2. **淘汰守恒**：`pushed == packets + evicted_duration + evicted_bytes`（时长轮 200=76+124+0；字节轮 199=20+0+179）——两项上限计数分账不互串。
3. **阶段绑定**：1 MiB 阶段必有 `evicted_duration>0` 且 `evicted_bytes==0`（时长主导）；200 KiB 阶段必有 `evicted_bytes>0` 且 bytes 上探到 ≤98% 上限（字节主导 199618=97.7%）。
4. **录像率**：received ≥ 85% expected（199~200/200，源 25fps×8s）。
5. **显示独立**：display_frames（39，慢消费 200ms 拉帧）≪ received 的 1/3——显示慢未丢录像压缩包。
6. **来源显式**：requested=both + nvr_configured=false → `effective=local_fallback local=true nvr=false reason=NVR_UNCONFIGURED`。
7. **JSON/log 交叉**：records/expected/bytes/span_ms/evicted_duration 与 acceptance 布尔双渠道一致。

## 3. EXT 扩展夹具（`_verify_ext.py`，12/12 PASS；EX-C 板端 g++ 直编自有 C++ 夹具 34/34）

- **EX-A 独立参数轮**（`--pre-seconds 2 --max-bytes 524288 --duration 5 --display-pull-ms 150`）：
  config/source 回显逐字段核验（30/2/524288/5/25/150）、独立语义重推导全过（bytes=522807≤524288、
  span=2000ms=pre、守恒 124=51+60+13、display 32≪124/3）、acceptance PASS、exit=0。
- **EX-B CLI 负例**：`--bogus-flag` → exit=2 + `unknown arg: --bogus-flag`；`--help` → exit=0 含 `--selftest`/`--max-bytes`。
- **EX-C 板端 g++ 直编**（`vext_recording.cpp` 链接交付 `compressed_ring.cpp`+`recording_source.cpp`+`config.cpp`+`yaml.cpp`+`recording_tap.cpp`，34 判例全 PASS）：
  - 无限上限语义：v01/v02 双不限 50 包全驻留零淘汰全窗（span=490/32=Precise）、v03 仅时长不限。
  - 单包超宽：v04 超字节上限单包立即淘空（bytes 归零 evicted=1）、v05 后续正常包不被阻。
  - collect 闭区间：v06 两端含（[200,900] → 8 包）、v07 单点 [100,100]、v08 不相交空。
  - first_keyframe_at_or_after：v09 恰等边界、v10 之后首个、v11 跨缺、v12 尾部恰等、v13 空 -1。
  - 淘汰分账：v14 时长先淘汰且 evicted_bytes==0、v15 收紧字节后 bytes 分账启用（≤150）、v16 包数界。
  - 大窗口不变量：v17 逐包中断言界（bytes≤20000/span≤5000）与守恒恒成立；
    **v20 终态精确预言**（独立 Python 模拟逐出算法：packets=190/bytes=20000/span_ms=3780/keyframes=4
    （驻留 ts 16800/17600/18400/19200）/evicted_duration=0/evicted_bytes=810——板上逐值命中）；
    v20c 驻留窗 keyframe 查询命中 16800/17600。
  - ts 回退防护：v22 契约"假定单调不减"下回退不崩且界保持（观察语义，非强判据）。
  - RecordingTap 线程安全：v23 4 写+2 读并发 1600 包期间读线程不变量零违反、
    v24 终态 pushed=1600/bytes≤8000/span≤1500、v25 终态守恒。
  - 来源状态：v26/v27 RecordingTap 存取回显、v28~v30 integration_is_configured 两维独立、
    v31 Local 显式拒绝 NVR 即便已配置、v32 Both 双启、v33 NvrOnly。
- **EX-D 确定性**：EX-A 同参两轮（a1/a2）config/source/acceptance 稳定行逐字段恒等；
  rolling 包数 50/51（|Δ|≤8 窗内自然波动，界限与守恒判据全程成立）。
- **EX-E 凭据泄漏扫描**：本目录全部文件凭据语义形态 0 命中。
- **清理**：板端 `/tmp/railhmi_rec` 构建树、vext 构件、vexta 产物全部移除（`ls` 确认非残留）。

## 4. 夹具自查与修正记录（验证侧过程，非产品缺陷）

| 过程轮 | 现象 | 根因（均为验证侧） | 处理 |
|---|---|---|---|
| ext v1 | EX-A 首判 KeyError span_ms | parse_round 短键名（span/kf/first）与语义检查字典键（span_ms 等）不一致 | 键名统一，离线 py_compile 后上板 |
| ext v1 | EX-C g++ 首链失败 | `to_string(RecordingSource)` 在 config.cpp（夹具初版漏链）；补入后 yaml::Node::get 又缺 | 依依赖序补 `config.cpp`+`yaml.cpp`（ITEM-005 同源教训：静态库分层布局） |
| ext v2 | v20_big_window_kf_count 1 FAIL（31/1） | 验证侧预期构造错：把**全生命周期**推入的 keyframe 数（25）当**驻留窗口**属性 | 按交付逐出算法独立 Python 模拟求终态精确预言（190/20000/3780/4/0/810），v2 复跑 34/34 逐值命中 |

## 5. 观察项（非缺陷，交实现方知悉）

- **O-1 tap_records received ±1 波动**：合成源 25fps×8s=200 包，双轮 received=199~200（管线启动/停机采样边界）；探针掩盖判据自带 85% 容差，比对以 |Δ|≤8 窗判接收稳定性，非缺陷。
- **O-2 ts 回退语义**：`CompressedPacket.ts_ms` 契约声明"假定单调不减"；回退样本不崩溃、退出与字节数保持有界（v22），但回退会使 span/collect 的区间语义对负跨度失真。生产路径 PTS 由 GStreamer 保证单调，登记交 ITEM-012 固化窗口时注意输入侧单调前提。
- **O-3 keyframe_last 输出未消费**：探针输出 `keyframe_last` 仅作记录，acceptance 未纳入；无判据影响。

## 6. 结论

`CompressedRingBuffer`（字节+时长双上限、最旧淘汰、分账计数、collect 闭区间、关键帧对齐、并发安全）与
`RecordingTap`（线程安全接入、来源显式状态）在板端真实压缩管线与验证侧自有夹具下行为符合交付契约与设计
§2.3/§3.1/§4.2/§7.1；`record_sink` 编码 tap 位于 parse 之后、解码之前，显示/AI 分支保留原状；NVR 未配置回落
本机显式呈现（`reason=NVR_UNCONFIGURED`）非静默。**TEST_PASS**（基线 commit `9091b2c` 版本；范围=本项交付
core/media 层 + CLI/配置面；ENV-D 真实相机压缩 tap 取证另档）。

## 7. 记录文件

| 文件 | 说明 |
|---|---|
| `rec-192_168_0_103-20261017-item011-verify-v1.log` / `.transcript.json` | 独立复跑全步骤原始 transcript（783/0 + selftest 9/9 + 双阶段 PASS） |
| `rec-…verify-v1.core-evidence_recording-tap-….log` / `.json` | fresh 时长绑定阶段逐行输出 |
| `rec-…verify-v1.core-evidence_recording-tap-byte-….log` / `.json` | fresh 字节绑定阶段逐行输出 |
| `_verify_run.py` | 复跑驱动（PowerShell 用户级凭据导入 → 同进程交付入口） |
| `_verify_compare.py` / `_verify_compare_result.json` | 独立解析比对脚本与结构化结果（10/10） |
| `_verify_ext.py` / `_verify_vext.cpp` / `rec-ext-…-verify-v1.json` | EXT 夹具脚本、板端 C++ 源与结构化结果（12/12，EX-C 34/34） |
