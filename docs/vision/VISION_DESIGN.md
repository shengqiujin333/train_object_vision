# 视觉方案设计 — 六路可见光视觉任务、数据、模型与推理应用（VisionDesignArtifact）

- Artifact: VisionDesignArtifact（视觉任务/数据/模型/推理应用方案与决策依据）
- Capability: `computer_vision_engineer.vision_design`
- 版本: v1（2026-10-18，基于本次接手 HEAD；接口基线 `docs/interfaces/INTERFACE_CONTRACT.md` v1.0.0）
- 上游输入（均已批准/已登记）:
  - `docs/interfaces/INTERFACE_CONTRACT.md` v1.0.0（IF-02/IF-04/IF-05 帧、推理、语义边界；本设计不新增/不改 schema、C++ ABI、YAML 或 DDL）
  - `docs/video/VIDEO_SYSTEM_DESIGN.md` v1（帧租约 FrameLease/FrameDescriptor、压缩分支、恢复）
  - `app/config/cameras.yaml`、`app/config/app.yaml`、`app/config/thresholds.yaml`、`app/config/roi.yaml`（唯一可执行配置来源）
  - 现有应用代码：`app/src/core/include/railhmi/core/inference.h`、`rules.h`、`config.h`；`app/src/media/include/railhmi/media/{ai_worker.h,ai_dispatcher.h}`
  - 历史模型资产与实现：`models/{yolov8s-seg.rknn,yolov5s.rknn,rail_labels.txt}`、`code/{main.cc,rknpu2_postprocess.cc,yolov5_person.cc,yolov5_postprocess.cc,person_measure.cc,postprocess.h}`
  - 历史训练/数据记录：`PROJECT_GUIDE.md`、`E:\work\rail_seg_train\`（含 `feasibility_500m_person_detection.md`）、`E:\work\railway.v4i.yolov8\`
  - 用户需求：`一些说明和要求.txt`（第 10/11 条职责与"当前可同模型、将来换模型"）
  - Runtime 统一环境与授权汇总（`runtime/environment.yaml` + `runtime/permissions.yaml`）
- 本次执行范围: **只做视觉方案设计**。未连接相机、未取流、未推理、未训练、未转换、未编译、未部署、未改产品源码。板端文档与历史训练环境本轮未在线核验；数据集与历史脚本只做**本机有限只读核对**（见 §5、§11）。

> 事实口径（全文统一）：**本轮本地核对**＝本次在开发机上对登记文件/目录的只读检查原始结果；**仓库既有证据**＝既往证据/文档（注明来源，不外推为本轮通过）；**待实测**＝有明确测量方法但本次未执行；**待确认决策**＝需需求方/现场确认的开放项。

---

## 0. 范围、职责与硬约束

### 0.1 本能力负责什么

把"轨道异物/人形入侵 + 各相机职责"的产品目标，转成**可追溯的视觉任务定义、数据策略、模型/量化选择、推理应用（预处理/后处理/结果语义）与评价方法**，并给出推理应用实现的有序增量。

### 0.2 不由本能力负责（明确交回）

| 事项 | 归属 |
|---|---|
| 取流/解码/格式转换/帧租约/队列与背压 | 视频系统职责（`docs/video/VIDEO_SYSTEM_DESIGN.md` 已定义） |
| Qt 纹理上屏、P1/P4/P5 页面结构与叠加渲染 | 桌面 UI 职责 |
| 规则/级别映射/多帧确认/事件持久化 | core 规则与事件服务职责（`rules.h`） |
| 数据集整理与固化、训练执行、ONNX/RKNN 导出与量化 | 数据准备/训练/模型导出对应能力（本设计只给策略、事实与接口；**不写入推理实现任务队列**） |
| 模型精度与检出率的独立验收、真机联调、人工视觉确认 | 测试角色 + 用户（视觉内容必须由用户核验） |
| NVR/门磁/受电弓专用硬件协议 | 未提供，保持未接入/未配置 |

### 0.3 不可违反的推理侧硬约束（来自接口契约，非本设计新增）

1. **只消费不可变帧租约**：真实模型适配器消费 `FrameLease`／`FrameDescriptor` 的像素，**不得**从 `FrameRef` 元数据推导像素，**不得**传已 `unmap` 的裸地址。
2. **同帧同代**：分割与检测必须来自同一 `(session_id,camera_id,generation,seq)`；跨帧缓存若无显式最大年龄与失配策略，**禁止**作为实时报警依据。
3. **AI 不可用 ≠ 安全**：无模型/无结果时返回 `unavailable + reason + 空框`，不得用空框冒充"已检测、无目标"。分割失败**不得**退化成"轨道内安全"。
4. **掩码独立交接**：掩码是以原图坐标表示的独立二值 plane 租约（0/非零），尺寸/stride/所有权规则同帧；**不得**把 RGB 掩码当 bool，且 `mask` 不进入现有 core `DetectionResult`。
5. **未知不当正常**：`unknown` 不映射到 `normal`；未知 label 被白名单拦截。
6. **职责能力需模型证据**：`ai_enabled=true` 与配置里登记的模型名**都不构成识别能力**；只有加载并核验了声明能力的模型才 `ready`。
7. **无凭据泄露**：视觉侧不读相机凭据；日志/工件不含凭据、含鉴权 URL 或 pipeline 原文。

---

## 1. 视觉任务判定（逐路，不默认全技术栈）

任务类型按"该相机部署要求"与"现有模型证据"分别判定，**不因为系统里有分割/检测/跟踪就把它们都塞给每路**。

| 相机 | profile | 部署要求（`一些说明和要求.txt` 第 11 条） | 判定任务类型 | 现有模型证据 | 当前可用性 |
|---|---|---|---|---|---|
| CAM-01 前方 | `front` | 前方障碍物、人，提醒刹车 | **语义分割（rail ROI）+ 目标检测（person）+ 时序确认**；可选跟踪与测距 | `yolov8s-seg`(rail) + `yolov5s`(COCO person) | **有模型资产**；精度待重测 |
| CAM-02 左侧 | `none` | 大视野，不检测 | 无检测任务（仅显示） | 无 | 正确置为不推理 |
| CAM-03 右侧 | `none` | 大视野，不检测 | 无检测任务（仅显示） | 无 | 正确置为不推理 |
| CAM-04 车顶 | `pantograph` | 受电弓三态：升弓正常/不到位/未升弓 | **状态分类（3 类）**（必要时先定位再分类） | **无专用模型证据** | **unavailable**（`UNSUPPORTED`/`MODEL_MISMATCH`） |
| CAM-05 车底 | `wheel_rail` | 车轮是否脱离轨道 | **异常检测/分割/状态判别** | **无专用模型证据** | **unavailable** |
| CAM-06 司机室 | `cab_door` | 两侧门是否关闭正常（门磁为未定替代方案） | **状态分类（2 类）** 或非视觉门磁 | **无专用模型证据** | **unavailable** |

关键结论：

- **本期真正有模型证据的只有 CAM-01 的"轨道分割 + 人形检测"**。受电弓三态、轮轨脱离、车门状态**没有任何当前模型证据**；配置里 `app.yaml app.ai.models` 把 `pantograph/cab_door` 指到 `yolov5s` 只是**登记意图**，`yolov5s` 是 COCO 80 类检测器，不会因此获得受电弓/车门语义。
- 用户需求第 10 条"当前各摄像头可以使用相同的模型，将来替换为各相机专用模型"应这样落地：**允许**先用同一套通用检测器在各路跑"目标/占用"级别的探测，但**只对已声明并核验了语义映射的 profile 产生业务报警**；未核验的职责保持 `unavailable + unknown`，不产生业务判定。这不是"所有相机已具备检测能力"。
- CAM-02/03 `ai_profile=none` **永不进入推理队列**，即使将来模型可用也不得产生检测类报警（左右只有显示职责）。
- 门磁是 CAM-06 的另一方案，属**非视觉**输入，未定；本设计不假设其接入。
- **分割 ≠ 轮轨脱离**、**轨道分割 ≠ 异物检测**：不得把 rail mask 当作任何其他职责的语义。

---

## 2. 输入/输出契约与推理接口

### 2.1 输入（media → 视觉适配器）

- 消费 `FrameLease`（不可变、共享所有权）与其 `FrameDescriptor`：`camera_id/session_id/generation/seq`、`capture_mono_us/capture_wall_ms/pts_ns`、`width/height`、`format ∈ {NV12,BGR888,RGB888}`、`colorimetry`、`planes[{offset,stride_bytes,size_bytes}]`、`buffer_bytes`、`memory_kind=cpu`。
- 按 caps 计算 stride/padding，**不假定连续 `width×height`**；NV12 的 Y/UV stride 来自描述符。
- 通道顺序只在模型适配器内显式转换（BGR→RGB），**不得用格式名掩盖通道颠倒**。

### 2.2 输出（视觉适配器 → 语义/规则/调试）

主输出是接口契约 §6 的 `InferenceEnvelope`（设计扩展，字段语义固定，不由本设计新增）：

```
InferenceEnvelope {
  # 身份：与输入 FrameDescriptor 完全一致
  session_id, camera_id, generation, seq,
  profile,                       # AiProfile
  capture_mono_us, result_mono_us,
  latency_ms,                    # 完成 − 接收，含排队
  model_version,                 # 运行时实际模型身份，不用配置名覆盖
  availability,                  # ready | unavailable
  reason,                        # 稳定 reason token（脱敏）
  boxes: [ { x,y,w,h,            # 原图归一化，x+w≤1, y+h≤1
             confidence, class_id, label,
             track_id,           # int32 或 null
             distance_m } ],     # -1 = 未知
  masks: [ { box_index, plane_lease } ]   # 原图坐标二值 plane，独立所有权
}
```

- 现有 core `DetectionBox`（`inference.h`）字段 `x/y/w/h/confidence/class_id/label/in_roi/distance_m` 继续作为语义层入参；语义适配器把 `InferenceEnvelope` 映射为 `core::Detection` 后才进 `RuleEngine`。
- `DetectionResult.ts_ms` 现为"结果时刻"；真实适配器须**明确改用完成时刻**并另存 capture/result 两时刻（契约要求，不改 core 签名，由 envelope 承载）。
- 空结果表达：`availability=ready` 且 `boxes` 为空 = "本次推理成功、无目标"；`availability=unavailable` = "没有能力/失败"。**两者语义不同**，UI/事件/快照标注必须区分。

### 2.3 与现有应用代码的接口对齐

- 媒体侧已有可插拔 `media::Detector`（`ai_worker.h`：`model_version(profile)`/`ready()`/`infer(FrameRef,profile)`）与 `SimDetector`（替代检测器，**非 RKNN**）。真实适配器实现 `Detector` 并替换 `SimDetector`；当前 `infer` 入参仍是 `FrameRef` 元数据，真实像素经 `FrameLease` 补充，属实现增量（见任务清单）。
- 调度侧 `core::plan_inference` 已定义按 `inference_interval`/`npu_budget`/温度守卫的路数预算；视觉适配器**不重写调度**，只被其调用。
- 调试视图 `build_ai_debug` 已定义 P5 字段（`model_version/available/unavailable_reason/fps/latency_ms/boxes/confidence/distance_text`）。视觉侧负责把真实结果填进去；掩码叠加仅在掩码真实可用时提供。

---

## 3. 标注语义、类别与映射（白名单）

| 模型 | 类别 | 语义 | 来源 |
|---|---|---|---|
| `yolov8s-seg` | 1 类：`rail` | "可行驶/可通行轨道区域"的二值/软掩码 | `models/rail_labels.txt`（单行 `rail`）、`postprocess.h` `OBJ_CLASS_NUM=1` |
| `yolov5s` | COCO 80 类 | 只使用 **cls_id==0 `person`** | `yolov5_postprocess.h` `YOLOV5_OBJ_CLASS_NUM=80`；`code/main.cc` 过滤 `cls_id==0` |

映射规则（严格白名单，交语义适配器实现）：

| profile | 允许进入 `RuleEngine` 的语义 | 报警类型（core 现有映射，不改） | 禁止 |
|---|---|---|---|
| `front` | `person`（框中心在 rail mask 内）/ 受支持的障碍物语义 | `PersonOnRail`(L1)、`FrontObstacle`(L1) | 未知 label、COCO 其它 79 类直接进规则 |
| `wheel_rail` | 经标定语义的 `wheel_abnormal` | `WheelDerail`(L1) | 无模型证据时不得上报 |
| `pantograph` | `unknown/not_raised/normal/abnormal` | `PantographAbnormal`(L2) | `unknown` 当 `normal` |
| `cab_door` | `unknown/closed/ajar` | `DoorAjar`(L2) | 无模型证据时不得上报 |
| `none` | 无 | 无 | 任何检测类报警 |

- **未知 label 一律拦截**：现有规则会把"非 person 都归为障碍"，语义适配器必须在进规则前做白名单，避免 COCO 的 car/truck 等被当成前方障碍而误报。
- **`unknown` 不映射为 `normal`**：受电弓/车门在无证据或推理失败时保持 `unknown`，不产生"正常"结论。
- **左右（none）永不产生检测类报警**；设备类报警（离线/冻结）由流状态机产生，不属视觉。
- 轨道掩码语义边界：掩码表示轨道区域，**不等于**"脱轨"或"障碍物"；不能拿 rail seg 结果推断轮轨状态。

---

## 4. 评价指标与业务错误代价

### 4.1 分层指标（不同层次分别记录，不互相替代）

| 层次 | 指标 | 测量位置 | 状态 |
|---|---|---|---|
| 训练/离线评估 | Box mAP50、Box mAP50-95、**Mask mAP50、Mask mAP50-95** | `best.pt` 在固定 test 集（`eval.py`） | 历史值见 §4.2；**须重测** |
| 量化后（ONNX→RKNN INT8） | 与 FP/ONNX 的 mAP 差、掩码 IoU 差 | 转换后同一 test 集（主机/板端模拟） | **待实测**（INT8 对掩码质量的影响是本项目已知风险） |
| 板端运行时 | 单帧检出率、置信度分布、p50/p95 延迟、FPS、NPU/温度 | RK3588 真机 + 固定预置位连拍 | **待实测**（需相机授权） |
| 操作/业务 | 单帧检出率、空场景误检率、时序平滑后检出率 | `feasibility_500m_person_detection.md` §4.2 方法 | 方法已定义；数值**待现场实测** |
| 人工视觉 | 实际画面中框/掩码是否合理、有无漏报误报 | 用户核验 | 只能由用户完成 |

### 4.2 既有历史数字（仓库既有证据，**不等同当前验收**）

- `PROJECT_GUIDE.md`：yolov8s-seg 训练 Box mAP50=0.965、Box mAP50-95=0.920、Mask mAP50=0.957、**Mask mAP50-95=0.877**；epoch 157 早停。该评估的 test 集极小（见 §5.3），**不能外推为当前通过**。
- `feasibility_500m_person_detection.md`（2026-09-17/18，Dahua .65，500m 实测）：站立人 500m 约 89 px 高（模型 640 内约 44px），静态相机 conf 0.59–0.61，连拍 5/5 检出；相机巡航/边缘时 conf 跌到 0.27–0.54。该报告给出操作级判据（下）。

### 4.3 业务错误代价（决定阈值与判据方向）

- **漏报（FN，人在轨道内未报警）= 安全关键**，代价远高于误报。因此：**召回优先**，宁可多报警，不可漏报；`confidence_min` 等阈值不得为压低误报而牺牲漏报。
- **误报（FP）= 报警疲劳/信任度下降**，用**时序多帧确认**（core 已有：3 帧、相邻 ≤500ms）和抑制窗口降低，**不用**单纯抬高单帧阈值。
- **能力缺失必须显式**：AI 不可用、模型不匹配、分割失败要显示为不可用/未知，不能静默成"安全"。
- **报告口径**：模型能力不足导致的未检出 ≠ 软件缺陷；必须区分"无模型证据（unavailable）"与"有模型但未检出（FN）"。

### 4.4 判据来源与待确认决策

- 单帧检出率 ≥70%（试验级）/≥95%（告警级）、置信度中位数 ≥0.4/≥0.5、空场景误检 ≤5% 帧/≤1 次·天·路、时序平滑后 ≥90%/≥99%：**来源**＝`feasibility_500m_person_detection.md` §4.2（历史现场方法），**待确认**为正式验收判据。
- 分割侧判据（Mask mAP / 掩码 IoU / 轨道区域覆盖率）**尚无需求数字** → **待确认决策**（见 §10）。
- `confidence_min`、`NMS`、掩码阈值、`inference_interval`、搜索/分块策略 → 配置或待确认决策，**不得写死**。

---

## 5. 数据事实与数据策略

### 5.1 数据集登记事实

| 项 | 值 | 来源/状态 |
|---|---|---|
| 位置 | `E:\work\railway.v4i.yolov8\`（Roboflow 导出） | 本轮本地核对存在 |
| 项目/版本/许可 | `railway-g7ct2` v4，**CC BY 4.0**（需署名） | 本轮本地核对 `data.yaml` |
| 类别 | `nc: 1`，`names: ['rail']` | 本轮本地核对 |
| 图像尺寸 | 640×640（抽样 400 张 train 全为 640×640 JPEG） | 本轮本地核对 |
| 标记类型 | 实例分割多边形（`.txt` 多列坐标） | 本轮本地核对 |
| 数据来源 | Roboflow Universe 公开数据集（第三方）；**非本项目相机采集** | 本轮本地核对 |

### 5.2 本轮实测的划分与标签质量（重要：与历史文档数字不一致）

| 划分 | 图像 | 纯 bbox 标签 | 纯 seg 标签 | 混合 | 空标签 | 实例数 |
|---|---|---|---|---|---|---|
| train | 5659 | 334 | 5290 | 15 | 20 | ~6932 |
| valid | 111 | 2 | 106 | 0 | 3 | ~118 |
| test | 107 | 6 | 99 | 1 | 1 | ~116 |

- **标签格式污染**：存在 bbox 格式标签（`class + 4 数字`），分割训练会报 `len(segments) != len(boxes)`（`PROJECT_GUIDE.md` §9.4）；本轮实测 train 有 334 个纯 bbox、15 个混合文件，valid 2、test 6。**必须清洗后重新固化版本**，不能沿用历史已删版本的数字。
- **版本不一致**：`PROJECT_GUIDE.md` 记载清洗后 train 349/valid 2/test 7，与本机实际的 5659/111/107 差异巨大，说明本机目录与历史训练用的数据快照不是同一版本。**训练/评估前必须先冻结数据版本并记录哈希**，否则历史 mAP 无法归因。
- **严重不平衡与偏小验证集**：valid 111 / test 107（其中还有 bbox 与空标签污染）对 mAP 结论的置信区间很宽；当前 test 不能作为正式验收依据。
- **PS 合成异物**：部分图像含 PS 合成的车/人/兔子等，对 rail seg 是遮挡增强，**没有异物类标签**，**不能**用于训练异物检测（`PROJECT_GUIDE.md` §4）。
- **无 person-on-rail 标签**：数据集只有 rail 类，没有"人在轨道内"的成对标签；front 的判定靠"COCO person 框 ∩ rail 掩码"的后处理组合，而非训练标签。
- **域差异（关键）**：数据集为通用轨道俯视/侧视图像，而 CAM-01 是**车内隔着玻璃、长焦、最远 500m** 的场景；光照、大气扰动、玻璃反光、运动模糊都与训练域不同。**不能假定离线 mAP 直接等同于现场检出率。**

### 5.3 为数据准备/训练能力提出的数据策略（不进推理实现队列）

- **划分原则**：按**采集来源/视频会话/时间**划分 train/valid/test，**禁止**同一段视频的相邻帧跨集合（防泄漏）；正式验收需要一个**冻结的、带哈希的现场测试集**。
- **泄漏防护**：Roboflow 的随机划分可能把同源近帧分到不同集合；重新冻结时按源分组。
- **数据不足**：异物（树枝/碎石）与"人在轨道内"缺标签 → 需要**现场采集 + 标注**（含站立/躺卧/遮挡/昼夜/距离梯度）；在采集完成前，对应能力保持 `unavailable`，不用合成数据冒充能力。
- **域变化**：需要现场相机数据微调（feasibility 报告建议 500–2000 张躺卧样本）；玻璃反光/雨雾/夜间逆光为已知风险，需覆盖不同时段。
- **空帧与未知类**：空场景（无目标）必须有代表性样本以避免误报；未知类必须被白名单拦截而不是"归为障碍"。
- **许可**：CC BY 4.0 可用于项目，须在 `packaging/RESOURCES.md` 保留署名与来源 URL；现场自采数据的授权/隐私（人像）需确认。

### 5.4 现有标签/资产清点

- `models/rail_labels.txt` = `rail`（1 类）；`models/yolov8s-seg.rknn`（13.3MB，640×640 NHWC INT8，13 输出）；`models/yolov5s.rknn`（8.5MB，640×640 NHWC INT8，COCO 80）。
- 模型来源/许可记录见 `packaging/RESOURCES.md`（本设计只在需要时引用，不重复登记）。

---

## 6. 模型复用 vs 训练、算力与量化

### 6.1 候选比较（按证据选择，不预设模型家族）

| 候选 | 能力 | 证据 | 结论 |
|---|---|---|---|
| **复用 `yolov8s-seg`(rail) + `yolov5s`(person)** | 轨道分割 + 人形检测两阶段 | `models/` 资产、`code/main.cc` 现有两阶段逻辑（person 框中心点 ⊂ rail mask → 报警）、历史 mAP | **本期基线**（CAM-01） |
| `yolov8s` 检测**微调**（自建异物/人形类） | 端到端检测，少一次分割 | 无训练标签证据 | **待数据**：需现场标注后才能评估 |
| 分块/ROI 推理（2×2 上采样） | 提升远距离小目标（躺卧 8–12px→16–24px） | `feasibility_500m_person_detection.md` §5.2 推算 | **保留候选**，算力 ×4，需实测 |
| 专用分类/检测模型（受电弓三态、轮轨、车门） | 状态语义 | **无模型证据** | **待训练**；在此之前 `unavailable` |
| 轻量跟踪（如 IoU/ByteTrack 级） | 稳定 `track_id` 支撑抑制与确认 | 无模型证据，规则需要 | **推理应用增量**（见任务清单） |
| 红外/热成像 | 夜间备份 | 本期范围外（只接 6 路可见光） | 不接入 |

- 复用优先策略：**先复用已有权重跑通推理链路与后处理，再用现场数据微调**；不得因为没有专用模型就把未核验的职责标成可用。
- 不按项目名/路径/地址写特判；模型按 `ai_profile` 与配置解析，差异只由当前输入与配置表达。

### 6.2 训练环境与部署环境（分别设计）

| 环境 | 事实 | 状态 |
|---|---|---|
| 训练（历史） | 云 GPU Tesla T4 16GB（`36.129.34.186:6001`），`rail_seg_train/`（`train.py`/`eval.py`/`export_onnx.py`/`export_rknn_format_onnx.py`） | 环境登记 `enabled=false`：**仅历史记录，不是当前可执行入口，不自动使用付费云资源** |
| 本地导出 | Windows 可训练/导出 ONNX（`E:\work\rail_seg_train\yolov8s-seg.pt` 存在） | 本轮本地核对存在；能否复现训练未核验 |
| RKNN 转换 | 需 `rknn-toolkit2` **2.3.2**（Python 3.8–3.12，不支持 3.13）；板端文档记录 conda env `~/miniconda3/envs/rknn`（py3.8.20） | **待实测**（未在线核验） |
| 部署推理 | RK3588 + `librknnrt 2.3.2` + rknpu 驱动 2.3.0a，3 NPU 核 | 仓库既有证据/README 文档基线 |
| 部署无训练能力 | 板端**不承担训练**；不假定支持某个加速器做训练 | — |

### 6.3 导出/量化设计（交模型导出能力执行）

- ONNX 导出沿用历史 13-tensor 格式（monkey-patch `Detect/Segment.forward` 复刻 airockchip 输出布局，`export_rknn_format_onnx.py`），因现有 `rknpu2_postprocess.cc` 按 13 输出编写。
- RKNN 量化：`quantized_dtype='w8a8'`、`quantized_method='channel'`、`optimization_level=3`、`mean=0/std=255`、校准集 train 前 200 张（`convert_rknn.py`）。
- **量化后必须重新评估**：掩码 mAP/IoU 的 INT8 退化是已知风险；校准集应覆盖现场域（当前校准集是通用轨道图，与 CAM-01 域不一致）。
- 输入规格：640×640 NHWC INT8；输出张量数量/形状/布局/量化参数**运行时用 `rknn_query` 读取**，不照抄常量。

### 6.4 算力与延迟预算

- 既有证据：`feasibility_500m_person_detection.md` §6 记录 yolov5s@640 单核推理约 **12–15ms**，RK3588 共 3 核；历史双模型 demo 实测 **15.8–18.8 FPS**（单路双模型，非六路并发）。
- 本项目调度：4 路启用推理（front/wheel_rail/pantograph/cab_door），`inference_interval=5`、`queue_depth=2`、`npu_budget=4`、`temp_limit_c=85`。
- **`npu_budget=4` 是调度路数预算，不是 NPU 核数，也不是已验证的算力容量**（契约明确）。
- **待实测**：每 profile 的单次推理延迟、4 路并发稳态 FPS、分块/两级模型叠加后的余量、热稳态与 `temp_limit_c` 触发行为。六路**全模型推理**不等于六路显示能力（历史双路 FPS 不能外推）。
- 内存：板端 7.7GiB；RKNN 模型与掩码缓冲需实测 RSS 稳态。

---

## 7. 推理应用：预处理/后处理规范

### 7.1 预处理

1. 从 `FrameLease` 取像素：NV12 经已有 `librga imcvtcolor` 转 BGR888（BT601_FULL，接受两个成功码）或直接用 `BGR888/RGB888` 视图。
2. **BGR→RGB 只在适配器内显式转换**（YOLO 训练为 RGB）。
3. **Letterbox**：等比缩放到 640×640，短边补边；记录 `scale` 与 `pad_x/pad_y`，供后处理逆变换；不拉伸变形。
4. **量化输入**：按输入张量 dtype（INT8/UINT8）与 `scale/zero_point` 写值；`mean=0/std=255` 归一化与导出配置一致。
5. 预处理失败（格式不支持、stride 非法、尺寸异常）→ 显式错误并计数，**不静默丢帧**。

### 7.2 后处理（YOLOv5 检测）

- 解码 3 个尺度；置信度过滤（阈值来自配置，历史默认 `BOX_THRESH=0.25`）、类别条件、NMS（历史默认 `NMS_THRESH=0.45`，IoU）。
- letterbox 逆变换回原图；输出**归一化** `x/y/w/h` 且 `x+w≤1, y+h≤1`（越界裁剪）。
- 仅保留白名单类别（person）；其余类别丢弃（可计数用于诊断）。
- 阈值/NMS 可配置，**不得**写死为历史常量。

### 7.3 后处理（YOLOv8-seg 分割）

- 现有参考实现（`rknpu2_postprocess.cc`）按 **13 个输出**编写：3 个尺度 × {box(DFL)、score、score_sum、seg 系数} = 12 个张量 + 1 个 proto（`PROTO_CHANNEL=32`、`PROTO_HEIGHT/WIDTH=160`）；`dfl_len = output_attrs[0].dims[1]/4`、`output_per_branch = n_output/3`。
- 掩码：`per-box seg 系数(32) · proto(32×160×160)` 矩阵乘 → 阈值化 → 逆 letterbox 到原图 → 二值 plane。
- **实现必须用 `rknn_query` 实际返回值核对张量数量/形状/布局/量化**，数量或形状不符时返回 `MODEL_MISMATCH`，**不能**只按文件名判断"ready"（契约要求）。
- 掩码输出：原图尺寸、`0/非零`、独立所有权/ stride 规则同帧；`mask` 不进 core `DetectionResult`。
- 阈值（如 0.5）**待确认决策**且可配置；不得把软掩码直接当布尔。

### 7.4 两阶段判定（CAM-01）

```
同帧(同 session/generation/seq)
  rail seg → rail mask + rail 框
  person detect → person 框
  for each person box:
      center = (x+w/2, y+h/2)
      if center ∈ rail mask: label=person, in_roi=true   → PersonOnRail(L1)
      else: 丢弃（不在轨道内，不报警）
  如配置 danger_zone：与 roi.yaml 的 ROI 求交后再判定升级
```

- 分割失败/掩码空 → **不产生"轨道内安全"结论**；该帧标记不可用并计入诊断。
- 若复用分割缓存：必须实现并登记**最大年龄**与失配策略，默认关闭；跨 generation 缓存**禁止**用于实时报警。
- 现有 `code/main.cc` 的点在掩码内判定可作参考，但实现须处理 letterbox 逆变换后的坐标系一致（框与掩码必须都在原图坐标）。

### 7.5 时序与跟踪

- 规则的多帧确认（3 帧、相邻 ≤500ms）需要**稳定的目标关联**；当前无跟踪模型。
- 优先方案：轻量跟踪/关联（IoU + 运动预测级别）产生 `track_id`；次选方案：显式、可验证的相机级事件适配（不产生 track_id，但保证同一目标不重复开事件）。
- **禁止**把所有目标映射为 `track_id=0`（会累计三次误认同一目标）；跟踪不可用时 `track_id=null`。
- 失帧/新 generation 必须清理确认与跟踪状态（需受控接口，不能整路误清）。

### 7.6 距离测量（可选，后置）

- 历史标定：CAM-01 500m 处站立人 ≈89px → **52 px/m**（该档变焦下）；`person_measure.cc` 提供像素测量。
- 只有提供相机标定（含变焦档位）时才由框像素高度换算 `distance_m`；未标定时恒为 `-1`，**不用框尺寸臆造米值**（契约）。
- 当前 `danger_proximity_m` 规则**未消费**距离，不得写成"已实施 5m 门槛"。

---

## 8. 接口说明与跨角色边界

### 8.1 视觉 ↔ 视频系统

- 视频侧提供：`FrameLease` + `FrameDescriptor`（同 generation 身份、capture 时刻、format/stride 来自 caps）、有界丢旧 AI 队列（`ai_dispatcher`）。
- 视觉侧提供：消费租约的预处理、`InferenceEnvelope`（同身份、result 时刻、latency、boxes/mask）、可用性语义。
- 视觉侧**不**改取流/解码/状态机；格式/stride 缺失或非法时返回显式错误。

### 8.2 视觉 ↔ core 规则/事件

- 视觉侧产出 `core::Detection`（白名单、`in_roi`、归一化坐标、`confidence`、`distance_m`）。
- core 侧负责级别映射、多帧确认、抑制、事件与确认；视觉侧**不改** `rules.h` 语义、不绕过白名单。
- 结果顺序/陈旧处理在视觉侧完成（拒绝旧 generation/重复 seq/乱序），并计数。

### 8.3 视觉 ↔ 桌面 UI

- P5 AI 调试消费 `build_ai_debug` 字段；叠加层四开关默认关闭；司机不可见（`ai_debug_visible`）。
- 掩码叠加只在掩码真实可用时提供；未知值不得格式化为 0（`unavailable_reason` 必须显示）。

### 8.4 视觉 ↔ 测试/用户

- 模型精度与检出率的独立验收归测试角色；**视觉内容必须由用户核验**，自动截图/数值不能冒充人工确认。
- 真机/现场证据与主机/文件源证据分开归档，互不替代。

### 8.5 交下游推理应用实现

按 `docs/vision/vision_tasks.yaml` 有序实现；先打通"能力核验 → 预处理 → 检测/分割后处理 → 同帧判定 → 结果关联发布"，再接入真实解码器与调度，最后补齐其它职责可用性语义、距离、调试视图与自检。数据整理、训练、ONNX/RKNN 导出**不在**该队列，由对应能力处理。

---

## 9. 风险与限制

| 风险 | 影响 | 缓解 |
|---|---|---|
| 数据集为第三方通用轨道域，与 CAM-01 长焦/隔玻璃域差异大 | 离线 mAP 高但现场漏检 | 现场数据微调；以现场检出率验收；域分层留证 |
| test 集仅 107 张且含 bbox 污染 | mAP 结论不可靠 | 冻结清洗后的现场测试集；重测；报告置信区间 |
| 无 person-on-rail / 异物标签 | 无法训练端到端异物检测 | 两阶段后处理组合 + 现场采集标注；未标注前对应能力 unavailable |
| INT8 量化退化（尤其掩码） | 分割质量下降 | 量化后重评；校准集换现场域；必要时混合量化/保留关键层 |
| 无受电弓/轮轨/车门专用模型 | 三职责不可用 | 保持 `unavailable + unknown`；训练专用模型后再启用 |
| 远距离小目标（躺卧 8–12px） | 漏报 | 分块/ROI 推理（算力 ×4，需实测）；预置位停留检测；多帧确认 |
| 无跟踪模型 | 抑制/确认语义不稳 | 轻量跟踪或显式相机级事件适配；禁止 track_id=0 |
| 大气扰动/夜间/逆光 | 有效分辨率下降 | 现场分时段验收；红外属本期范围外 |
| 掩码坐标/stride 处理错误 | 判定错位 | 同帧身份校验；逆 letterbox 单位测试；掩码与框同坐标系 |
| 4 路并发算力/热稳态未知 | 掉帧/降载 | 实测延迟与温度；调度预算与温度守卫生效；不静默停 |

---

## 10. 待确认决策（需需求方/现场确认，不由本设计单方面冻结）

1. CAM-01 正式验收判据：单帧检出率/置信度中位数/空场景误报/时序平滑后检出率的目标值与时段覆盖（历史方法见 `feasibility_500m_person_detection.md` §4.2，尚未升级为正式验收）。
2. 分割侧判据：Mask mAP / 掩码 IoU / 轨道覆盖率目标（当前无需求数字）。
3. `confidence_min`、`NMS`、掩码阈值、`inference_interval`、分块/ROI 策略的取值与配置归属。
4. 距离测量是否纳入本期：所需标定（含变焦档位）与精度要求；当前规则未消费距离。
5. 跟踪方案：引入轻量跟踪 vs 相机级事件适配；以及跨 generation 确认清理的受控接口边界。
6. 受电弓三态 / 轮轨脱离 / 车门状态：是训练专用视觉模型，还是采用门磁等非视觉方案；对应现场采集与标注计划。
7. 前方"障碍物"语义范围：是否仅在 COCO person 之外训练自建障碍类；未经训练前如何提示"未知障碍"（现有规则兜底语义）。
8. 现场测试集的采集计划、许可与人像隐私处理。

---

## 11. 本次执行事实与未执行声明

- **已执行（本轮本地只读核对）**：登记文件/目录的阅读与统计——`docs/interfaces/INTERFACE_CONTRACT.md`、`docs/video/VIDEO_SYSTEM_DESIGN.md`、`app/config/*.yaml`、`app/src/{core,media}` 相关头/实现、`models/` 资产与标签、`code/` 历史实现、`PROJECT_GUIDE.md`、`README.md`、`E:\work\rail_seg_train\{README.md,convert_rknn.py,eval.py,feasibility_500m_person_detection.md}`、`E:\work\railway.v4i.yolov8\`（划分/标签格式/图像尺寸统计）。均为有限等待的本地只读操作。
- **未执行**：任何相机/板端连接与取流、任何推理/训练/转换/量化/编译/部署、任何板端性能或温度测量、任何人工视觉核验。板端文档与历史训练/转换环境未在线核验，只按仓库既有证据引用。
- 既有 TEST_PASS / RELEASE_READY 保留其原范围，不外推为本视觉方案或模型能力已通过。
- 本设计不替代验证能力的独立验收；模型精度、真机检出率与人工视觉确认均为待执行项。
