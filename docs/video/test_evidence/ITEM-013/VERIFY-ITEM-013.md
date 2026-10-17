# ITEM-013 录像 provider 与回放媒体链路 — 独立验证证据（TestEvidence, VERIFY-20261017-v1）

- Artifact: TestEvidence（软件验证轮独立复跑与扩展夹具证据）
- Capability: `software_tester.software_verification`
- 工作项: 视频 TASK ITEM-013（录像 provider 与回放媒体链路）
- 被测实现版本: commit `0630a85`（实现 stamp `20261017-item013-r2`；基线 `f9c95fb`）
- 日期: 2026-10-17
- 目标: compute_host `192.168.0.103`（RK3588 / aarch64 / Ubuntu 22.04 / GStreamer 1.20.3）
- host key fingerprint: `SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU`（与实现轮一致）
- 验证入口: `app/tools/board_media_provider_check.py`（deliverable runner；`--keep-remote` 独立复跑）+ `_verify_ext.py`（自有板端扩展夹具）+ `_verify_compare.py`（独立解析比对）

## 1. 覆盖范围与版本核对

- 实现交付共 13 个文件（`docs/video/implementation/ITEM-013_MEDIA_PROVIDER.md` §1）；本轮 git 状态仅新增验证侧文件，未触碰产品文件（core `recording_provider.cpp`、media `media_provider.cpp`、probe `media_provider_probe.cpp` 与 impl 归档逐字节同源——实现交付 run 使用同一 app/ 树下 build）。
- 上游阅读：`docs/interfaces/INTERFACE_CONTRACT.md` §8 IF-07（provider 方法族/status/reason/实际 decoded position/NVR unconfigured/「ok 空列表≠可播放」）、`docs/video/VIDEO_SYSTEM_DESIGN.md` §7.2、`docs/video/VIDEO_TEST_DESIGN.md` TC-V-PLY-01/02、历史教学(memory)：实际解码 position 判据按「怨五轮 act_rel spread ≥1000ms」在两侧轮复现。
- 结论范围：**provider 三态 status/reason、真实媒体可解码判定、实际解码 position、导出真实路径/大小、媒体生成缩略图、NVR 未配置显式不可用不模拟填补、以实际播放头同步**。真实相机 RTSP 片段回放（ENV-D）与四跳转/时间轴 UI（桌面 ITEM-020）、DB 片段索引读写（平台层）不在本项边界——实现文档如实声明，本验证同口径。

## 2. 独立复跑（不采信实现 r2 归档）

驱动：`_verify_run.py`（PowerShell 用户级凭据导入 → 同进程交付 runner；全部步骤有限超时）。

| 步骤 | 结果 |
|---|---|
| upload | 240 files → `/tmp/railhmi_prov` |
| build-core → `railhmi-core-tests` | **822 passed / 0 failed**（含 `recording_provider` 组；PASS 判例行 822 逐行与 impl r2 **逐行 identical** + name-set 比对 0 diff） |
| build-media + `railhmi-media-provider --selftest` | **8/8 PASS**（SELFTEST_EXIT=0；与 impl r2 selftest 恒等） |
| run-provider（板端真实压缩管线 Молeva：真实 MPEG-TS 生成→query/play/export/thumbnail/NVR/sync） | acceptance `result=PASS`（13 旗全 true） |
| 下载 4 件证据 | log/json/.ts/.jpg 成功 |

## 3. 独立语义比对（`_verify_compare.py`，11 checks 全 PASS）

不采信交付 acceptance 行，独立重推导两轮（impl r2 vs verify-v1）：

| 判据 | 结果 |
|---|---|
| semantics_impl-r2 / semantics_verify-v1 | PASS（clip availability=complete+size>0 / query 五 case 三态（complete ok+covered≥窗−100 / empty「ok 空 segments=0」/partial covered≈1000 / gaps reason 非空 / expired error=TIMEOUT）/ play 五轮 ok+reached+frames>0 + act_rel 前四同关键帧 ±100 + 第五轮差 ≥1000（实际解码 position 判据）+ |actual−req|≤2000/行 + play kv 键集闭包 / export ok+size 对齐源 ±15% / missing=unavailable/NOT_FOUND / thumbnail .jpg exists / nvr unavailable/UNCONFIGURED segments=0 / sync close≤300 in_sync=true + spread>300 in_sync=false+prompt 含 2000 + in_sync==(dev≤300) 一致性 both / acceptance 13 旗全 true / **JSON-日志交叉**（clip/query/play/export/thumbnail/nvr/acceptance 七处）） |
| stable_acc / stable_nvr / stable_export_missing / stable_thumb | PASS（跨轮逐字段恒等） |
| stable_play_status_series | PASS（五轮 ok/true 系列恒等） |
| play_actual_mode | PASS（act−req 模式 [245,145,45,−1855,−1855] vs [245,145,45,−1855,−1335]——头三/四轮关键帧落点恒等，第五轮换关键帧；请求接受≠实际解码立场两侧一致支持） |
| play_frames_window | PASS（impl [14,14,14,14,14] vs verify [14,14,14,14,2]——逐轮 frames>0 且差≤15；末轮 2 帧=播放窗刚好落段尾部，属自然帧计数窗波动非判据违例） |
| export_size_window | PASS（1328972 vs 1334988，|Δ%|=0.45% ≤ 15%（x264enc 码率自然波动窗）） |
| covered_ms_window | PASS（complete 4960 == 4960 值恒等） |
| 真实媒体独立核对（两侧） | `.ts` size%188==0，包全 0x47 同步（impl 7069/7069、verify 7101/7101）；缩略图 JPEG `ffd8ffe0`（60861 B / 60627 B）媒体生成非占位 |

实现 r2 归档 .ts/.jpg 与 impl 侧 byte 细节差异（size 0.45% / thumb 0.4%）属编码器非决定论与同表象一致；已登记不按字节恒等判。

## 4. 扩展夹具（`_verify_ext.py`，12 checks PASS（含 vext 42/42））

| EX | 内容 |
|---|---|
| EX-A | 独立参数轮（pre=2/post=3/warmup=6/extra=3，run_s=8）：**参数无关重推导**——clip 行 size>0 或 incomplete（交付显式短缺标注非缺陷），partial/empty/gaps/expired 查询语义不依赖 availability 标签独立核验（partial covered∈(0,2000] + status=partial；empty=ok+segments=0；gaps reason=SYNTH_GAP；expired=error/TIMEOUT）+ complete query covered==net±100+coverage_complete 按实际窗独立推导 + play 五轮 reached/frames/dev≤2000/actual∈[t0−1000,t1+1000] + export/nvr/sync 全判据。EX-A 本轮 t1−t0=3960 < 窗口 5000 → 交付显式标 incomplete（POST_ROLL 1 帧关键帧粒度短缺——ITEM-012 教训同源），partial 查询仍精确为 partial/covered=1000、empty 仍 ok/0、**「查询成功无覆盖≠可播放」语义与 availability 独立成立**。 |
| EX-B | CLI 负例 2 例：`--bogus-flag` exit=2+`unknown arg` 行、`--help` exit=0 含 `--selftest` 用法行 |
| EX-C | 板端 g++ 直编 `_verify_vext.cpp`（链接交付 `recording_provider.cpp`）42/42 PASS：adjacent/touching/chain/clipped/outside/empty/neg-window/乱序/零长/全不可用 covered 并集数学、未配置优先+不展开候选+不吐缺口、≥窗/恰窗/差 1ms/混合可用/unavailable+NOT_FOUND、半开窗左右边界、缺口三重过滤+ok 无 reason、deadline 严格大于/含等+/0/负值不超时、7 个 reason token 稳定字符串、parse 4 态 roundtrip+reject 3 变体、PlaybackResult/ExportResult 默认 −1/false/0 |
| CLEAN | 板端 `/tmp/extA` `/tmp/vext_provider` `vext_recording_provider.cpp` 三件清理 + 二次 `ls` 「No such file or directory」确认 |
| EX-D | 凭据泄漏扫描（本目录全部非 `_` 文件——front/side 相机凭据+SSH secret 语义命中 0） |

**观察项 O-1（登记，非缺陷，交实现方参考）**：`thumbnail()` 在 `media_provider.cpp` `gst_element_seek_simple` 与 `play_segment` r1 修复同类形态（预滚完成前 seek）——但缩略图验收判据为「媒体生成的可读引用」（TC-V-PLY 从未断言 at_ms 位置精度），`rel>0` 时 seek 是否生效不属当前 TaskItem 判据域：多轮复跑均产出有效非空 JPEG。如果后续 item 要求时间定位缩略图，此时该 seek 时序需要审核。

## 5. 过程轮与教训（验证侧自负）

- v1 过程轮三处夹具构造错全部自查后修正，无产品缺陷误报：① `_verify_compare.py` PLAY_KEYSET 误含 `type`（play_lines_kv 剥 type= 前缀后无 type 键——ITEM-019 剥 prefix 教训相反面同源），归档 dry-run 零成本抓出；② EX-A 首选 complete 硬性预期在参数轮 warmup=6 下遭交付显式 incomplete 标注（关键帧 1s 粒度短缺 POST_ROLL_SHORTAGE——ITEM-012 教训：**可用性必须按窗口+容差重推导**），parâmetro 无关重推导修正；③ `_verify_vext.cpp` 首版缺口插入时掉 `}`，g++ 定点直报。
- 独立 compare 判据核心判据按 10 项深度独立重推导（query 五态/play/position 语义/export/thumbnail/NVR/sync 数学 + JSON-日志交叉），两侧轮归档全部成立。

## 6. 结论

**TEST_PASS**。被测版本（commit 0630a85，实现 stamp 20261017-item013-r2）在独立复跑与扩展夹具下满足任务书全部用户可观察判据：

1. provider 方法族四态：无覆盖 `ok` 空列表（**「查询成功无覆盖≠可播放」语义双侧独立成立，与 availability 无关**）；部分覆盖 `partial`； имеющи重不可用 `unavailable/NOT_FOUND`；deadline 过期 `error/TIMEOUT`。
2. **实际解码 position**：`requested` rel 400..2500 得同一关键帧落点（±100ms 一致 spread），rel 4500 换关键帧（差≥1000ms 两侧轮成立）——回报的是真实解码 PTS 而非请求接受，|actual−req|≤KEY_UNIT 容差（≤2000ms）。
3. 缺口如实返回：`gaps=1 reason=SYNTH_GAP`；覆盖并集/裁剪/半开窗/token 死锁在 vext 42 判例全绿。
4. 导出返回实际路径/大小（±0.45% 与源真实 .ts 一致，size%188==0 全 0x47 同步）；源缺失显式 `unavailable/NOT_FOUND`。
5. 缩略图为媒体生成可读 JPEG 引用（两侧 ff d8 ff e0）。
6. NVR 未配置 → query/play/export 全 `unavailable/UNCONFIGURED`+segments=0（**不生成模拟片段填补**，双侧轮一致）。
7. 同步按实际解码播放头：close 组 in_sync、spread 组 max−min=2000>300 → `in_sync=false` + 显式提示（`同步偏差 2000 ms（阈值 300 ms）`）+ `in_sync == (max_deviation<=300)` 一致性 both。
8. 显式边界（与实现一致，非外推）：真实相机 RTSP 片段回放（ENV-D 现场取证）、四跳转/时间轴 UI（桌面 ITEM-020）、DB 片段索引读写（平台层）未在本项覆盖；缩略图 locate 精度登记 O-1 交实现方参考。

## 7. 原始证据文件（本目录）

| 文件 | sha256(16) | 内容 |
|---|---|---|
| `prov-192_168_0_103-20261017-item013-verify-v1.log` | `99e705e03804e23c` | 验证轮板端独立复跑全 transcript |
| `prov-192_168_0_103-20261017-item013-verify-v1.transcript.json` | `1948d5f98d52c313` | 结构化 transcript |
| `prov-…verify-v1.core-evidence_media-provider-…verify-v1.log` | `97baaece1d913cf4` | fresh 片段/query/play/export/thumbnail/NVR/sync/acceptance 逐行 |
| `prov-…verify-v1.core-evidence_media-provider-…verify-v1.json` | `4a34e25264f977d5` | fresh provider 汇总 JSON |
| `prov-…verify-v1.core-evidence_prov_exports_CAM-01_evt-200-CAM-01.ts` | `0972f315a2a0d1b7` | fresh 真实 MPEG-TS 导出片段（1334988 B，只读归档） |
| `prov-…verify-v1.core-evidence_prov_thumbs_cam01_00000.jpg` | `05fd362b15fbac5b` | fresh 媒体生成缩略图（60627 B，JPEG） |
| `_verify_compare.py` / `_verify_compare_result.json` | — | 独立比对器与结构化结果（11 checks PASS） |
| `_verify_run.py` | — | 验证侧独立复跑驱动 |
| `_verify_ext.py` / `_verify_vext.cpp` | — | 自有扩展夹具与 C++ 源（42 判例） |
| `prov-ext-192_168_0_103-20261017-item013-verify-v1.json` | `8f56e6e6212756c2` | EXT 结构化结果（12 checks、vext 42/42） |
