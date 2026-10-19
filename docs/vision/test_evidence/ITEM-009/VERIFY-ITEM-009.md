# ITEM-009 软件验证证据（software_tester.software_verification，修复复核轮 v2）

- 工作项：ITEM-009 真实检测器接入推理工作线程与调度（视觉推理线）
- 验证对象：`docs/vision/implementation/ITEM-009_DETECTOR_WORKER.md`（commit ad8924b，r8 修复轮）
- 验证轮次标识：`verify-20261021-item009-v2`（v1 见 git 历史 bb63967：TEST_FAIL D-01）
- 验证侧角色契约：独立验证与失败分析；不改产品源码；验证侧构造错误自行修正并留痕
- 环境：Paramiko SSH 192.168.0.103（admin，凭据环境变量 `TRAIN_RK3588_SSH_CREDENTIAL`，指纹
  `SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU`，与 v1 一致），板端只写 /tmp，无相机连接（ENV-D）

## 结论（先读）

**TEST_PASS —— v1 全部判据复现全绿，v1 业务缺陷 D-01 的呈证命令面修复后反转，判据域补全。**

| 阶段 | 内容 | 结果 |
| --- | --- | --- |
| V-RUN | 交付入口 `board_vision_pipeline.py` 独立板端复跑（fresh，--keep-remote） | 全绿（9 步 exit=0，8 份证据取回） |
| V-CMP | 离线 oracle：impl r8 归档 59 项 + fresh 轮 51 项独立判据 + 跨轮恒等 8 项 | 118/118 PASS |
| V-NEG | D-01 反转取证：v1 取证同一命令面复跑 + 源码收口守恒 | 5/5 PASS（139→1 反转成立） |
| V-EXT | 板端 g++ 直编自有扩展夹具 38 判例 + CLI/确定性/泄漏/清理 | 9/9 PASS（夹具 38/38） |

被测版本与覆盖范围：实现 commit `ad8924b`（r8 修复轮，基线 `4908e7a` + D-01 修复）。
test/file 源板端链路呈证；RTSP 真实取流（ENV-D）、精度/召回不在本轮判据（交付声明边界同 v1）。

## 1. 被测变更（v1 TEST_FAIL → r8 修复）

D-01（v1 判定 TEST_FAIL 依据）：`vision_rknn_detector.cpp` load_kind 全部 5 处失败路径同构
`models_[kind]=std::move(m); if(err)*err=m->reason;` —— move 后解引用空 `unique_ptr`，
chain 模式模型缺失必 SIGSEGV（v1 取证：exit=139、缓冲 stdout 0 字节、gdb 回溯 `_M_assign` + load_kind）。

r8 修复（源码复核 + 板端行为双重确认）：

1. `load_kind` 新增局部 `fail_with(model, reason, detail)`：先写 reason/detail、**拷贝** reason
   到局部串，最后移交所有权 —— 5 处失败路径统一收口（S4 静态断言 `fail_with=5`、裸 move-后-解引用 0 处）。
2. 探针 `emit` 加 `fflush`（ofstream 同步 flush）：崩溃/异常路径保留已产生证据（v1 取证依赖的
   "0 字节输出旁证" 从此不再出现，属取证能力增强）。
3. selftest 新增 3 判例：`model_missing_graceful` / `model_missing_reason_visible` /
   `invalid_model_graceful`（19→22），异常路径进入交付自检回归面。
4. 交付 runner 新增默认步 `neg-missing-models`：`--models-dir /tmp/railhmi_item009_absent_models`
   直接复现 v1 缺陷命令面，判 NEG_EXIT=1（非 139）。

## 2. V-RUN 独立板端复跑（fresh 轮）

命令：交付入口原样调用，`--stamp verify-20261021-item009-v2 --keep-remote`。

```
[env] 0.2s / [mkdir] / [upload-app] 320 files / [upload-model] yolov8s-seg.rknn + yolov5s.rknn
[build-core] 50.0s exit=0 / [build-media] 63.6s exit=0
[core-tests] exit=0 TEST_EXIT=0        (JUnit tests=1215 failures=0)
[selftest]  exit=0 SELFTEST_EXIT=0     (22/22 PASS，含 3 个新异常路径判例)
[chain]     exit=0 CHAIN_EXIT=0        (单路真实链路 9/9)
[chain4]    exit=0 CHAIN4_EXIT=0       (4 路 budget=1 9/9)
[neg-missing-models] exit=0 NEG_EXIT=1 (模型缺失优雅降级，非 139)
downloads=8   transcript/log 已归档（文件名见 file_manifest）
```

## 3. V-CMP 离线独立比对 oracle（118/118）

脚本 `verify/verify_item009_compare_v2.py`（先经 `_dryrun_v2_impl.py` 对 impl r8 归档零板成本
预演，抓出并修正 2 处验证侧 oracle 构造错误后才上板，见 §6）。对 impl r8 与 fresh 两轮各自
独立断言 + 跨轮恒等（`verify/item009-compare-verify-20261021-item009-v2.json`）。要点：

- selftest 22 判例名集合恒等（v1 的 19 + r8 新增 3）；`RESULT: PASS failures=0`；
  submit max=0us、snapshot p95=0us、request_stop=0us、join=32ms（<400ms）
- 3 个新判例逐项断言：missing_graceful（load=false err=`det:MODEL_MISSING;seg:MODEL_MISSING;`）、
  missing_reason_visible（status_json 含 MODEL_MISSING）、invalid_graceful（非法文件 load=false）
- 生产模式 `prod_no_synthetic_frames`（steps>0 且 infer_calls=0）；温度族 MEASURED/
  thermal_zone*/injected=false，guard=false 与实测 ≤85.0°C 一致
- chain：config token 独立重推导（interval=5/depth=2/npu_budget=4(config)/temp_limit_c=85.0/
  confirm_frames=3/confidence_min=0.5）；`skipped_interval+consumed==300`；fps∈[4,6]；
  envelope 身份链（generation=1/latency≥0/model_version `rknn-2.3.2/yolov5s+rknn-2.3.2/yolov8s-seg`）
  + rail 掩码（box_index=-1/kind=rail/1280x720/plane）
- chain4：4 路、budget=1(cli-override) 生效、无饥饿（各路 consumed>0 且偏差≤6）、
  CAM-04/05/06 PROFILE_UNSUPPORTED 快速失败（last_infer_us≤100）、CAM-01 真实推理（≥20ms）
- neg 归档（impl r8 侧 8 项）：type=models JSON 双模型 present=false + reason=MODEL_MISSING、
  front profile ready=false reason=MODEL_MISSING、`[FAIL] chain_models_ready`、
  `RESULT: FAIL failures=1`、4 份 unavailable envelope 照常发布、stop 行 `AI 不可用`
- JUnit 1215/0 判例名集合跨轮恒等（sched_ 35 + thermal_ 19 新增族在位）
- 替代链路回归参照 impl r7 归档（synthetic_tick=on、4 channels、stopped=AI 不可用、result=PASS）
- 跨轮恒等：selftest 判例集/chain·chain4 判据序/profile 结构/JUnit 判例集与计数/config token

## 4. V-NEG D-01 反转取证（5/5）—— 修复成立的直接证据

脚本 `verify/verify_item009_neg_v2.py`（S 编号与 v1 取证 `dbg_missing_models.py` 一一对应，
复用 v1 取证的同一命令面；fresh 保留树）。对照 v1 归档
`verify/dbg-missing-models-evidence.json`：

| 步骤 | v1（r6 树） | v2（r8 树） | 判定 |
| --- | --- | --- | --- |
| S1 同命令复现（默认 models_dir 缺失） | exit=139、stdout 0 字节 | **exit=1、6231 字节** | 反转成立 |
| S2 stdbuf 无缓冲 | 崩溃中断于 `type=config` 后 | **完整走完，末行 `RESULT: FAIL failures=1`，MODEL_MISSING 命中 5 处** | 反转成立 |
| S3 有效模型对照 | exit=0 | exit=0（成功路径不受影响） | 保持 |
| S4 源码守恒 | gdb 崩于 load_kind `_M_assign` | `fail_with=5`、裸 move-后-解引用 0 处（验证侧静态扫描） | 修复在位 |

runner 默认步 `neg-missing-models` 独立佐证（impl r8 与 fresh 双轮）：`NEG_EXIT=1`、
`type=models` 双模型 MODEL_MISSING、unavailable envelope 照常发布 —— 语义"模型缺失 → 显式
reason → AI 不可用"取代进程崩溃，正是 v1 判定被破坏的验收语义。

## 5. V-EXT 板端自有扩展夹具（9/9，夹具 38/38）

`verify/vext_item009.cpp` **逐字节复用 v1 v2 版夹具**（38 判例域 A..M，core 层调度/温度语义，
r8 未触碰该层），`verify/verify_item009_vext_v2.py` 驱动：

```
V0 fresh 保留树在场 PASS / V1 板端 g++ 直编 1.8s exit=0
V2 38 判例全 PASS 且与夹具源判例名集合恒等（src=38 board=38）/ 域覆盖 A..M 全
V3 CLI：unknown-arg=2 / no-action(selftest 默认)=0 / --help=0
V4 确定性：--selftest 同参两轮 --json sha256 恒等（1164a725…，与 v1 轮同值；
   stdout 温度/时序行属环境量不做字节恒等）
V5 板端产物凭据语义扫描 0 命中
V6 通过后清理（vext 构件 + 保留树 + neg 取证临时文件），板上 /tmp 已无 railhmi_vision_item009_* 残留
```

## 6. 验证侧返工留痕（v2 过程轮）

| 处 | 错误 | 复核依据 | 修正 |
| --- | --- | --- | --- |
| compare impl 侧 r7 归档路径 | 前缀误写 r8 stamp（20261021-item009-r7 不存在） | ai-runner 归档由 r7 轮产出，前缀是 r7 轮自身 stamp（20261020-item009-r7） | 按实际归档名引用，注明 r7 对 r8 树的参照边界 |
| neg_oracle models JSON 计数 | `count('"reason":"MODEL_MISSING"')==2` | status_json 中 front profile reason 亦为 MODEL_MISSING，全文命中 3 处 | 改为 `count("MODEL_MISSING")>=2` |
| V-NEG S2 窗口 | `tail -3` 窗口断言 MODEL_MISSING 可见 | `[FAIL] chain_models_ready` 行距尾部 4 行，窗口截不到（第 2 轮 dry 思维遗漏，首跑即现） | reason 可见性改全文 `grep -c` 计数断言（≥2），末行 RESULT 断言保留 |

过程轮错误均为验证侧期望构造错，未产生产品缺陷误报；板端执行全部一次通过。

## 7. 范围边界与移交

- ENV-D：本轮无相机连接，RTSP 真实取流不在判据内（test/file 源呈证链路贯通）
- 精度/召回不在本轮（selftest 声明"不声称精度"）；4 路全模型并发仅 front 有模型证据（交付 §7.4 边界）
- `railhmi-ai-runner` 替代链路：r8 runner 默认步不含 ai-runner，沿用 impl r7 归档作回归参照
  （r8 修复不触及 SimDetector 链路，fresh 轮以 prod_no_synthetic_frames + 判例集恒等等价覆盖）
- 局部任务 TEST_PASS 仅证明 ITEM-009 与版本 ad8924b 的已测范围
- 板端构建树/临时文件已全部清理（V6 CLEAN_DONE，`ls` 确认无残留）

## 8. 证据文件索引（本轮新增）

| 文件 | 用途 |
| --- | --- |
| `vision-item009-192_168_0_103-verify-20261021-item009-v2.{log,transcript.json}` | fresh 复跑全套 runner 留痕 |
| `vision-item009-192_168_0_103-verify-20261021-item009-v2.app_core-evidence_*`（8 份） | core-tests JUnit/selftest/chain/chain4/neg-missing |
| `verify/verify_item009_compare_v2.py` / `verify/item009-compare-verify-20261021-item009-v2.json` | V-CMP 118/118 |
| `verify/_dryrun_v2_impl.py` | oracle 板前预演驱动（59 项 impl+neg 侧） |
| `verify/verify_item009_neg_v2.py` / `verify/item009-neg-verify-20261021-item009-v2.json` | V-NEG 5/5 反转取证 |
| `verify/verify_item009_vext_v2.py` | V-EXT 驱动（夹具 vext_item009.cpp 复用） |
| `vext-item009-verify-20261021-item009-v2.{json,stdout.log}` | V-EXT v2 结果归档（9/9，38 判例） |

v1 证据（TEST_FAIL 判定、D-01 取证 S1..S4、vext v1/v2 返工留痕）见 git 历史 bb63967 版本的
`VERIFY-ITEM-009.md` 与 `verify/dbg-missing-models-evidence.json`，本轮不覆盖、不删除。
