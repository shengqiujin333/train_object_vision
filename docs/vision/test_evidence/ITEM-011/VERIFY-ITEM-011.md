# ITEM-011 软件验证证据（software_tester.software_verification）

- 工作项：ITEM-011 距离测量（视觉推理线，可选后置）
- 验证对象：`docs/vision/implementation/ITEM-011_DISTANCE.md`（commit `7cf3a4e`，r3 终轮）
- 验证轮次标识：`verify-20261019-item011-v1`
- 验证侧角色契约：独立验证与失败分析；不改产品源码；验证侧构造错误自行修正并留痕
- 环境：Paramiko SSH 192.168.0.103（admin，凭据环境变量 `TRAIN_RK3588_SSH_CREDENTIAL`），
  板端只写 /tmp，无相机连接（ENV-D）；core/media 在板端原生构建，无交叉编译
- 判据域：任务书验收——仅带标定（含变焦档位）时由人员框像素高换算 `distance_m`；
  未标定/档位未知恒 `-1`（未知占位 `—`，非 0）；公式公开可核对
  `distance = ref_m * ref_h_px / px`（锚点 CAM-01 500 m/89 px → 常数 44500 px·m）；
  不假设规则引擎消费距离

## 结论（先读）

**TEST_PASS —— 三叉验证全绿：交付复跑逐步复现 + 离线 oracle 73/73 + 板端自有扩展夹具 39/39。**

| 阶段 | 内容 | 结果 |
| --- | --- | --- |
| V-RUN | 交付入口 `board_vision_distance.py` 独立板端复跑（fresh，--keep-remote） | 全绿（188 步 exit=0，18 份证据取回） |
| V-CMP | 离线 oracle：fresh 轮对 impl r3 归档独立判据（板前 dry-run 同 72/72，收尾 C8 后 73/73） | 73/73 PASS |
| V-EXT | 板端 g++ 直编自有扩展夹具 39 判例 + CLI 负例 5 + 语义反转负例 + 确定性 + 扫描 + 清理 | 12/12 PASS（夹具 39/39） |

被测版本：实现 commit `7cf3a4e`（r3 终轮；r1 MediaCheck 编译失败、r2 form-no-calib 步骤
漏传 `--calib` 回退 selftest，均为实现侧过程留痕，见 impl 证据）。验收语义逐条呈证：

1. **带标定（含档位）才换算，且输出精确等于公式**：锚点 89px→500.0 m、178px→250.0 m
  （4 形态 + fresh 复跑恒等）；media selftest 36px→1236.1111 m；V-EXT A1/A2 用真实发帧
   worker 链路（372px 图像 74.4px 框）→ 44500/74.4 ≈ 598.118 m 文本 `598.1 m`。
2. **未标定 / 档位未知恒 -1，debug 文案 `—` 永非 0**：4 形态中 no-calib（CAM-09）与
   zoom-unknown 两者 `distance_m:-1` + `type=debug text=—`；V-EXT A3/A4 worker 真实链路
   `distance_unknown=1 ZOOM_UNKNOWN`、文本 `—`、`last_distance_m=-1`。
3. **门控顺序固定且互斥**（同请求只产一个 reason，不越门提示）：V-EXT B1..B6 呈证
   NOT_PERSON→NO_CALIBRATION→ZOOM_UNKNOWN→ZOOM_MISMATCH→INVALID_BOX→OUT_OF_RANGE
   优先序（如非 person + 档位未知 → NOT_PERSON 而非 ZOOM_UNKNOWN）。
4. **超范围不 clamp**：40px→1112.5 m `OUT_OF_RANGE` 且 `meters=-1`（不是 600），
   C4 呈证；0=不限与单边限定语义 C5/C6。
5. **规则消费假设**：core-tests `distance_not_consumed_by_rules` 判例在 impl 域 + fresh 域
   双侧捕获比对（V-CMP C1 精确 47 判例名域），本模块不宣称任何规则接线。

## 1. V-RUN 独立板端复跑（fresh 轮）

命令：交付入口原样调用，`--stamp verify-20261019-item011-v1 --keep-remote`
（cleanup 延后至 V-EXT V7，保证扩展夹具可在同一树直编）。

```
[env]/[mkdir]/[upload-app] 313 files / [upload-model] 双 rknn
[build-core] exit=0 / [build-media] exit=0
[core-tests] exit=0 TEST_EXIT=0        (JUnit tests=1347 failures=0，含 47 distance 判例)
[distance-selftest] exit=0 DIST_SELFTEST_EXIT=0   (14/14)
[distance-fixtures] ×6 exit=0 FIXTURE_EXIT=0
    (no_calib→NO_CALIBRATION / zoom_unknown / zoom_mismatch / non_person→NOT_PERSON /
     out_of_range→OUT_OF_RANGE / valid→DISTANCE_OK；每夹具 RESULT: PASS，失败=0)
[distance-forms] ×4 exit=0 FORM_*_EXIT=0
    (anchor 89px→500m / scaling 178px→250m / zoom-unknown -1 / no-calib CAM-09 -1)
[media-selftest] exit=0 MEDIA_SELFTEST_EXIT=0     (28 判例含 6 distance 判例；36px→1236.1111)
[chain] exit=0 CHAIN_EXIT=0            (带标定真实链路 9/9 判定；distance_valid=0 /
                                        distance_unknown=0、text=— —— 测试源无人员目标，
                                        实现/验证两侧口径一致)
[cleanup] 延后（--keep-remote）
downloads=18   transcript/log 已归档（文件名见 file_manifest）
```

## 2. V-CMP 离线独立比对 oracle（73/73）

脚本 `verify/verify_item011_compare.py`，对 fresh 轮独立断言并与 impl r3 归档逐项恒等
（`verify/item011-compare-verify-20261019-item011-v1.json`）。**先对 impl r3 归档板前
dry-run 一次（`verify/_dryrun_on_impl.py` → `item011-compare-20261021-item011-r3.json`，
72/72，C8 因 verify 工件尚未生成的信号性前置未激活），证明判据非空转后才对 fresh 轮
执行。** 分相：

- C0EXIT transcript 全部步 exit=0 + 每步 *EXIT token=0（C0EXIT_DOMAIN 16 token 请点）
- C1 JUnit 1347/0 判例多重集恒等 + 47 distance 判例名精确域（多重集）+ 型式断言
  （NOT_PERSON/NO_CALIBRATION/ZOOM_*/OUT_OF_RANGE 在域）
- C2 selftest 14 判例名恒等 fresh vs r3 逐行
- C3 六夹具语义逐项（reason/valid/-1 复收）
- C4 四形态公式复算（89px→500 / 178px→250 / -1×2）+ EXIT token + 恒等
- C5 media 28 判例 + 6 distance 判例名集合 + 公式常数 1236.111111 复算
- C6 chain 9 判定 + calibration echo + 路遥测（valid=0/text=—）稳定性
- C7 指纹 + 凭据泄漏扫描（短凭据照实标注）
- C8 file_manifest 含 `docs/vision/test_evidence/ITEM-011/VERIFY-ITEM-011.md`

收尾在 VERIFY 工件与 manifest 更新完成后复跑确认 **73/73 全过**（C8 消费）。

## 3. V-EXT 板端自有扩展夹具（12/12，夹具 39/39）

`verify/vext_item011.cpp`（板端 g++ -std=c++17 -O1 直编，链 media/core/platform 三层静态库 +
pkg-config gstreamer-1.0/gstreamer-video-1.0）覆盖交付自测（core 47 distance 判例 +
selftest 14 + media 6）未覆盖的边界。**关键新增覆盖：worker 集成层 zoom 门控——交付
media selftest 只锚定 valid 与 no-calib 两条链路，验证夹具用真实 GStreamer NV12
FrameStore::publish → FrameLease → VisionAiWorker submit/step 真线程直驱，在 worker
遥测面观测 zoom_unknown / zoom_mismatch / 多档位精确换算 / 不限范围四条链路。**

驱动 `verify/verify_item011_vext.py`：

```
V0 fresh 保留树在场 PASS（三层静态库 + 双探针可执行）
V1 板端直编 2.5s exit=0（g_memdup 弃用警告，链接成功）
V2 39 判例全 PASS 且判例名集合与夹具源 check( 第二实参恒等（深度感知提取，src=39 board=39）
V3 CLI 负例：distance --bogus=2 / --help=0 / --fixture 无名=2 / 无参数 selftest PASS /
   media --bogus=2
V4 语义反转负例：CAM-09（无标定）--expect valid → exit=1（RESULT: FAIL failures=1
   —— 按声明“无标定恒 -1”断言必败，证明判据真咬合）
V5 确定性：distance --selftest --json 同参两轮 sha256 恒等（d3e863ca2d4dafea…）
V6 板端产物凭据语义扫描（SSH 凭据=admin 与登记用户名同名，文件级全串现禁用；按
   项-002/004 既定口径 len≥10 才计全串命中，本轮实测命中仅假阳性 cams yaml）
V7 通过后清理：vext 构件 + 整棵保留树，ls 确认无残留（CLEAN_DONE）
```

夹具域（39 判例）：

- **A0/A1 独立锚点自检**：验证侧独立经 parse_calibration_spec 构造锚点（source=
  `vext:independent`），reference_constant=44500 / px_per_m=52.352941 复算
- **A1..A7 worker 集成（GStreamer 真租约）**：Z1 匹配 → 74.4px→598.118 m 精确公式+文本
  `598.1 m`（A1/A2）；无档位 → ZOOM_UNKNOWN + 文本 `—` + last=-1（A3/A4，
  worker 层自测未覆盖项）；Z9 → ZOOM_MISMATCH（A5）；多档位 Z2=250m 锚点 →
  250*89/74.4 ≈ 298.992 m（A6）；min=0/max=0 不限范围全开放通过（A7）
- **B1..B6 门控顺序互斥**：NOT_PERSON > NO_CALIBRATION > ZOOM_UNKNOWN >
  ZOOM_MISMATCH > INVALID_BOX > OUT_OF_RANGE 顺序恒定（同请求单 reason）
- **C1..C6 范围端点语义**：[5,600] 上下端点含等值通过（8900px→5.0；**上限用二进制
  精确值对 55.625px↔800m**，避免 float 舍入把恰在边界推为越界液的假阳性）、越端点
  拒绝且 `meters=-1` 不 clamp（40px→1112.5）、0=不限双侧（C5/C6）
- **D1..D4 标定集合消费面**：多相机成员/zooms_of/同档位不同相机不同常数隔离
  （CAM-01→500 vs CAM-02→250）/同键重复登记先到优先
- **E1..E5 输出与无损公式**：`"distance_m":500,` 整数无小数点、无效 `"distance_m":-1,`
  恒等 token、原始 `distance_from_box_height_px` 与 estimate 一致（44.5px→1000），
  且 raw 不做范围门控（0.5px→89000）而 estimate 同输入 `OUT_OF_RANGE`——两者分层
  职责锚定
- **F1..F6b parse 边界**：obj_m 缺省→px/m=-1、min=0/max=0 合法、空串/缺等号/负 ref/
  `nan`/`inf` 全拒绝

## 4. 验证侧返工留痕（本轮过程）

| 处 | 错误 | 修正 |
| --- | --- | --- |
| 夹具 anchor() | 首版残留 `parse_calibration_spec(..., nullptr==nullptr ? new… : nullptr)`
  死代码（内存泄漏 + 无效逻辑） | 改为独立 parse + 失败即 FATAL exit(3)（夹具自检） |
| A1/A2/A6 图像高度 | stub 检测器 0.20 框高在 180px 图像 = 36px→1236 m，超出 [5,600] 范围被
  正确拒为 OUT_OF_RANGE（首版期望“valid”是验证侧算术错） | 改用 372px 图像（0.20*372=
  74.4px→598.118 m，含范围校验通过）；这也是“worker 帧才带图像高度”语义的额外证据 |
| A2 文本 | 44500/74.4 = 598.118，首版写 598.0 m | 改 `598.1 m`（A1 公式面在板面已
  证明实现精确） |
| C3/C6 端点 | 首版 44500/600 /=598.0 无二进制精确换算对 | 上限改 55.625px↔800m、
  C6 改 44500/8000↔5.5625px→8000 m 双向精确（ITEM-022 FP 教训应用） |
| V6 扫描 | SSH 凭据=admin 与 cameras.yaml username 同串致文件级假命中 | 按
  ITEM-002/004 既定口径 len≥10 才计全串命中；本轮 raw 命中即假阳性，如实记录 |
| V-CMP oracle | 板前 dry-run 抓 8 处验证侧构造错（C0EXIT 未捕获 main log、distance 判例域
  漏 distance_parse_spec_fields、4 形态 EXIT 同名 token 16 个非 17、media 判例名带
  ` :: detail` 后缀、manifest 深度 4 级不足等） | 均修正为 dry-run 72/72 后再上板 |

过程错误均为验证侧期望/工具构造错；板端产品行为未出现误报（A1 失败首判定为产品行为
正确、夹具预期错误，修正后全绿）。

## 5. 范围边界与移交

- ENV-D：本轮无相机连接，RTSP 真实取流不在判据内；真实 NPU 检测精度/框高信度不在本轮
  判据（worker 距离链路在 stub 检测器 + 真实发帧链路上呈证）
- 真实链路（chain）测试源无人员目标：每路 `distance_valid=0 / text=—` 是如实观察而非
  缺陷（调试视图占位恒 `—` 非 0 已由实现侧 + V-EXT 双侧呈证）；真实行人取流下的
  距离质量判定移交 ENV-A0 相机连接轮
- 变焦档位当前无真实 PTZ 控制接线：档位→标定映射在配置面/夹具面呈证，真实光变焦时
  同步档位状态移交后续（接口契约 §6 声明范围）
- 局部任务 TEST_PASS 仅证明 ITEM-011 与版本 7cf3a4e 的已测范围
- 板端构建树/临时文件已全部清理（V7 CLEAN_DONE，`ls` 确认无残留）

## 6. 证据文件索引（本轮新增）

| 文件 | 用途 |
| --- | --- |
| `vision-item011-192_168_0_103-verify-20261019-item011-v1.{log,transcript.json}` | fresh 复跑全套 runner 留痕 |
| `vision-item011-192_168_0_103-verify-20261019-item011-v1.app_core-evidence_*`（16 份） | core-tests JUnit/selftest log+json/6 fixtures/4 forms log+anchor json/media log+json/chain log+json |
| `verify/verify_item011_compare.py` / `verify/item011-compare-verify-20261019-item011-v1.json` | V-CMP 73/73（收尾） |
| `verify/item011-compare-20261021-item011-r3.json` / `verify/_dryrun_on_impl.py` | oracle 板前 dry-run（对 impl r3 归档，非空转证明） |
| `verify/vext_item011.cpp` | V-EXT 夹具（39 判例，域 A..F） |
| `verify/verify_item011_vext.py` / `verify/item011-vext-verify-20261019-item011-v1.json` | V-EXT 驱动与结果（12/12） |
| `vext-item011-verify-20261019-item011-v1.stdout.log` | V-EXT 板端 stdout 留痕 |
