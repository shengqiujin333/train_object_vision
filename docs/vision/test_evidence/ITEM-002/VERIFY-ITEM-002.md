# 视觉推理 ITEM-002 独立软件验证 — 帧租约消费与预处理

- Capability: `software_tester.software_verification`
- Work item: `TL-2253ff62-6914-4107-9b3f-56d1d6d04814-tasks-ITEM-002`
- 验证对象: `computer_vision_engineer.vision_inference_implementation` 输出 r4（commit 84483b6，stamp `20261019-item002-r4`）: `docs/vision/implementation/ITEM-002_PREPROCESS.md`
- 结论: **TEST_PASS**
- 验证轮 stamp: `verify-20261019-item002-v1`（board 192.168.0.103，主机指纹 `SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU`）

## 0. 验证方法

三段式独立验证（不采信交付 selftest/acceptance 行；判据独立重推导）:

- **V-RUN** 交付入口独立复跑: `app/tools/board_vision_preprocess.py --keep-remote`（277 文件上传 → CoreTests 构建与单测 → MediaCheck 构建 → selftest / fixture-bgr / fixture-nv12 / lease-source → 9 项证据取回）。运行两轮（同名 stamp 重发布覆盖，内容域判据两轮一致）。
- **V-CMP** 独立解析比对 oracle `verify/verify_item002_compare.py`（T1..T7）：退出码 token 重断言（外层 SSH shell 恒 0，解析 `*_EXIT` token）、selftest 行级语义与源判例计数、letterbox/BT.601/量化独立数学、租约描述符算术、core JUnit 判例名多重集恒等、凭据语义脱敏扫描。**对 impl r4 归档先零板成本预演**（`_dryrun_on_impl.py`：46/47，唯一失败为预期差异 keep-remote 无 cleanup），再上板。
- **V-EXT** 验证侧自有板端扩展夹具 `verify/vext_item002.cpp`（g++ 直编，链接交付 `build-media` 树三份静态库，不重编交付源），29 个判例（g01..g28 + g09b）。

## 1. V-RUN fresh 复跑结果

全部步骤成功（步退出 token 均 0）：env / mkdir / upload-app(277) / build-core / core-tests(TEST_EXIT=0) / build-media / selftest(SELFTEST_EXIT=0, SELFTEST_JSON_EXIT=0) / fixture-bgr(FIX_BGR_EXIT=0) / fixture-nv12(FIX_NV12_EXIT=0) / lease-source(LEASE_EXIT=0) / 9 项下载。`--keep-remote` → 无 cleanup 步骤（T1_keep_remote_no_cleanup PASS）。产物流 14 项见 `item002-compare-verify-20261019-item002-v1.json`。

## 2. V-CMP 独立比对（47/47 PASS）

| 组 | 判据（摘要） | 结果 |
|---|---|---|
| T1 | 7 步退出 token 独立重断言、keep-remote 无 cleanup、指纹 SHA256:、277 文件上传、9 项下载 | 11/11 |
| T2 | selftest 逐行 PASS（27 行，计数从探针源 `check(` 调用 28 处导出，run_source 空租约判例在 lease-source 日志）、RESULT: PASS failures=0、14 个判例域在场、与 impl r4 全文**恒等** | 4/4 |
| T3 | letterbox 独立数学（1280×720→scale 0.5/640×360/pad_y=140；900×500→pad_y=142；100×1→rh 6/pad_y=317；855×480→rh 359）；BGR/NV12 fixture 行逐字段 vs 独立数学；BT.601+量化通道均值独立重算（实测/期望差 ≤0.001）；BGR vs NV12 letterbox 一致；R>B+100 双路径 | 10/10 |
| T4 | 量化独立重推导：v=0→−128、v=255→127、pad 值 114→q=−14→byte 242（selftest 行锚定）、fixture vmin/vmax 域（bgr −128..127、nv12 vmax=126） | 5/5 |
| T5 | 真实租约行描述符算术（nv12 1280×720 plane0_stride=1280 plane1_offset=921600=1280×720）；空租约 NOT_READY；统计 prepared==frames==101、rejected=1（含 pre-loop 空租约自检）且 `reject_reasons NOT_READY=1` 交叉一致；p50≈22.7ms p95≈24.7ms；首帧 tensor 行 | 8/8 |
| T6 | core JUnit tests=1003 failures=0；判例名多重集与 r4 **恒等**（fresh=1003=impl）；视觉域判例名 62 个 ≥60；三条代表判例在场 | 5/5 |
| T7 | fresh 全产物凭据语义脱敏扫描（URI/__密码 pattern + 三个凭据 env 明文子串；`--user admin` 假阳性口径排除），11 文件 0 hits | 1/1 |

## 3. V-EXT 验证侧自有扩展夹具（29 判例全 PASS）

板端 g++ 直编 `vext_item002.cpp`（-pthread，链接 `librailhmi_{media,core,platform}.a` + gst/librga pkg-config；gst_init 必须——否则 `gst_video_frame_map` 段错 139）。全部 PASS（`vext-item002-...json`，EXIT token 0）：

- **结构性入口（TC-T-PRE-06/FI-AI-05 域）** g01–g03 空租约 invalid/NOT_READY/无张量/`rejected==1` 按 NOT_READY 计数；g17–g19 租约析构后 `plane_data` nullptr、无租约 store prepare → NOT_READY（不悬挂不伪像素）。
- **真实租约 librga 主链路** g04–g16：真实 GStreamer NV12 sample 经 FrameStore publish → FramePreprocessor.prepare；灰 Y=200 → 量化 72 精确（vmin/vmax 单值）；640×480→640×640 scale=1 pad_y=80；pad q=−14 与内容 q=72 混合均值 50.5 独立重算一致；内容行逐字节 72（张量为自有拷贝）；**旧租约像素在 store 重发布后逐字节保持**（无显示缓冲复用）；Y=100 → q=−28/pad −14；prepared==2；seq 1/2/session 透传；buffer_ref `fr-CAM-V-*` 逐帧不同。
- **publish 拒绝族** g20–g22：奇宽 NV12 在 FrameStore 层拒绝（odd_dimensions），不产租约 → prepare NOT_READY。
- **core 独立向量 + 转换器一致性** g23–g28：红色标定（Y=76, U=85, V=255）RGA→BGR 均值 R=254.0、B=G=0；core 参考转换同向量一致（RGA 254.0 与反量化参考 254.0 差 <8）；奇宽/stride 不足在参考路径显式拒绝。

夹具源 check 调用共 29 处，板上 PASS 行 29（V2_vext_count_matches_source，计数从源码统计不手抄）。
CLI 负例（V3）：`--bogus-flag`=2、无动作=2、`--help`=0。确定性（V4）：`--selftest` 同参两轮 stdout SHA256 恒等。板端产物泄漏扫描（V5）0 hits。

## 4. 工作项要求逐条核对

| impl 交付映射（impl 文档 §2） | 验证侧独立证据 |
|---|---|
| 从 FrameLease 取像素；唯一入口 prepare(const FrameLease&) | vext g02/g04–g06 + lease-source 首帧 lease 行；描述符算术独立核对（T5） |
| NV12 走 librga 转 BGR | vext g06/g23–g26（真实 librga blit 成功路径 + 一致性）；lease-source 101/101 |
| BGR→RGB 仅适配器内显式转换 | fixture R>B+100 双路径（T3e）+ vext g24（R=254 主导） |
| letterbox 等比与 pad 记录 | T3 独立数学四组 + vext g07/g08/g09b |
| dtype/scale/zp 量化 | T4 独立重推导 + vext g08/g13（精确量化值） |
| 禁 FrameRef/无显示缓冲复用 | vext g01–g03/g12（旧租约像素逐字节不变）/g17–g19 |
| 非法输入显式拒绝不静默丢帧 | selftest 拒绝判例族 + vext g20–g22/g27/g28 + reject_reasons 交叉（T5） |
| 合成图 + 带padding NV12 正确 | fixture-bgr/nv12 vs 独立 BT.601+letterbox+量化重算 ≤0.001（T3c） |
| core 单测 | 1003/0，与 r4 判例多重集恒等（T6） |

## 5. 观察（不影响 TEST_PASS）

- **O-1 文档口径偏差**: impl 文档 §1 称 selftest「24 判例」，探针源 `check(` 28 处（selftest 27 + run_source 1）、板上 PASS 27 行。文档数字过时（r1 期口径），建议交付侧改「selftest 27 判例」。
- **O-2 测试设计勘误（impl 已自报，独立复核成立）**: `VISION_TEST_DESIGN.md` TC-T-PRE-01 的 `pad_y=80` 数学不自洽（等比居中正确值为 140）；本验证用独立 letterbox 函数复现 140。
- **O-3 板端 librga 尺寸下限**: RGA blit 对 64×64 级小尺寸返回 `RGA_BLIT fail: Invalid argument`（raga_api 1.10.1），交付主链路 1280×720 不受影响；验证侧夹具因此取 640×480（同 ITEM-005 先例）。不属交付缺陷，未计入失败。
- **V-EXT 过程注记**: 首轮夹具 64×64 因 O-3 全链路 PRE_CONVERSION_FAILED（11 判例失败），驱动首轮误清了 keep-remote 树已重建；均为夹具侧问题，交付代码未改动。

## 6. 覆盖边界（未执行项）

- 真实 RTSP/文件源未在本轮验证（仅 test 源； impl 交付亦声明 RTSP 为后续 ITEM 接入）。
- RKNN 前向推理、4 路并发、occupied byte 精算、scale/精度基准（p50≈22.7ms 为当前实现值，非目标结论）。
- selftest 与本验证均未覆盖 RGA 硬件在 >2 路并发下的调度表现（ITEM-009/017 域）。

## 7. 证据索引

- 复跑 transcript/log: `vision-item002-192_168_0_103-verify-20261019-item002-v1.{log,transcript.json}` + 同 stamp 9 项 core-evidence 产物
- 比对 oracle: `verify/verify_item002_compare.py`、`item002-compare-verify-20261019-item002-v1.json`、预演夹具 `verify/_dryrun_on_impl.py`
- 扩展夹具: `verify/vext_item002.cpp`、`verify/verify_item002_vext.py`、`vext-item002-verify-20261019-item002-v1.json`
- 基线（impl r4）: `vision-item002-192_168_0_103-20261019-item002-r4.{log,transcript.json}` + 9 项产物
- 板端临时树（/tmp/railhmi_vision_item002_verify-20261019-item002-v1）已在 V-EXT V6 步骤确认删除
