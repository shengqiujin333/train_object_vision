# ITEM-010 软件验证证据（software_tester.software_verification）

- 工作项：ITEM-010 其它职责可用性语义（pantograph / wheel_rail / cab_door，视觉推理线）
- 验证对象：`docs/vision/implementation/ITEM-010_DUTY_AVAILABILITY.md`（commit `f2c4f0b`，r4 终轮）
- 验证轮次标识：`verify-20261019-item010-v1`
- 验证侧角色契约：独立验证与失败分析；不改产品源码；验证侧构造错误自行修正并留痕
- 环境：Paramiko SSH 192.168.0.103（admin，凭据环境变量 `TRAIN_RK3588_SSH_CREDENTIAL`），
  板端只写 /tmp，无相机连接（ENV-D）；核心库/双探针在板端原生构建，无交叉编译
- 判据域：test design G10（TC-T-MIS-01..04：登记不是能力、unknown 永不 normal、
  能力匹配模型到场才 ready、不可用语义可观察）

## 结论（先读）

**TEST_PASS —— 三叉验证全绿：交付复跑逐步复现 + 离线 oracle 116/116 + 板端自有扩展夹具 28/28。**

| 阶段 | 内容 | 结果 |
| --- | --- | --- |
| V-RUN | 交付入口 `board_vision_duty.py` 独立板端复跑（fresh，--keep-remote） | 全绿（16 步 exit=0，22 份证据取回） |
| V-CMP | 离线 oracle：fresh 轮对 impl r4 归档 116 项独立判据（板前 dry-run 同 116/116 证明非空转） | 116/116 PASS |
| V-EXT | 板端 g++ 直编自有扩展夹具 28 判例 + CLI 负例 + 语义反转负例 + 确定性 + 清理 | 11/11 PASS（夹具 28/28） |

被测版本：实现 commit `f2c4f0b`（r4 终轮；r2 CoreTests 编译失败、r3 cab_door 误报
MODEL_MISSING 均为实现侧过程留痕，见 impl 证据）。验收语义逐条呈证：

1. **无专用模型 → unavailable + reason + unknown**：三职责在真实配置口径下
   `unavailable=3 unknown=3`（pantograph→PROFILE_UNSUPPORTED、wheel_rail→PROFILE_UNSUPPORTED、
   cab_door→PROFILE_UNSUPPORTED；模型缺失夹具→MODEL_MISSING、结构不匹配夹具→MODEL_MISMATCH），
   状态一律 unknown，从不映射 normal。
2. **登记不是能力**：config 步独立断言 `registration is not evidence` 语义细节在场，
   app.yaml 登记的模型名（pantograph→yolov5s 等）不改变 unavailable 判定。
3. **能力匹配模型到场才 ready**：only ready 夹具（声明+完整结构匹配描述）为 READY；
   真实 rknn_query 描述 drill 反向证明——yolov8s-seg 13 输出 vs 声明 3 输出 → MODEL_MISMATCH
   （mismatched 含 `output_count 13 != 3`），证明核验真实咬合。
4. **不可用语义可观察**：chain4 真实 4 路链路 unavailable=[CAM-04,05,06]（不占 NPU 预算、
   skipped 恒空、envelope 照常发布空 boxes+masks）；V-EXT g20..g29 调度域 28 判例中的
   10 例进一步锚定守卫/预算/waits/idle/JSON 边界。

## 1. V-RUN 独立板端复跑（fresh 轮）

命令：交付入口原样调用，`--stamp verify-20261019-item010-v1 --keep-remote`
（cleanup 延后至 V-EXT V7，保证扩展夹具可在同一树直编）。

```
[env]/[mkdir]/[upload-app] 320 files / [upload-model] 双 rknn
[build-core] exit=0 / [build-media] exit=0
[core-tests] exit=0 TEST_EXIT=0        (JUnit tests=1300 failures=0，85 项 duty 族新增在位)
[duty-selftest] exit=0 DUTY_SELFTEST_EXIT=0   (17/17)
[duty-config]  exit=0 DUTY_CONFIG_EXIT=0      (真实配置：3 职责全 unavailable+unknown)
[duty-fixtures] ×5 exit=0 FIXTURE_EXIT=0      (no_model/config_only→PROFILE_UNSUPPORTED、
                                               missing→MODEL_MISSING、mismatch→MODEL_MISMATCH、ready→READY)
[drill] RKNN×2 + DESC_SEG/DESC_DET exit=0     (真实 yolov8s-seg 描述 vs 声明 → MODEL_MISMATCH)
[duty-media] ×2 exit=0 MEDIA_NO_MODEL/MEDIA_MISMATCH_EXIT=0
[chain4]     exit=0 CHAIN4_EXIT=0             (budget=1：run=[CAM-01]、unavailable=[CAM-04,05,06]、skipped=[])
[cleanup]    exit=0（--keep-remote 下保留树，清理延后）
downloads=22  transcript/log 已归档（文件名见 file_manifest）
```

## 2. V-CMP 离线独立比对 oracle（116/116）

脚本 `verify/verify_item010_compare.py`，对 fresh 轮独立断言并与 impl r4 归档逐项恒等
（`verify/item010-compare-verify-20261019-item010-v1.json`）。**先经 `_dryrun_names.py`
同类预演纪律：oracle 以 impl r4 归档为输入板前 dry-run 一次（`item010-compare-20261021-item010-r4.json`，
同样 116/116），证明判据非空转后才对 fresh 轮执行。** 分相：

- C00 transcript 17 步 exit=0 + cleanup 延后（keep-remote 语义）
- C1 JUnit 1300/0 判例多重集恒等 + duty 域关键判例在位
- C2 selftest 17 判例名恒等 + 独立场景断言（no_model 三职责 PROFILE_UNSUPPORTED/unknown、
  ready-but-unknown、prediction_normal、未知标签不显示正常）
- C3 config 行恒等 + 登记名集合 + `registration is not evidence` 细节
- C4 五夹具语义逐项重推导（PROFILE_UNSUPPORTED/MODEL_MISSING/MODEL_MISMATCH/READY）
- C5 真实描述 drill（seg：mismatched=`output_count 13 != 3` + detected=yolov8_seg；
  det：`output_count 3 != 2`；api 2.3.2 / drv 0.9.8；in=1 out=13/3）
- C6 media duty 两情形（no-model / mismatch）逐字段恒等
- C7 chain4 语义重推导（4 路、budget=1、unavailable 快速失败 last_infer_us<1000、fps>4.5、
  envelope 空 boxes+masks、消耗守恒、events=0、温度实测）
- C8 指纹 + 凭据泄漏扫描（长度≥10 全串匹配口径，避免 admin 假阳性）
- C9 全部 17 个 EXIT token 为 0 且种类集合完备

## 3. V-EXT 板端自有扩展夹具（11/11，夹具 28/28）

`verify/vext_item010.cpp`（板端 g++ -std=c++17 直编，链 `librailhmi_core.a`）覆盖交付自测
（core-tests 85 + selftest 17）未覆盖的边界，驱动 `verify/verify_item010_vext.py`：

```
V0 fresh 保留树在场 PASS / V1 板端直编 1.4s exit=0
V2 28 判例全 PASS 且判例名集合与夹具源 check( 第二实参恒等（深度感知提取，src=28 board=28）
V3 CLI 负例：duty --bogus=2 / --help=0 / 无参数 selftest 默认 PASS / media --bogus=2
V4 语义反转负例：真实 yolov8s-seg 描述 + 声明 pantograph --expect available → exit=1
   （mismatch 模型按声明意图断言 available 必失败 —— 判据真咬合的直接证据）
V5 确定性：duty --selftest --json 同参两轮 sha256 恒等（f4cd6baeb71a3223…）
V6 板端产物凭据语义扫描（有界）
V7 通过后清理：vext 构件 + 整棵保留树，ls 确认无残留
```

夹具域（g01..g29，无 g09；28 判例）：

- **A apply 自动求值**：未先 evaluate 直接 apply 自动求值并计数（g01/g02）；无声明 apply 拒绝
  rejected_unavailable=1（g03）
- **B 阈值边界**：score==score_min 恰好过（g04）、0.499999 拒（g05）、override==0 是合法阈值非
  恒拒（g06）、override<0 回落声明阈值（g07）
- **C 平分 tie**：等分时首个高优先序标签保持（g08，严格 `>` 不被后到覆盖）
- **D 统计台账精确账**：evaluations/predictions/unknown_results（evaluate 的 READY-unknown 与
  unknown apply 各 +1 = 2）/blocked/below/rejected 逐项（g10）；二次 apply 不增账（g11）；
  重复 evaluate 语义状态回落 unknown 而 reason=READY（g12，结构视图 vs 最近推理视图）
- **E Detection 映射**：NotRaised→PantographState::NotRaised（g13）、Closed→DoorState::Closed
  （g14）、wheel normal 不置 abnormal（g15 回归锚）
- **F load_status 透传**：RKNN_RUNTIME_UNAVAILABLE 原样作 reason（g16）、空 → NOT_READY（g17）
- **G output_count=0**：跳过输出核验仍 READY（g18）、布局检查随之跳过（255x80x80 不判
  mismatch，g19 行为锚定）
- **H 调度集成**：温度守卫 budget 2→1（g20）run 仅 front（g21）职责路 unavailable 不占预算
  （g22）无帧职责路归 idle 不归 unavailable（g23）skipped 恒空（g24）waists 只含 front
  （g25）次轮稳定（g26）max_wait 不被污染（g27）恢复后职责路仍在预算外（g28）决策 JSON
  unavailable 可观察（g29）

## 4. 验证侧返工留痕（本轮过程）

| 处 | 错误 | 修正 |
| --- | --- | --- |
| vext check() 打印 | `" :: " + detail.c_str()` 两指针相加，板端 g++ 编译失败 | 改为 std::string 拼接（验证侧夹具缺陷，非产品缺陷） |
| V4 退出码 | `\| tail -2; echo $?` 取到 tail 的退出码 | 探针输出重定向文件后 `echo $?`，再 tail 查看 |
| V4 描述路径 | 误写 `../../descriptors/`（保留树内无此相对路径） | 按交付 runner 口径改绝对 `%REMOTE%/descriptors/` |
| 判例数口径 | 驱动断言 29，实际夹具 28（无 g09） | 以夹具源提取集合恒等为准，阈值改 28 |
| check 名提取 | v1 从闭括号起扫描逗号（0 命中）；v2 顶层逗号扫描得多行首参垃圾名（12 个） | v3 深度感知：从开括号深度 1 起找首个深度 1 逗号，板前离线验证 28/28 后移植进驱动 |

过程错误均为验证侧期望/工具构造错；板端产品行为未出现误报。V4/V2 修复后板端复跑一次全绿。

## 5. 范围边界与移交

- ENV-D：本轮无相机连接，RTSP 真实取流不在判据内；真实 NPU 推理精度/召回不在本轮判据
  （duty 语义在结构证据 + 类分数注入夹具上呈证）
- wheel_rail / cab_door 无专用模型可用（同 pantograph 无 pantograph 专用权重）：本轮呈证的是
  "无专用模型时的拒绝语义 + 能力匹配才 ready" 全链路；专用模型到场后的端到端识别质量移交后续
- 真实模型 drill 仅反向（yolov8s-seg/yolov5s 结构与职责声明不匹配 → MODEL_MISMATCH），
  无正向专用模型可 drill
- 局部任务 TEST_PASS 仅证明 ITEM-010 与版本 f2c4f0b 的已测范围
- 板端构建树/临时文件已全部清理（V7 CLEAN_DONE，`ls` 确认无残留）

## 6. 证据文件索引（本轮新增）

| 文件 | 用途 |
| --- | --- |
| `vision-item010-192_168_0_103-verify-20261019-item010-v1.{log,transcript.json}` | fresh 复跑全套 runner 留痕 |
| `vision-item010-192_168_0_103-verify-20261019-item010-v1.app_core-evidence_*`（20 份） | core-tests JUnit/selftest/config/5 fixtures/rknn×2/descriptor×2/media×4/chain4 |
| `verify/verify_item010_compare.py` / `verify/item010-compare-verify-20261019-item010-v1.json` | V-CMP 116/116 |
| `verify/item010-compare-20261021-item010-r4.json` | oracle 板前 dry-run（对 impl r4 归档，非空转证明） |
| `verify/vext_item010.cpp` | V-EXT 夹具（28 判例，域 A..H） |
| `verify/verify_item010_vext.py` / `verify/item010-vext-verify-20261019-item010-v1.json` | V-EXT 驱动与结果（11/11） |
| `verify/_dryrun_names.py` | check 名提取板前离线验证（深度感知 v3） |
| `vext-item010-verify-20261019-item010-v1.stdout.log` | V-EXT 板端 stdout 留痕 |
