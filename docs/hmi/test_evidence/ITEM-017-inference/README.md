# ITEM-017 证据 — 推理服务接入与双模型复用

- Work item: `TL-778f77eb-1fde-496d-9677-7330e26c6476-tasks-ITEM-017`
- 依据: `docs/hmi/DESKTOP_APP_DESIGN.md` §8.1（按 ai_enabled/ai_profile 调度；左右侧 none 不入队）、§8.2（降频、有界丢旧帧、NPU 预算、温度守卫、AI 退出转异常）；`docs/hmi/INTERACTION_DESIGN.md` v2 §5.5/§7.4（AI 调试页、AI 运行时字段）；`docs/hmi/VISUAL_DESIGN.md` v2 §8.5
- 源码: `app/src/core/include/railhmi/core/inference.h`、`app/src/core/src/inference.cpp`、`app/src/core/tools/ai_probe.cpp`、`app/tests/core/test_inference.cpp`、`app/src/media/include/railhmi/media/ai_worker.h`、`app/src/media/src/ai_worker.cpp`、`app/src/media/tools/ai_runner.cpp`、`app/src/ui/qml/pages/P5AiDebug.qml`、`app/src/ui/qml/main.qml`、`app/src/ui/src/main.cpp`、`app/config/app.yaml`、`app/src/core/src/config.cpp`、`app/tools/board_build.py`

## 1. 交付内容

1. **`railhmi-core` 推理服务模型**（无 Qt/RKNN，纯逻辑）：
   - `BoundedFrameQueue`：有界**丢旧帧**队列（深度固定，满时丢最旧并计数）。
   - `ai_priority`/`ai_eligible`：职责优先级 `front > wheel_rail > pantograph > cab_door`；`none`（左右广角）**不进入**推理队列。
   - `frame_due(seq, interval)`：每 N 帧推理一次（降频）。
   - `plan_inference`：按优先级 + **NPU 预算**排队；超温触发**温度守卫**降载（`budget/2`，仍运行，**不静默停**）。
   - `AiRuntime`/`AiChannelRuntime`：逐路 `model_version/fps/latency_ms/last_result_ms/dropped`。
   - `ai_status_level`：worker 停 → `Unavailable`（“AI 不可用”）；有路结果陈旧/超温 → `Warning`（降级）；否则 `Ok`。
   - `ai_alarm_annotation(ai_available)`：AI 不可用时相关检测类报警标注“AI 不可用”（不静默）。
   - `build_ai_debug`：仅 `ai_enabled` 相机的调试视图（框/ROI/置信度/耗时/模型版本/最近结果）。
2. **`railhmi-media` 推理工作线程** `AiWorker`：独立 `std::thread` 消费每路有界队列，按帧间隔调用可插拔 `Detector`，维护 `AiRuntime` 遥测；`start/stop` 明确 `join`，退出清理干净。附 `SimDetector`（**替代输入，非 RKNN**）用于无模型时验证线程/调度/遥测。
3. **配置化**：`app.yaml` 新增 `ai:`（`inference_interval`/`queue_depth`/`npu_budget`/`temp_limit_c` + 按职责登记 `models` 名称/版本）；`config.cpp` 解析并校验（具体字段路径报错）。
4. **QML AI 调试页** `P5AiDebug.qml`：每路画面叠检测框/ROI（叠加层**默认关闭**）+ 右侧指标面板（模型版本/FPS/延迟/最近结果/置信度）；推理停时每路显式“AI 不可用”。`main.cpp` 注入 `railhmiAiDebug`（未启动推理线程时按“AI 不可用”呈现）。
5. **两个探针**：`railhmi-ai-probe`（core，无 Qt）、`railhmi-ai-runner`（media，真实线程；运行→停止）。

## 2. 实测（板端 RK3588 本机）

### 2.1 TestRun（core 单测 + ai-probe）

`board_build.py --mode TestRun` → `TESTRUN_EXIT=0` `AI_EXIT=0`。核心单测 **480 passed / 0 failed**（含 ITEM-017 新增推理断言）：
```
[PASS] third push drops oldest / queue depth 2, dropped 1 / oldest kept is seq 1 / newest is seq 2
[PASS] front > wheel_rail / wheel_rail > pantograph / pantograph > cab_door
[PASS] none not eligible (两侧广角不入队) / frame interval gating
[PASS] budget 4 -> 4 cameras run / priority order front>wheel_rail>pantograph>cab_door
[PASS] budget 2 -> top 2 priority / rest skipped / wide-angle cameras not queued
[PASS] over-temp triggers guard / over-temp degrades but does not silently stop / budget halved to 2
[PASS] worker stopped -> nothing scheduled
[PASS] worker down -> AI Unavailable / AI text = AI 不可用 / detection alarms annotated AI 不可用
[PASS] worker stopped -> every AI channel marked unavailable (not silent)
```

`railhmi-ai-probe`（板端原文节选）：
```
type=config interval=5 depth=2 budget=4 temp_limit=85 models=4
type=queue camera=CAM-01 profile=front depth=2 dropped=35 inferred=10 requested=10 fps=5 model=rknn-2.3.2/yolov5s
type=queue camera=CAM-05 profile=wheel_rail ... model=rknn-2.3.2/yolov8s-seg
type=schedule temp=60 run=CAM-01,CAM-05,CAM-04,CAM-06 skipped= guard=false
type=schedule temp=90 run=CAM-01,CAM-05 skipped=CAM-04,CAM-06 guard=true
type=debug camera=CAM-01 profile=front available=true model=rknn-2.3.2/yolov5s latency=33 boxes=1 conf=80 last=<1 秒前
type=ai-status stopped=AI 不可用
type=acceptance worker_down_status=AI 不可用 alarm_annotation=AI 不可用 debug_all_unavailable=true temp_guard_degrades=true wide_angle_excluded=true
type=result PASS
```

### 2.2 MediaCheck（推理工作线程）

`board_build.py --mode MediaCheck` → `BUILD_EXIT=0` `AIRUN_EXIT=0`（同时复核 ITEM-013/015 取流探针回归 `PROBE_EXIT=0`/`MULTI_EXIT=0`）。`railhmi-ai-runner --duration 4`（板端原文）：
```
type=worker start interval=5 depth=2 cameras=6 detector=sim(substitute,not-rknn)
type=channel camera=CAM-01 profile=front fps=5 latency=33 dropped=79 model=rknn-2.3.2/yolov5s
type=channel camera=CAM-04 profile=pantograph ... / CAM-05 wheel_rail(yolov8s-seg) / CAM-06 cab_door
type=ai-status running=运行 eligible=4 with_result=4
type=ai-status stopped=AI 不可用
type=acceptance worker_stopped=true ai_unavailable=true channels_ran=4/4 result=PASS
```

判据核对（用户可观察点）：
- 仅 `ai_enabled` 相机进入推理（`cameras=6` 中 `eligible=4`，左右广角 CAM-02/03 排除）✅
- 有界丢旧帧 + 帧间隔降频（`depth=2 dropped=79`；50 帧请求 10 次推理）✅
- 优先级调度 + NPU 预算 + 温度守卫（`temp=60 run=4`；`temp=90 run=2 guard=true` 仍运行）✅
- 上报模型版本 / 推理 fps / 最近结果时间 / 耗时（`model=.../fps=5/latency=33/last=<1 秒前`）✅
- **停掉推理线程** → `stopped=AI 不可用`，报警标注 `AI 不可用`，调试页每路 `unavailable`（不静默）✅

## 3. 边界

- **未绑定 RKNN 张量/RKNN 后处理**：本轮用 `SimDetector`（替代输入，**非 RKNN**）驱动线程/调度/遥测与“不可用”语义；`yolov8s-seg`/`yolov5s` 作为**模型版本**按职责登记并在遥测/调试页消费。模型文件加载、NV12 前处理、后处理精度由**视觉/模型职责**提供（ROLE：视频和模型模块由对应工程角色提供），应用只消费 `模型版本/框/ROI/置信度/耗时`。`Detector` 接口已就绪，接入真实模型不改本层调度/队列/UI。
- **真机 AI 端到端（ENV-D）未执行**：真实相机帧 + RKNN 结果未跑；取流侧 test 源已复现（ITEM-013/015）。
- **QML 运行期未执行**：板端缺 Qt5 Quick/Qml 开发包（E-01b）；本项以 core/media 板端实跑 + 探针 + QML 源码/静态扫描 PASS 为证据。
- 应用进程尚未把 `AiWorker` 接入 QML 运行时；`main.cpp` 以“AI 不可用”占位驱动调试页（如实，不虚构结果）。

## 4. 证据文件

- `build-TestRun-...-ITEM017-r3.*`（权威轮：core 单测 480/0 + ai-probe PASS + 状态/仿真探针回归）
- `build-MediaCheck-...-ITEM017-media-r1.*`（media 构建 + ai-runner 线程 PASS + 取流探针回归）
- `...-ITEM017-r1.*` / `...-ITEM017-r2.*`（失败轮留痕：r1 缺 `StatusLevel::Degraded` 枚举编译失败；r2 YAML 流式映射 `{...}` 未被解析致配置非法；均已修复）

## 5. 复现

```sh
# 板端
bash app/build.sh CoreTests && ./build-core/tests/railhmi-core-tests
./build-core/tools/railhmi-ai-probe --config ./config
bash app/build.sh MediaCheck && ./build-media/tools/railhmi-ai-runner --config ./config --duration 4
# 经登记 SSH 入口
python app/tools/board_build.py --host <addr> --user <user> \
  --credential-env TRAIN_RK3588_SSH_CREDENTIAL --mode TestRun \
  --outdir docs/hmi/test_evidence/ITEM-017-inference --stamp <tag>
python app/tools/board_build.py --host <addr> --user <user> \
  --credential-env TRAIN_RK3588_SSH_CREDENTIAL --mode MediaCheck \
  --outdir docs/hmi/test_evidence/ITEM-017-inference --stamp <tag>
```
