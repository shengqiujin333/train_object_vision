# ITEM-012 软件验证证据（software_tester.software_verification）

- 工作项：ITEM-012 AI 调试视图数据与掩码叠加（视觉推理线）
- 验证对象：`docs/vision/implementation/ITEM-012_DEBUG_VIEW.md`（commit `3229e11`，
  impl 轮次 `20261021-item012-r4` 终轮）
- 验证轮次标识：`verify-20261019-item012-v1`
- 验证侧角色契约：独立验证与失败分析；不改产品源码；验证侧构造错误自行修正并留痕
- 环境：Paramiko SSH 192.168.0.103（admin，凭据环境变量 `TRAIN_RK3588_SSH_CREDENTIAL`），
  板端只写 /tmp，无相机连接（ENV-D）；core/media 在板端原生构建，无交叉编译
- 判据域：任务书验收——P5 每路 model_version/available/unavailableReason/fps/latencyMs/
  最近结果/置信度/距离/boxes/ROI 数据齐备；掩码叠加数据**仅在掩码真实可用时**提供；
  四个叠加开关**默认全关**；司机角色不产生任何 AI 调试数据；未知值占位 `—` **绝不格式化为 0**

## 结论（先读）

**TEST_PASS —— 三叉验证全绿：交付复跑逐步复现 + 离线 oracle 59/59（双向）+
板端自有扩展夹具 29/29。**

| 阶段 | 内容 | 结果 |
| --- | --- | --- |
| V-RUN | 交付入口 `board_vision_debug.py` 独立板端复跑（fresh，--keep-remote） | 全绿（15 步 exit=0，19 份证据取回） |
| V-CMP | 离线 oracle：fresh 轮对 impl r4 归档独立判据；板前 dry-run 对 impl r4 归档同套 59/59 | 59/59 PASS |
| V-EXT | 板端 g++ 直编自有扩展夹具 29 判例 + CLI 负例 8 + 开关端到端咬合 + 确定性 + 扫描 + 清理 | 14/14 PASS（夹具 29/29） |

被测版本：实现 commit `3229e11`（r4 终轮；r1 MediaCheck 编译失败、r2
EnvelopeBox→DetectionBox 类型不符 + `--overlays none` 被拒，均为实现侧过程留痕，
见 impl 证据）。验收语义逐条呈证：

1. **P5 字段齐备且未知≠0**：真实链路运行态 P5 行全字段（§2 impl 证据），V-CMP C6 对
   fresh 轮 chain JSON 独立深解析（model_version 双模型 / fps=5.0 / latency=141 /
   mask 1280×720 / positive∈(0,921600)）；V-EXT C07/C08 把 runtime fps=0 / latency=0
   显式压成 `—`（非 `0.0 fps` / `0 ms`）；C03 区分真 0（ready + 0 框 → `box_count_text="0"`
   且 `confidence_text="—"`）与未知。
2. **掩码叠加只认真实掩码**：V-EXT A01..A09 用自定义检测器经 `out.seg` 保活注入
   **真实 SegResult 掩码**走 worker 真实发帧链路——抓取使能才复制（A01）、内容逐字节
   等于注入（A02）、深拷贝独立于检测器缓冲（A03 改写 0xAA 不影响）、第二帧覆盖 + 
   updates=2（A04）、开关关 → `NOT_CAPTURED`（A05）、ready 无掩码 → `SEG_UNAVAILABLE`
   （A06）、stride<width 拒绝（A07）、检测器不可用原因透传（A08）、stride>width 整
   plane 拷贝（A09）。core 层 B01 呈证 **positive_pixels 只扫 width 不扫 padding**
   （padding=0xFF 时精确 6 而非 18）；B02/B03/B05 拒绝非法 plane（stride<width / 差
   一字节 / null 回退"掩码不可用"）；B04 恰好 stride*height 可用。
3. **四开关默认关 + 解析咬合**：V-EXT C10 describe 稳定序；V4 端到端——板端真实
   二进制 `--config ./config --role maintainer --overlays none --mask-demo` →
   `overlays_effective=false` 且无掩码对象；对照组 `--overlays mask` →
   `overlays_effective=true`、`overlays.mask=true`、`positive_pixels=131658`
   （1280×720 每 7 字节 1 正像素 = 131658 验证侧独立复算）。
4. **司机角色不可见**：V-CMP C4 对 fresh 轮 driver 配置 JSON 断言 `visible=false` /
   `channels=[]` / `overlays_effective=false`（即使四开关全开）；V-EXT B10 呈证
   registry 有相机而 channels 缺失时每路给"无掩码数据"原因（非崩溃/非 NOT_CAPTURED）。
5. **不可用原因显式**：V-CMP C3 对 unavailable 夹具断言全占位 + 非空原因 +
   非任何 0 形式；V-EXT C09 陈旧原因"推理陈旧"透传 + 全占位。

## 1. V-RUN 独立板端复跑（fresh 轮）

命令：交付入口原样调用，`--stamp verify-20261019-item012-v1 --keep-remote`
（cleanup 延后至 V-EXT V7，保证扩展夹具可在同一树直编）。

```
[env]/[mkdir]/[upload-app] 335 files / [upload-model] 双 rknn
[build-core] exit=0 / [build-media] exit=0
[core-tests] exit=0 TEST_EXIT=0        (JUnit tests=1412 failures=0，含 65 debug 判例)
[debug-selftest] exit=0 DEBUG_SELFTEST_EXIT=0   (17/17)
[fixture ×6] exit=0 FIXTURE_EXIT=0
    (fields / unavailable / mask_on / mask_off / no_mask / driver；每夹具 RESULT: PASS)
[config ×3] exit=0 CFG_EXIT=0
    (dev-all-overlays 4 判据 / driver-all-overlays 2 / maint-default-off 4)
[media-selftest] exit=0 MEDIA_SELFTEST_EXIT=0     (30 判例含掩码抓取门控 2)
[chain] exit=0 CHAIN_EXIT=0            (12s test 源 --debug-overlays mask 13/13：
    运行态 P5 available=true + 真实掩码 1280×720 positive=54867 updates=51 +
    停止后每路 available=false "AI 不可用")
[cleanup] 延后（--keep-remote）
downloads=19   transcript/log 已归档（文件名见 file_manifest）
```

## 2. V-CMP 离线独立比对 oracle（59/59）

脚本 `verify/verify_item012_compare.py`，对 fresh 轮独立断言并与 impl r4 归档逐项恒等
（`verify/item012-compare-verify-20261019-item012-v1.json`）。**先对 impl r4 归档板前
dry-run 一次（同脚本，stamp 视作 fresh → `item012-compare-20261021-item012-r4.json`，
59/59），证明判据非空转后才对 fresh 轮执行。** 分相：

- C00 transcript 步骤域 + 上传规模 335 + 模型资产
- C01 每步 `*_EXIT=0` token（15 个：build×2 / tests / selftest / 6 fixture / 3 config /
  media / chain）
- C02 清理语义由 `invocation.keep_remote` 推导（fresh 保留、impl 归档已清理）——
  同一套判据对两种归档成立
- C10..C12 JUnit 1412/0 + 65 debug 判例名精确域（多重集）+ fresh 与 r4 恒等
- C20..C22 selftest 17 判例名逐行恒等
- C3 六夹具语义独立重推导（mask_on positive=1024=width=64 步进 3 的独立复算 /
  unavailable 全占位且非任何 0 形式 / mask_off 无 mask 载荷 / no_mask
  SEG_UNAVAILABLE / driver 隐藏）
- C4 三配置口径（dev 合成掩码 positive=131658=1280×720//7 独立复算 / driver 隐藏 /
  maint 全关）
- C5 media selftest 30 + 掩码抓取门控 2 判例名
- C6 chain debug_view JSON 深解析（运行态：visible/available/model 双模型/fps/latency/
  mask 尺寸与正像素范围；停止态：每路 unavailable="AI 不可用"）
- C7 指纹 + 凭据泄漏扫描（短凭据照实标注）
- C8 7 份日志 `type=` 行 fresh vs r4 逐行恒等（发射稳定性）

## 3. V-EXT 板端自有扩展夹具（14/14，夹具 29/29）

`verify/vext_item012.cpp`（板端 g++ -std=c++17 -O1 直编，链 media/core/platform 三层
静态库 + pkg-config gstreamer-1.0/gstreamer-video-1.0）覆盖交付自测（core 65 debug
判例 + selftest 17 + media 30）未覆盖的边界。**关键新增覆盖：worker 真实掩码快照——
交付 media selftest 的 stub 检测器无掩码，只覆盖"无掩码→原因"分支；验证夹具用自定义
`MaskedDetector` 经 `out.seg` 保活注入真实 SegResult 掩码，走 GStreamer NV12
FrameStore::publish → FrameLease → VisionAiWorker submit/step 真线程直驱，在 worker
遥测面观测抓取/深拷贝/覆盖/拒绝四条链路；core 层 stride padding 正像素精确算术与
尺寸校验边界亦为交付单测未覆盖。**

驱动 `verify/verify_item012_vext.py`：

```
V0 fresh 保留树在场 PASS（三层静态库 + 双探针可执行）
V1 板端直编 3.1s exit=0（g_memdup 弃用警告，链接成功）
V2 29 判例全 PASS 且判例名集合与夹具源 check( 第二实参恒等（深度感知提取，src=29 board=29）
V3 CLI 负例：debug --bogus=2 / --help=0 / --fixture 无名=2 / 无参数 selftest PASS /
   --config --overlays mask,bogus=2 / --role superuser=2（解析只在 --config 分支咬合）
   / media --bogus=2 / chain --debug-overlays bogus=2（chain 模式校验先行）
V4 开关端到端咬合：--overlays none --mask-demo → overlays_effective=false 无掩码对象；
   --overlays mask --mask-demo → overlays_effective=true + overlays.mask=true +
   positive=131658（验证侧独立复算 1280×720//7）
V5 确定性：debug --selftest --json 同参两轮 sha256 恒等（584a9b7d97813983…）
V6 板端产物凭据语义扫描（SSH 凭据=admin 与登记用户名同名，文件级全串现禁用；按
   ITEM-002/004 既定口径 len≥10 才计全串命中；本轮 raw 命中即 cameras.yaml 假阳性，
   如实记录）
V7 通过后清理：vext 构件 + 整棵保留树，ls 确认无残留（CLEAN_DONE）
```

夹具域（29 判例）：

- **A01..A09 worker 真实掩码快照（GStreamer 真租约 + 自定义检测器）**：抓取使能 →
  READY/64×48/stride64/updates=1（A01）；快照内容 == 注入图案 phase=0 偶数下标 1
  （A02）；检测器缓冲事后改写 0xAA 快照不变——深拷贝独立（A03）；第二帧相位翻转 →
  plane[0]=0/plane[1]=1 + updates=2 最近一帧语义（A04）；开关关 → NOT_CAPTURED/无
  plane/updates=0（A05）；ready 无掩码 → SEG_UNAVAILABLE（A06）；stride 32<64 →
  SEG_UNAVAILABLE（A07）；检测器 down → 原因 `STUB_DOWN` 透传（A08）；stride 68>64 →
  整 plane 3264 字节拷贝含 padding（A09）
- **B01..B10 core 掩码门控深边界**：padding=0xFF 只扫 width → positive 精确 6 非 18
  （B01，逐行 stride 扫描的直接证据）；stride<width 拒绝（B02）；plane 差一字节拒绝
  （B03）；恰好 stride*height 可用（B04）；available+null plane → 回退原因"掩码不可用"
  （B05）；kind="custom" 透传（B06）；全 0 plane positive=0 是数值非占位且 JSON
  `"positive_pixels":0`（B07/B08）；全 1 精确 64（B09）；registry 有相机 channels 空 →
  每路"无掩码数据"（B10）
- **C01..C10 字段语义补充**：admin 全开关生效 + ROI 未配置原因（C01）；available 覆盖
  语义锚定——worker_alive=false 但输入路声明可用时以输入为准，调用方负责一致性
  （C02，登记为观察项非缺陷判据）；真 0 与占位区分（C03）；置信度两位小数 0.50
  （C04）；model_version JSON 转义 `a"b\c`（C05）；距离文本透传（C06）；fps=0 → `—`
  （C07）；latency=0 → `—`（C08）；陈旧原因透传 + 全占位（C09）；describe 稳定序
  `boxes,mask`（C10）

## 4. 验证侧返工留痕（本轮过程）

| 处 | 错误 | 修正 |
| --- | --- | --- |
| A04 检测器共享 | worker 以 `make_shared<MaskedDetector>(det)` 拿到**值副本**，
  测试侧 `det.set_phase(1)` 改的是原对象 → 第二帧图案不变，判据必败 | 全域改用单一
  `shared_ptr<MaskedDetector>` 由 worker 与测试共享，`set_phase`/`corrupt_buffer` 经
  共享指针调用 |
| A01..A03 生命周期 | `drive_masked(bool, MaskedDetector*, ...)` 里 worker 以裸指针构造
  `shared_ptr` → worker 析构即释放，测试随后 `corrupt_buffer` 是 use-after-free | 签名改
  `shared_ptr<MaskedDetector>` 传参（所有权共享） |
| B10 行数 | registry 固定 2 路，`build_debug_view` 每 registry 相机产 1 行 →
  `channels.size()==1` 期望错 | 改为 2 行全"无掩码数据" |
| C09 期望 | runtime fps=5.0 未清零，`fps_text` 恒 `5.0 fps` 而判据期望 `—` | runtime
  fps/latency 一并清零（占位来自"输入声明不可用"路径） |
| 夹具编译 | `infer(const AiFrame&)` 未限定名解析失败 → 抽象类无法实例化
  （`invalid new-expression of abstract class`）；`Role` 未 using 声明 | 改
  `media::AiFrame`；补 `using core::Role` |
| V3 media 负例 | `--selftest --debug-overlays bogus` 中 selftest 先行返回，负例不咬合
  （exit=0） | media 负例改 `--mode chain`（该校验在 chain 路径，exit=2）；debug 负例改
  `--config` 路径（`--role/--overlays` 只在 config 分支校验） |
| V3 管道退出码 | `... | tail -2; echo EXIT=$?` 捕获的是 tail 的退出码 | 改重定向到文件
  后 `echo EXIT=$?` |
| V4 JSON 形状 | config 模式 JSON 是 `{"view":...}` 包裹，且 Python bool 打印
  `False/True` 非 json 小写 | 取 `['view']` 后断言；字符串锚改 `False/True` |
| V4 格式串 | 5 个 `%s` 只传 4 参 → TypeError | 补参 |

过程错误均为验证侧期望/工具构造错；板端产品行为未出现误报（每处判据修正后即绿，
无一处需要改产品代码——产品缺陷计数为 0）。

## 5. 范围边界与移交

- ENV-D：本轮无相机连接，链路用 test 源 + 真实 RKNN；掩码来自真实分割输出
  （1280×720/54867 正像素），但真实相机取流下的掩码质量不在判据内
- P5 的 Qt/QML 渲染、配色、透明度、图层顺序与交互属桌面 UI 职责——本验证只覆盖
  **数据面**（`overlay_*` 布尔 + 掩码 plane 指针 + 占位文本）；未做实机渲染/人工视觉
  确认
- 6 路掩码叠加并发内存带宽（每帧约 0.9MB 拷贝 ×6）未评估；单路 CAM-01 呈证
- C02 观察项：`DebugChannelInput.row.available` 为输入权威（runtime 死但输入声明可用
  → 显示可用）；调用方（media 探针链路）已保证一致性，若上游失真无本层兜底——登记
  为观察项，非本轮缺陷
- 局部任务 TEST_PASS 仅证明 ITEM-012 与版本 3229e11 的已测范围
- 板端构建树/临时文件已全部清理（V7 CLEAN_DONE，`ls` 确认无残留）

## 6. 证据文件索引（本轮新增）

| 文件 | 用途 |
| --- | --- |
| `vision-item012-192_168_0_103-verify-20261019-item012-v1.{log,transcript.json}` | fresh 复跑全套 runner 留痕 |
| `vision-item012-192_168_0_103-verify-20261019-item012-v1.app_core-evidence_*`（17 份） | core-tests JUnit/selftest log+json/6 fixtures/3 config log+json/media log+json/chain log+json |
| `verify/verify_item012_compare.py` / `verify/item012-compare-verify-20261019-item012-v1.json` | V-CMP 59/59 |
| `verify/item012-compare-20261021-item012-r4.json` | oracle 板前 dry-run（对 impl r4 归档，非空转证明） |
| `verify/vext_item012.cpp` | V-EXT 夹具（29 判例，域 A/B/C） |
| `verify/verify_item012_vext.py` / `verify/item012-vext-verify-20261019-item012-v1.json` | V-EXT 驱动与结果（14/14） |
| `vext-item012-verify-20261019-item012-v1.stdout.log` | V-EXT 板端 stdout 留痕 |
