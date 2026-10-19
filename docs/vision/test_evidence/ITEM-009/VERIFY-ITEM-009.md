# ITEM-009 软件验证证据（software_tester.software_verification）

- 工作项：ITEM-009 真实检测器接入推理工作线程与调度（视觉推理线）
- 验证对象：`docs/vision/implementation/ITEM-009_DETECTOR_WORKER.md`（commit 4908e7a，r6 终轮）
- 验证轮次标识：`verify-20261020-item009-v1`
- 验证侧角色契约：独立验证与失败分析；不改产品源码；验证侧构造错误自行修正并留痕
- 环境：Paramiko SSH 192.168.0.103（admin，凭据环境变量 `TRAIN_RK3588_SSH_CREDENTIAL`，指纹
  `SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU`），板端只写 /tmp，本轮无相机连接（ENV-D）

## 结论（先读）

**TEST_FAIL —— 交付声明范围内存在 1 项功能异常（D-01，SIGSEGV），其余全部验证判据通过。**

| 阶段 | 内容 | 结果 |
| --- | --- | --- |
| V-RUN | 交付入口 `board_vision_pipeline.py` 独立板端复跑（fresh，--keep-remote） | 全绿（216.4s，7 份证据取回） |
| V-CMP | 离线 oracle：impl r6/r7 归档 + fresh 轮独立判据 + 跨轮恒等 | 104/104 PASS |
| V-EXT | 板端 g++ 直编自有扩展夹具 38 判例 + CLI/确定性/泄漏/清理 | 9/9 PASS（夹具 38/38） |
| D-01 | 异常路径探查：chain 模式模型缺失 | **SIGSEGV（exit 139）** |

D-01 位于本轮交付源码 `app/src/media/src/vision_rknn_detector.cpp`（ITEM-009 新增），属"功能/异常行为
不符"——按能力契约返回 TEST_FAIL，附可复现证据与根因定位。

## 1. V-RUN 独立板端复跑（fresh 轮）

命令：交付入口原样调用（验证侧不改参数语义），`--stamp verify-20261020-item009-v1 --keep-remote`。

结果（`vision-item009-192_168_0_103-verify-20261020-item009-v1.log` / `.transcript.json`）：

```
[env] exit=0 / [mkdir] exit=0 / [upload-app] 320 files
[build-core] exit=0 / [build-media] exit=0
[core-tests] exit=0 TEST_EXIT=0          (JUnit tests=1215 failures=0)
[selftest]  exit=0 SELFTEST_EXIT=0       (19/19 PASS)
[chain]     exit=0 CHAIN_EXIT=0          (单路真实链路 9/9)
[chain4]    exit=0 CHAIN4_EXIT=0         (4 路 budget=1 9/9)
PROBE_EXIT=0 elapsed=216.4s downloads=7
```

fresh 轮证据文件 7 份已取回归档（文件名见 file_manifest）。

## 2. V-CMP 离线独立比对 oracle

脚本 `verify/verify_item009_compare.py`（先经 `_dryrun_on_impl.py` 对 impl 归档零板成本预演，
抓出并修正 5 处验证侧 oracle 构造错误后才上板）。

对 impl r6 与 fresh 两轮各自独立断言（每轮 45 项）+ 跨轮恒等（14 项），共 104 项全部 PASS
（`verify/item009-compare-verify-20261020-item009-v1.json`）。要点：

- selftest 19 判例名集合恒等且 `RESULT: PASS failures=0`；submit max<50ms、snapshot p95<20ms、
  request_stop=0us、join<400ms
- 生产模式 `prod_no_synthetic_frames`（steps>0 且 infer_calls=0）
- 温度实测族：`state=MEASURED`、source=thermal_zone*、`injected=false`；guard=false 与实测
  ≤85.0°C 一致（守卫判定与实测温度一致，非模拟值冒充）
- chain 配置 token 独立重推导（interval=5/depth=2/npu_budget=4(config)/temp_limit_c=85.0/
  confirm_frames=3/confidence_min=0.5）；`skipped_interval+consumed==300`；fps∈[4,6]
- envelope 身份链（camera_id/profile/generation=1/seq int/latency≥0/model_version
  `rknn-2.3.2/yolov5s+rknn-2.3.2/yolov8s-seg`）+ rail 掩码（box_index=-1/kind=rail/1280x720/plane）
- chain4：4 路、CLI 预算覆盖 budget=1 生效、无饥饿（各路 consumed>0 且偏差≤6）、
  CAM-04/05/06 PROFILE_UNSUPPORTED 快速失败（last_infer_us≤100）、CAM-01 真实推理（≥20ms）、
  3 份 unavailable envelope 结构恒等
- JUnit 1215/0，新增 54 = sched_ 35 + thermal_ 19
- 跨轮确定域恒等：判例名集合/chain 判据序列/profile 结构/JUnit 判例集/config token
  （时序域按采样波动做结构与方向断言，不做逐字节比对；join_us 28ms vs 31ms 属采样噪声）

## 3. V-EXT 板端自有扩展夹具

`verify/vext_item009.cpp`（38 判例，域 A..M）+ `verify/verify_item009_vext.py`（V0..V6）。

覆盖交付单测/探针未触及的调度器与温度语义边界（全部验证侧独立手工推导期望值）：
轮转配对精确序列（budget=2）、新路 wait 抢占、idle 恢复后等待计数保留、budget=1 四路 max_wait
有界、温度解析物理边界（默认界 [-40,200] 含端点、200.001 越界、内部空格/尾部杂非 kMalformed）、
多 zone 首失败 reason 保留、注入样本门（仅 isfinite；NaN→kOutOfRange+UNAVAILABLE）、fresh 半开窗
（age==max_age 新鲜；max_age≤0 免检；无效样本永不新鲜）、守卫奇数预算下取整（5→2/3→1/1→1）、
idle 不参与 wait 统计、schedule_round_json 特殊字符转义、budget=1 三路周期 3 精确轮转序
X,Y,Z,X,Y,Z。

v2 轮结果（V1 板端 g++ 直编 1.7s，exit 0）：

```
V0 树在场 PASS / V1 编译 PASS / V2 38 判例全 PASS 且与夹具源判例名集合恒等 / 域覆盖全
V3 CLI：unknown-arg=2 / no-action(selftest 默认)=0 / --help=0
V4 确定性：--selftest 同参两轮 --json sha256 恒等（1164a725…；stdout 温度/时序行属环境量不做字节恒等）
V5 板端产物凭据语义扫描 0 命中
V6 通过后清理（vext 构件 + 保留树），板上 /tmp 已无 railhmi_vision_item009_* 残留
```

### 验证侧返工留痕（v1 → v2）

v1 轮 38 判例中 6 处失败经逐条对源码复核，全部为验证侧期望算术错误（非产品缺陷），修正留痕：

| 判例 | v1 错误期望 | 复核依据 | v2 修正 |
| --- | --- | --- | --- |
| C_waits_after_run | 两路 wait 均 0 | `waits()` 为记账后口径：r3 由 CAM-08 运行，CAM-01 last_run=r2 → wait=1 | 01==1 && 08==0 |
| E_plus_40C_rejected | +40 越界 | 默认 max=200，+40 在界内 | 改断言接受 c==40.0 |
| E_minus_40C_rejected_default | -40 被拒 | 拒绝条件为 `c < min`，恰等于下界含端点接受 | 改断言接受 c==-40.0 |
| E_minus_39_999_ok | 双侧 ±0.001 窗 | 浮点严格不等边界（ITEM-022 教训复现），收窄为区间断言 | c>-40.0 && c<-39.9 |
| E_200_001_rejected | wide 界(max=250) 下拒绝 | 用错界限 | 改默认界(max=200) 下拒绝 |
| M_max_wait_3 | 峰值 3 | `max_wait_rounds` 在本轮 run 记账后计算，对外可观测峰值为 2 | ==2（注释说明口径） |

驱动判据同步修正 2 处：V3 `no-action` 期望 2→0（本探针 mode 默认 selftest，无动作即自检通过，
与 ITEM-008 tracking 探针不同）；V4 由 stdout 字节恒等改为 `--json` 摘要恒等（stdout 含实测
温度/时序属环境量）。V1 轮 V3 发现的 `--mode chain --source bogus` 退出 139 未列入负例，
单独立案为 D-01 取证（见下）。

## 4. D-01 功能异常：chain 模式模型缺失 → SIGSEGV（exit 139）

**定性**：ITEM-009 交付代码中的异常路径缺陷。模型缺失/不可加载时探针应优雅降级为
`chain_models_ready FAIL`（exit 1，探针自身设计了该判据 `vision_pipeline_probe.cpp:650`），
实际进程段错误崩溃，语义"停止后 AI 不可用/模型不可用显式 reason"被进程崩溃取代。

**可复现**（板上保留树，任一位置）：

```bash
cd <build_tree>/app          # models_dir 默认 ../models（不存在即可复现）
./build-media/tools/railhmi-vision-pipeline --mode chain --config ./config --source test --duration 2
echo $?                      # -> 139（SIGSEGV）；缓冲输出 0 字节
```

对照：`--models-dir <有效 model-assets>` → 9/9 PASS exit 0（chain/chain4 既证）；空模型目录同样 139。

**取证过程**（`verify/dbg_missing_models.py` → `verify/dbg-missing-models-evidence.json`）：

- S1 复现：exit=139，stdout 0 字节（emit 用 std::printf 无 flush，缓冲随崩溃丢失）
- S2 `stdbuf -o0` 无缓冲复现：崩溃前最后一行为 `type=config …`，下一行应为 `type=models …`
  （即 `detector->load()` 返回后的 `status_json()`），崩点锁定在 `load()` 内部
- S3 对照：有效模型目录 → RESULT: PASS failures=0，exit 0
- S4 gdb 回溯（板端 /usr/bin/gdb）：

```
Program received signal SIGSEGV, Segmentation fault.
pc 0x7ff7a318f8 <std::__cxx11::basic_string<…>::_M_assign(std::__cxx11::basic_string<…> const&)+56>
#0 …::_M_assign(…) () at /lib/aarch64-linux-gnu/libstdc++.so.6
#1 railhmi::media::RknnVisionDetector::load_kind(…) ()
#2 railhmi::media::RknnVisionDetector::load(…) ()
#3 (anonymous namespace)::run_chain(…) ()
#4 main ()
```

**根因**（源码静态对应，`vision_rknn_detector.cpp` load_kind 全部 5 处失败路径同构，
161-163/171-174/181-184/191-194/199-204 行附近）：

```cpp
models_[kind] = std::move(m);   // unique_ptr m 移交后变空
if (err) *err = m->reason;      // 解引用空 unique_ptr -> SIGSEGV（_M_assign 于 m->reason 的字符串赋值）
```

`run_chain` 恒传 `&load_err`（vision_pipeline_probe.cpp:499-500），故"调用方要错误串"与
"加载失败"同时成立时必崩。模型文件缺失时 `RknnSession::open` 先以 `MODEL_MISSING` 失败返回，
随即落入上述路径。修复方向（供实现方参考，验证侧不改产品源码）：先把 `reason` 拷贝到局部
`std::string` 再 move，或调整赋值顺序；5 处同构一并修。

**影响面**：`RknnVisionDetector::load` 当前唯一调用方为本探针 chain 模式（rg 全仓核对）。
桌面主程序本轮未接真实检测器（生产路径走 VisionAiWorker + 配置的检测器注入），但该缺陷使
"模型缺失 → 显式 unavailable reason" 的异常语义在 load 阶段即被崩溃取代；且探针是本轮
ITEM-009 验收判据的载体。归属交付范围，判 TEST_FAIL 依据成立。

**覆盖缺口说明**（为何既有全绿轮次未发现）：selftest 用 stub 检测器（不加载 RKNN）；chain/
chain4 用有效模型（加载成功路径）；core 1215 单测不覆盖 media 层。模型缺失的 Front profile
加载失败路径此前无任何轮次触及——本轮由验证侧 CLI 负例探查发现。

## 5. 范围边界

- ENV-D：本轮无相机连接，RTSP 真实取流不在判据内（探针 test/file 源呈证链路贯通）
- 精度/召回不在本轮（探针 selftest 声明"不声称精度"）
- `railhmi-ai-runner` 替代链路回归：交付 runner 默认步不含 ai-runner，fresh 轮复用 impl r7
  归档做 R7 oracle（compare 脚本内注明理由）
- 板端构建树/临时文件已全部清理（V6 CLEAN_DONE + dbg 树 rm -rf 复核为空）

## 6. 证据文件索引

| 文件 | 用途 |
| --- | --- |
| `verify/_dryrun_on_impl.py` / `verify/dryrun_on_impl.json` | oracle 预演（51/51 PASS） |
| `verify/verify_item009_compare.py` / `verify/item009-compare-verify-20261020-item009-v1.json` | V-CMP 104/104 |
| `verify/vext_item009.cpp`（v2）/ `verify/verify_item009_vext.py` | V-EXT 夹具与驱动 |
| `vext-item009-verify-20261020-item009-v1.{json,stdout.log}` | V-EXT v2 结果归档（9/9，38 判例） |
| `verify/dbg_missing_models.py` / `verify/dbg-missing-models-evidence.json` | D-01 取证 S1..S4 |
| `verify/vision-item009-192_168_0_103-dbg-item009-missing-models.{log,transcript.json}` | dbg 树构建留痕 |
| `vision-item009-192_168_0_103-verify-20261020-item009-v1.*`（根目录） | fresh 复跑全套（transcript/log + 7 证据） |
