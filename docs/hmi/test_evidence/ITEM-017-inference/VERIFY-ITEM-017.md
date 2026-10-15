# ITEM-017 软件验证 TestEvidence — 推理服务接入与双模型复用

- 角色/Capability: `software_tester` / `software_verification`
- Work item: `TL-778f77eb-1fde-496d-9677-7330e26c6476-tasks-ITEM-017`
- 被验证实现: `n005_embedded_linux_engineer_desktop_app_implementation.implementation`（commit `47638a8`）
- 权威判定轮: **VERIFY-20261016-v2**（2026-10-16，板端 RK3588 192.168.0.103 经项目登记 Paramiko SSH 入口）
- 结论: **TEST_PASS**

## 1. 判据（J1–J6，来源 DESKTOP_APP_DESIGN §8.1/§8.2、INTERACTION_DESIGN §5.5/§7.4）

| # | 判据 | 结果 |
|---|------|------|
| J1 | 仅 `ai_enabled` 相机按 `ai_profile` 进入推理；两侧广角（CAM-02/03, `none`）不入队 | ✅ 板端探针 6 路中 `eligible=4`，CAM-02/03 在 queue/run/调度全部缺席 |
| J2 | 有界**丢旧帧**队列 + 配置帧间隔降频推理 | ✅ `depth=2` 默认 50 帧/间隔 5：`dropped=35 inferred=10`（与逐帧采样预言完全一致）；多变体 50/2/4、25/5/2、1/5/2、7/3/2、10/4/3、50/5/1 全部命中预言 |
| J3 | 上报模型版本 / 推理 fps / 最近结果时间 / 耗时 | ✅ `model=rknn-2.3.2/yolov5s`（前/弓/门）与 `rknn-2.3.2/yolov8s-seg`（轮轨）按职责登记；`fps=5`；`latency` 逐路；`last=<1 秒前`→`N 秒前`→`M 分 R 秒` 文本边界逐级核对 |
| J4 | 优先级 + NPU 预算调度；超温守卫**降载不停** | ✅ temp=60 `run=CAM-01,CAM-05,CAM-04,CAM-06`；temp=90 `run=CAM-01,CAM-05 skipped=CAM-04,CAM-06 guard=true`（仍运行）；温度边界（=limit 不触发 / +ε 触发 / 奇数预算向下取整 / 预算 1 时 floor 保留 front）ext 夹具逐条断言 |
| J5 | 停掉推理线程 → 状态区 AI 项与相关报警**明确标注不可用，不静默** | ✅ `stopped=AI 不可用`、`alarm_annotation=AI 不可用`、调试页每路 `unavailable=AI 不可用`；`ai_runner` 真线程 start→stop 同验收 PASS |
| J6 | AI 调试页用户可观察（框/ROI/置信度/耗时/模型版本/最近结果） | ✅ 探针 `debug` 行 4 路全部 `boxes=1 conf=80 in_roi`；QML `P5AiDebug.qml` 静态接线（叠加层默认关闭、不可用占位诚实）；板端缺 Qt5 Quick 运行时（E-01b），QML 运行期未执行如实标注 |

## 2. 板端独立复跑（验证人自有凭证入口 + 自有 stamp）

经 `app/tools/board_build.py`（登记入口，指纹 `SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU` 核验一致后执行，全程有限等待）：

- **TestRun（v2）**：`TESTRUN_EXIT=0`；core 单测 **480 passed / 0 failed**；480 行 `[PASS]` 与实现者 r3 存档**逐行一致（identical=True）**；JUnit XML 独立解析 `tests=480 failures=0`，专用推理域用例 40 个（阈值 ≥37）。
- **ai-probe（v2）**：`AI_EXIT=0` `result=PASS`；验证人独立解析（不信探针自身 acceptance 行）：config/queue/schedule/debug/ai-status/acceptance 六组断言 `fails=[]`；输出与实现者 r3 存档**逐字节一致（identical=True，确定性复现）**。
- **MediaCheck（v2-media）**：`BUILD_EXIT=0 PROBE_EXIT=0 MULTI_EXIT=0 AIRUN_EXIT=0`（取流单路/六路回归同行复验）；`ai-runner` 独立断言 `fails=[]`（4/4 路线程运行、fps≥4、模型版本正确、停止后 `AI 不可用`），输出与实现者 media-r1 存档逐字节一致。
- **状态探针回归（v2）**：ITEM-016 两角色探针随 TestRun 同轮 `exit=0`，无回归。

## 3. 验证自有扩展夹具 `verify_ai17_ext.cpp`（125 断言全绿，链交付 `librailhmi_core.a`）

超出实现者 test_inference.cpp（37 断言）与交付探针覆盖的边界：

- **队列微观边界**：第 3 次 push 丢最旧返回 true；pop 取出的即被保留的最旧 seq；空队列 pop 不虚增 dropped 计数；`clear()` 保丢帧账不重置；depth=1 保最新；depth<=0 收敛为 1 不崩溃。
- **丢帧预言多变量**（逐帧采样于最后一个到期帧，非循环结束值）：50/5/2→(35,10,末弹 seq=44)、50/2/4→(21,25,45)、25/5/2→(15,5,19)、1/5/2→(0,1,0)、7/3/2→(3,3,5)、10/4/3→(4,3,6)、50/5/1→(36,10,45)；每变量守恒 `pushes=inferred+dropped+size`；最旧帧先出（FIFO 消费序）。
- **`requested=ceil(frames/interval)`** 恒等式 + 每变体 `inferred==requested`。
- **优先级/资格总序**：front(1)>wheel_rail(2)>pantograph(3)>cab_door(4)>none(99) 严格链；四职责 label 非空；none 不可入队。
- **调度分区不变量**：预算 8>资格数 → 全跑不 skip；run/skipped 不相交且并集=资格集；run+skipped 顺序=优先级序；单路资格 < 预算单独跑；仅 none 输入 → run/skipped 全空。
- **温度守卫边界**：temp=85.0（=limit）不触发全额；85.01 触发且 4→2；84.999 不触发；预算 1 超温 floor=1 仍跑 front（不静默停）；奇数预算 5 超温 5→2（向下取整）。
- **状态边界**：无任何结果 → Warning「推理陈旧」；age==stale_ms 仍 Ok，age==stale_ms+1 转 Warning；一路新鲜则整体 Ok；worker 停 → Unavailable「AI 不可用」；报警标注 true→空串 / false→「AI 不可用」；自定义 stale 窗口同边界语义。
- **调试视图矩阵**：worker 停 → 4 路全部不可用理由「AI 不可用」；存活无结果 → 「等待结果」+ `last=—`；新鲜结果 → available、框/置信度/模型版本/耗时从 latest 覆盖；age 文本五级（<1 秒前 / 1 秒前 / 2 秒前 / 59 秒前 / 1 分 0 秒前 / 2 分 5 秒前）逐级核对，且 >stale_ms 时 available 翻转为陈旧；空框但结果新鲜仍 available；fps 来自运行时遥测、正耗时以结果覆盖。

## 4. 负例 CLI（板端实测）

- `railhmi-ai-probe --nope` → **exit 2**（`unknown arg: --nope`，不崩溃）。
- 篡改 `app.yaml` `temp_limit_c` 为非数字 → **exit 1**，字段路径报错 `app.ai.temp_limit_c: expected number`。

## 5. 边界确认（如实，不虚构）

1. **SimDetector 为替代输入，非 RKNN**：线程/调度/遥测/不可用语义经真线程验证；RKNN 张量绑定、NV12 前处理、后处理精度属视觉/模型职责（实现声明一致，未在本层造假）。
2. **ENV-D 真机端到端未执行**：真实相机帧 + RKNN 结果未跑；取流侧回归（stream/multi probe）同轮 PASS。
3. **QML 运行期未执行**：板端缺 Qt5 Quick/Qml 开发包（E-01b）；P5 调试页以 QML 源码静态核对 + core/media 板端实跑为证据；`main.cpp` 注入 `worker_alive=false` 占位如实呈报「AI 不可用」。
4. INTERACTION §5.5 距离字段未实现——实现者显式声明省略，属声明式契约内偏差，验证方登记不改判。

## 6. 过程轮留痕

- v1：junit 正则 `inference_cases=24`（阈值 37）自判 fail——验证侧 pattern 覆盖不足，改用精确用例名域后为 40；ext 上传缺 mkdir `ENOENT`——修复。
- v1 ext 115/3 失败（59s/60s/125s age 断言期望 `available` 恒真）——验证侧预言错误：交付语义 age>stale_ms 即「推理陈旧」不可用是**正确**行为，夹具更正为「available==fresh」。
- v1/v1-media 存档保留；v2 为权威轮。MediaCheck v1 轮未带 `--keep-remote` 导致负例步骤缺 /tmp 目录，v2 修正留痕。

## 7. 证据文件

- 权威轮：`build-TestRun-192_168_0_103-VERIFY-20261016-v2.*`、`build-MediaCheck-192_168_0_103-VERIFY-20261016-v2-media.*`、`VERIFY-FIXTURES-VERIFY-20261016-v2.{log,transcript.json}`
- 过程轮（留痕）：`...-VERIFY-20261016-v1.*`
- 验证夹具：`verify_ai17_ext.cpp`、`verify_ai17_fixtures.py`、`run_verify_round.sh`

## 8. 复现

```sh
bash docs/hmi/test_evidence/ITEM-017-inference/run_verify_round.sh   # 默认 stamp VERIFY-20261016-v2 可 VERIFY_STAMP 覆盖
# 或逐项:
python app/tools/board_build.py --host 192.168.0.103 --user admin \
  --credential-env TRAIN_RK3588_SSH_CREDENTIAL --mode TestRun \
  --outdir docs/hmi/test_evidence/ITEM-017-inference --stamp <tag> --keep-remote
python app/tools/board_build.py --host 192.168.0.103 --user admin \
  --credential-env TRAIN_RK3588_SSH_CREDENTIAL --mode MediaCheck \
  --outdir docs/hmi/test_evidence/ITEM-017-inference --stamp <tag>-media --keep-remote
```
