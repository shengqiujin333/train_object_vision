# 视觉/推理应用软件测试设计（TestDesignArtifact）

- Artifact: TestDesignArtifact（视觉/推理应用测试设计）
- Capability: `software_tester.software_test_design`
- 版本: v1（2026-10-18）
- 主案: `n015_software_tester_software_test_design`（CALL-7379e666-2393-4387-8ac8-b8d3bc7d6b02）
- 数据源: `docs/vision/VISION_DESIGN.md` v1、`docs/vision/vision_tasks.yaml`（TL-2253ff62 ITEM-001..013）、`docs/interfaces/INTERFACE_CONTRACT.md` v1.0.0（IF-02/IF-04/IF-05）、`docs/video/VIDEO_TEST_DESIGN.md` v1（帧层测试设计，本文件不重复其用例）、`app/config/{app,cameras,roi,thresholds}.yaml`、`app/src/core/include/railhmi/core/{inference.h,rules.h}`、`app/src/media/include/railhmi/media/{ai_worker.h,ai_dispatcher.h}`、`models/{yolov8s-seg.rknn,yolov5s.rknn,rail_labels.txt}`、`code/` 历史两阶段实现
- 边界声明: 本文不新增 core schema、C++ ABI、YAML 字段或 SQLite DDL。模型精度/检出率/误报漏报的数值通过、真机性能定论与人工视觉确认不在本设计内下结论；本文定义**仪器、判据、测量方法与证据格式**。规则级别映射与多帧确认数学已由 hmi core 单测覆盖，本文只验证推理侧白名单入口的拦截行为（G7）。

---

## 1 范围

### 1.1 覆盖域

- 13 个视觉 TASKS ITEM-001..013 的可执行测试方案：模型能力核验、租约预处理、YOLOv5 检测后处理、YOLOv8-seg 13 输出分割后处理、同帧两阶段判定、结果关联与发布、语义白名单接入、跟踪协同、线程/调度接入、其它职责可用性语义、可选测距、AI 调试视图数据、无相机自检夹具。
- 上游硬约束（VISION_DESIGN §0.3 + InterfaceContract §6）直接转成测试判据：
  1. 只消费不可变帧租约（禁止从 FrameRef 元数据推像素、禁止消费 unmap 后地址）；
  2. 分割与检测同帧同代 `(session_id,camera_id,generation,seq)`；
  3. AI 不可用/分割失败 ≠ 轨道内安全（unavailable+reason+空框 与 ready+空框 语义区分）；
  4. 掩码为原图坐标独立二值 plane 租约，`mask` 不进 core `DetectionResult`；
  5. `unknown` 不映射 `normal`；未知 label 白名单拦截；
  6. profile 能力需模型证据：`ai_enabled=true` 与 app.yaml 登记模型名不构成能力，`rknn_query` 核验通过才 ready；
  7. 日志/工件脱敏（无凭据、无含鉴权 URL）；
  8. 禁止把所有目标映射为 `track_id=0`；跟踪不可用时 `track_id=null` + 相机级适配。

### 1.2 范围排除

- 视频取流/解码/RGA/帧状态机测试：已由 `docs/video/VIDEO_TEST_DESIGN.md` 覆盖，本文消费其结论。
- 规则级别映射/多帧确认数学：hmi core tests（`app/tests/core/test_rules.cpp` 等）已覆盖，本文验入口拦截。
- 模型精度与漏报/误报数值、RKNN 转换正确性、训练/数据集固化：数据/训练/导出能力与执行轮负责；本文只挂"待执行"标记，不以脚本存在声称通过。
- NPU 算力容量定论（ITEM-009 只定义仪器与守卫生效性判据，测量值属执行轮）。
- 电气/MCU/MCU 上电不在软件测试职责。

---

## 2 测试层与环境

| 层 | 载体 | 测试对象 | 覆盖任务 | 说明 |
|---|---|---|---|---|
| ENV-AI-HOST | 开发机 + 夹具张量/帧构造器（无 RKNN 库） | 能力核验逻辑、预处理数学、检测/分割后处理数学、同帧判定、结果关联、语义白名单、跟踪关联、可用性语义、自检 | ITEM-001,003..008,010..013 | 张量源经注入接缝驱动后处理；全部为确定性数学断言 |
| ENV-AI-BOARD | 板端 + `librknnrt.so` + 真模型 | `rknn_query` 真模型探针：张量数量/形状/dtype/量化参数逐项归档、与后处理期望匹配 | ITEM-001,002,009 | 经登记 `rk3588_inspect` Paramiko 入口；历史 Zoo demo 产物只作系统能力参考，不冒充本 app 通过 |
| ENV-C | 板端 file/test 源 + 真模型进程 | 真实取流→派发→真模型推理→envelope→规则/P5 全链路、温度守卫、预算调度 | ITEM-002,006,007,009,012 | 板端资源夹具与视频域 ENV-C 共用 |
| ENV-D | 板端真实 RTSP | front 真实画面置信度分布、50 0m/距离梯度 case、真实链路 | ITEM-007,011,013 | 现场用例；需授权 + 用户人工视觉确认 |

分层规则：ENV-AI-HOST 为确定性数学结论；板端/真机档**重新测量、不外推 host 结果**；低层失败不外推高层，高层不通过也不否定低层数学结论。file/test 源的 pass 不等于真实源 pass；SimDetector 通过不等于真 Detector 通过。

### 2.1 测试基础设施需求（交实现/环境侧，不由测试设计直接实现）

1. **张量源注入接缝**：真实 Detector 的张量输入须可被合成张量描述（与 `rknn_query` 同结构的描述符）替换，用于 host 后处理单测。若实现侧绑定后不可注入，记为环境缺口如实归档，不替代为假通过。
2. **合成帧构造器**：已知内容的 BGR/NV12 帧（含 stride padding、已知图形位置），可预先计算 letterbox 参数与期望掩码/框。
3. **文件源夹具**：合成已知图像循环（person 中心在 rail 掩码内的帧、在其外的帧、纯背景帧），供 ENV-C 触发 TC-T-TWOPHASE 两条路径。
4. **脱敏审计方法**：复用视频域 TC-V-LOG-01 的日志 grep 口径。

---

## 3 故障注入目录（FI-AI）

| ID | 注入 | 实现 | 验证观察 |
|---|---|---|---|
| FI-AI-01 | 模型文件缺失 | profile 指向不存在路径 | unavailable + 稳定 reason（MODEL_MISSING 级 token）；P5 显示不可用原因；不冒充 ready |
| FI-AI-02 | 张量数量/形状不匹配 | 夹具返回错误输出数/shape | MODEL_MISMATCH；不进入推理循环；不按文件名判定 |
| FI-AI-03 | 量化参数异常 | 夹具 dtype/zp/scale 与后处理期望不符 | MODEL_MISMATCH；不静默用默认值遮蔽 |
| FI-AI-04 | 非法帧格式/stride | NV12 stride<width、奇数宽高、零 planes | 显式错误 + 计数；不静默丢帧；无越界写 |
| FI-AI-05 | 消费 unmap 后地址 | test double 先 unmap 再访问 | 显式错误/断言，不 segfault、不产出伪结果 |
| FI-AI-06 | 跨帧/跨代两阶段 | 同 seq 不同 generation 的 seg/det 结果组合 | 拒绝 + 计数；不产生安全结论 |
| FI-AI-07 | 分割失败/掩码空 | 掩码全零/掩码 lease 不可用 | unavailable + reason；不发"轨道内安全" |
| FI-AI-08 | 调度队列过载 | depth=2 注入快帧流 | 结果队列有界、丢最旧计数；submit/snapshot/stop 不被 infer 阻塞 |
| FI-AI-09 | 温度守卫 | 注入温度 > temp_limit_c=85 | 降载/暂停按调度策略；恢复后自动回升；实测温度来源 |
| FI-AI-10 | 陈旧/重复/乱序结果 | 注入旧 gen、重复 seq、乱序 seq | 各自丢弃且**独立计数** |
| FI-AI-11 | 非 person 类别 | COCO cls_id∈{2,7,...} 注入 | 白名单外丢弃 + 计数；不入 RuleEngine |
| FI-AI-12 | 未知 label | 语义层注入未知/未映射 label | 适配器拦截：零报警，拦截计数可见 |
| FI-AI-13 | track_id 冒用 | 全部目标映射 track_id=0 的实现注入 | 检出并判失败（多目标会累计误认一次） |

---

## 4 场景用例集

### G1 模型能力核验与探针（ITEM-001）— 主 env: AI-HOST / AI-BOARD

| TC | 描述 | 刺激 | 观测 | 判据 |
|---|---|---|---|---|
| TC-T-CAP-01 | 张量描述全项匹配置 ready | 合法夹具描述（yolov8s-seg 13 输出 + proto；yolov5s 3 尺度） | ready + 逐张量匹配明细归档 | 数量/形状/布局/量化逐项通过才 ready；model_version=运行时实际身份 |
| TC-T-CAP-02 | 缺失/不匹配拒绝 | FI-AI-01/02/03 | unavailable + stable reason token | reason 稳定、可被 P5 上报；无凭据/路径泄露 |
| TC-T-CAP-03 | 不按文件名判定 | 配置登记 yolov5s 但夹具返回 1 类分割布局 | MODEL_MISMATCH；无伪 boxes | 文件存在≠ready |
| TC-T-CAP-04 | 板端真模型 13 输出探针 | `rknn_query` 真 yolov8s-seg | 每张量 name/dims/dtype/zp/scale 归档 JSON | 与后处理 `dfl_len/output_per_branch=4/proto` 假设一致则 ready；否则排除 |
| TC-T-CAP-05 | 板端真 yolov5s 探针 | `rknn_query` 真 yolov5s | 三尺度布局归档 | 与检测后处理期望一致 |
| TC-T-CAP-06 | 每路 model_version/原因可读 | P5 探针/envelope | ready 时讀 model_version 实值；unavailable 时 reason 必有 | unavailable 时 model_version 可空但 reason 必在 |

### G2 租约预处理（ITEM-002）— 主 env: AI-HOST / ENV-C

| TC | 描述 | 刺激 | 观测 | 判据 |
|---|---|---|---|---|
| TC-T-PRE-01 | Letterbox 参数 | BGR 1280×720→640×640 | scale=0.5、pad_x=0、pad_y=80 记录正确 | 不拉伸变形；参数供后处理逆变换 |
| TC-T-PRE-02 | NV12 非连续 stride | y_stride=1280+pad | 输出尺寸 640×640 无 padding 垃圾 | planes[].stride_bytes 真实使用 |
| TC-T-PRE-03 | BGR→RGB 显式转换 | 纯红 BGR 帧 | 模型输入 R 通道均值 > B 通道 | 只在适配器内转换，不颠倒 |
| TC-T-PRE-04 | INT8 量化写入 | 已知 0–255 图 + 夹具 scale/zp | 值=clip(round(v/scale)+zp)；误差记录 | 与输入张量 dtype/量化一致；mean=0/std=255 口径 |
| TC-T-PRE-05 | 非法帧拒绝 | FI-AI-04 | 显式错误 + 计数；无越界写 | 错误可观测不静默 |
| TC-T-PRE-06 | 禁 FrameRef 推像素 | 仅元数据无 lease 请求 infer | 显式拒绝；无伪结果 | §0.3-1 |
| TC-T-PRE-07 | 禁 unmap 后访问 | FI-AI-05 | 显式错误，不 segfault | §0.3-1 |

### G3 检测后处理（yolov5s person，ITEM-003）— 主 env: AI-HOST

| TC | 描述 | 刺激 | 观测 | 判据 |
|---|---|---|---|---|
| TC-T-DET-01 | 三尺度解码 | 夹具头张量含手工可解码 1 person 框 | 解码坐标/置信度与手工期望一致（归一化容差 1e-4） | box/score/anchor 解码正确 |
| TC-T-DET-02 | 置信度过滤+NMS | 重叠 IoU>阈值双框 + 阈值边界框 | NMS 保留高分框；边界行为与配置联动 | 阈值/NMS 来自配置，不写死 0.25/0.45 |
| TC-T-DET-03 | letterbox 逆变换 | 已知模型域坐标 + pad 参数 | 原图归一化坐标正确；x+w≤1、y+h≤1 | 越界裁剪 |
| TC-T-DET-04 | person 白名单 | cls_id∈{0,2,7,..} 注入 | 仅 cls_id==0 保留；丢弃计数=注入数-1 | 非 person 不入结果 |
| TC-T-DET-05 | 空图空框 | 全零/噪声夹具 | ready + boxes=[] | 空图≠unavailable |

### G4 分割后处理（yolov8s-seg，ITEM-004）— 主 env: AI-HOST / AI-BOARD

| TC | 描述 | 刺激 | 观测 | 判据 |
|---|---|---|---|---|
| TC-T-SEG-01 | 13 输出结构解析 | 3 分支×{box,score,score_sum,seg 系数}+proto 夹具 | 分支/形状可解析 | `dfl_len=dims[1]/4`、`output_per_branch=4`；数量不符→MODEL_MISMATCH |
| TC-T-SEG-02 | 系数·proto 生成掩码 | 已知系数向量 + proto | 点积→阈值→二值掩码与手工期望逐像素一致 | 阈值可配置；不把软掩码当布尔 |
| TC-T-SEG-03 | 逆 letterbox 掩码 | 模型域掩码 + pad | 原图尺寸掩码与框坐标一致 | 掩码坐标=原图坐标 |
| TC-T-SEG-04 | plane 二值与所有权 | 人为释放/析构观察 | 0/非零；独立生命周期；stride 规则同帧 | 不复用帧裸地址；不入 core DetectionResult |
| TC-T-SEG-05 | 框/分数/掩码一一对应 | 多 rail 实例夹具 | box[i]↔mask[i]↔score[i] 索引对齐 | 无错位 |
| TC-T-SEG-06 | 张量数不符 | FI-AI-02 | MODEL_MISMATCH；无伪掩码 | §0.3-6 |

### G5 同帧两阶段判定（ITEM-005）— 主 env: AI-HOST / ENV-C

| TC | 描述 | 刺激 | 观测 | 判据 |
|---|---|---|---|---|
| TC-T-TWOPHASE-01 | 同帧命中 | 同身份 rail mask + person 中心在掩码内 | in_roi=true、label=person | 一致坐标确定性命中 |
| TC-T-TWOPHASE-02 | 中心不在掩码 | 中心 mask=0 | in_roi=false，不产生轨道内事件 | 丢弃可计数 |
| TC-T-TWOPHASE-03 | 跨帧/跨代拒绝 | FI-AI-06 | 拒绝 + 计数 | 同帧同代 |
| TC-T-TWOPHASE-04 | 分割失败不安全 | FI-AI-07 | unavailable + reason；无安全输出 | 空掩码≠"轨道内安全" |
| TC-T-TWOPHASE-05 | danger_zone ROI 交 | 配置 danger_zone + roi.yaml | 按 roi.yaml 求交后再判定升级 | roi.yaml 唯一 ROI 来源 |
| TC-T-TWOPHASE-06 | 跨帧缓存默认关 | 无缓存配置下注入旧帧结果 | 不作实时报警依据；若实现缓存则必须登记最大年龄/失配策略 | 默认关闭 |

### G6 结果关联与发布（ITEM-006）— 主 env: AI-HOST / ENV-C

| TC | 描述 | 刺激 | 观测 | 判据 |
|---|---|---|---|---|
| TC-T-PUB-01 | 身份链完整 | 一帧全链路 | envelope 含 (session,cam,gen,seq)/capture_mono_us/result_mono_us/latency_ms/model_version | latency=完成−接收，含排队 |
| TC-T-PUB-02 | 陈旧/重复/乱序 | FI-AI-10 | 三类各自独立计数；不向语义层下发 | 不可用语义不受影响 |
| TC-T-PUB-03 | 不可用与空框区分 | unavailable 对比 ready+空框 | 两种输出在 envelope/P5/事件上可区分 | §0.3-3 |
| TC-T-PUB-04 | 有界队列丢旧 | FI-AI-08 | 队长≤depth；丢最旧计数；infer 不阻塞 submit/snapshot/stop | 调度不阻塞 |
| TC-T-PUB-05 | drop 计数单调 | 长跑 | ai_dropped 单调不减 | 与视频域 FI-V-05 口径一致 |

### G7 语义白名单接入（ITEM-007）— 主 env: AI-HOST / ENV-C

| TC | 描述 | 刺激 | 观测 | 判据 |
|---|---|---|---|---|
| TC-T-SEM-01 | front person 语义 | 命中掩码 person → RuleEngine | `PersonOnRail`(L1)/`FrontObstacle` 按现有映射 | 只进受支持语义；级别不改 |
| TC-T-SEM-02 | 未知 label 拦截 | FI-AI-12 | 零报警 + 拦截计数 | 现有规则"非 person 归障碍"兜底被前置白名单挡住 |
| TC-T-SEM-03 | 多目标聚合排序 | 同帧 3 目标命中 | 先聚合再按稳定 track 排序，一次处理；不重复开事件 | InterfaceContract §6 |
| TC-T-SEM-04 | generation 变更清理 | 失帧/新 gen | 受控接口清理该路确认+跟踪；不整路误清无关目标 | 需实现侧受控接口；现状无公开 reset 为依赖 |
| TC-T-SEM-05 | none 路不派发 | CAM-02/03 帧 | 不入推理队列；无检测类报警 | ai_profile=none 永久 |

### G8 跟踪协同（ITEM-008）— 主 env: AI-HOST

| TC | 描述 | 刺激 | 观测 | 判据 |
|---|---|---|---|---|
| TC-T-TRK-01 | 同目标 ID 稳定 | 连续帧同位置目标 | 同 track_id 命中只更新同一事件 | 无 track_id=0 冒充（FI-AI-13 交叉） |
| TC-T-TRK-02 | 跟踪不可用路径 | 禁用/失败 | track_id=null + 相机级事件适配生效 | 相机级适配不冒充 track 语义 |
| TC-T-TRK-03 | 目标互不干扰 | 两目标交替命中 | 确认/抑制各自独立；互不清除 | 禁整路 reset 误清 |
| TC-T-TRK-04 | 重连后代际清理 | 重连→新 generation | 旧目标确认/跟踪状态被清理；跨代不复用 | 清理走受控接口 |

### G9 真实检测器接入与调度（ITEM-009）— 主 env: AI-HOST / ENV-C

| TC | 描述 | 刺激 | 观测 | 判据 |
|---|---|---|---|---|
| TC-T-DISP-01 | 链路贯通 | file/test 源→AiWorker→真 Detector→发布 | 帧→派发→推理→envelope→规则/P5 全 trace | 生产模式关闭自造帧 tick（SimDetector 退役） |
| TC-T-DISP-02 | 停止后不可用 | stop | AI unavailable + reason；视频显示不受影响 | AI 不可用不影响显示 |
| TC-T-DISP-03 | 非阻塞推理 | 慢 infer stub | submit/snapshot/stop 均及时返回 | infer 不持调度锁 |
| TC-T-DISP-04 | 调度优先级 | 4 路启用（front>wheel_rail>pantograph>cab_door） | `plan_inference` 优先级/预算生效 | 无饥饿；间隔来自配置 |
| TC-T-DISP-05 | npu_budget 语义 | 预算边界夹具 | budget=路数预算；不当作 NPU 核数 | 文档口径一致 |
| TC-T-DISP-06 | 温度守卫实测 | 板端实测温度 vs 注入值 | 守卫判定使用真实实测温度；>85°C 触发降载 | 模拟 FPS/温度不冒充 |

### G10 其它职责可用性语义（ITEM-010）— 主 env: AI-HOST

| TC | 描述 | 刺激 | 观测 | 判据 |
|---|---|---|---|---|
| TC-T-MIS-01 | 无专用模型 unavailable | wheel_rail/pantograph/cab_door 无模型证据 | unavailable + reason（UNSUPPORTED/MODEL_MISMATCH），语义 unknown | 配置登记模型名不构成能力 |
| TC-T-MIS-02 | unknown≠normal | 同上 | 输出未知语义，永不产生 normal 引言 | §0.3-5 |
| TC-T-MIS-03 | 提供核验模型后 ready | 合规夹具张量 + 声明语义映射 | 按声明能力转可用 | 核验通过才 ready |
| TC-T-MIS-04 | none 路永不推断 | CAM-02/03 | 不入队列；无检测类报警 | §1 判定表 |

### G11 可选测距（ITEM-011）— 主 env: AI-HOST

| TC | 描述 | 刺激 | 观测 | 判据 |
|---|---|---|---|---|
| TC-T-DIST-01 | 未标定 → -1 | 无标定配置 | distance_m=-1；P5 显示"未知"非 0 | 不用框高臆造米值 |
| TC-T-DIST-02 | 标定换算 | 注入 px/m 标定 + 已知框高 | distance_m 与换算公式一致 | 标定含变焦档位时才生效 |
| TC-T-DIST-03 | 规则不消费距离 | 触发 PersonOnRail | 事件判定与 distance_m 无关 | `danger_proximity_m` 未消费的现状原样记录 |

### G12 AI 调试视图数据（ITEM-012）— 主 env: AI-HOST / ENV-C

| TC | 描述 | 刺激 | 观测 | 判据 |
|---|---|---|---|---|
| TC-T-DBG-01 | P5 字段齐备 | 跑链路 | 每路 model_version/available/unavailable_reason/fps/latencyMs/最近结果/confidence/distance/boxes/ROI | 字段存在且来源真实推理 |
| TC-T-DBG-02 | 掩码叠加条件 | 掩码可用/不可用两态 | 叠加只在真实掩码可用时提供；四开关默认关闭；司机角色不显示 | 叠加层依赖真实掩码 |
| TC-T-DBG-03 | 未知≠0 | unavailable 路 | 未知数值显示占位（非 0） | unknown formatting |

### G13 无相机自检与夹具证据（ITEM-013）— 主 env: AI-HOST / ENV-C

| TC | 描述 | 刺激 | 观测 | 判据 |
|---|---|---|---|---|
| TC-T-SELF-01 | --selftest 全路径 | 无设备运行 --selftest | FI-AI-01/02/04/07/10/12 各路径覆盖 + 结构化 JSON 输出 | JSON 归档 `docs/vision/test_evidence/SELFTEST/` |
| TC-T-SELF-02 | 文件源夹具判定复现 | 已知合成图→期望框/掩码/判定 | host/板端各自复现（容差记录） | host 结论不外推板端 |
| TC-T-SELF-03 | 待执行项标注 | 自检/验证报告 | 明确"真机精度与人工视觉确认待执行" | 不由本增量声称通过 |

---

## 5 冒烟集（P0）

| TC | 描述 | env |
|---|---|---|
| TC-T-SMOKE-01 | core/media 单测全绿（含新推理模块测试） | AI-HOST |
| TC-T-SMOKE-02 | 真模型探针归档（yolov8s-seg 13 输出 + yolov5s 布局） | AI-BOARD |
| TC-T-SMOKE-03 | --selftest 全路径绿（无设备） | AI-HOST |
| TC-T-SMOKE-04 | file 源→推理→发布 30s 无崩溃、计数稳定 | ENV-C |
| TC-T-SMOKE-05 | 脱敏审计零命中 | all |

---

## 6 需求覆盖矩阵

| VISION 任务 | 覆盖用例 |
|---|---|
| ITEM-001 模型能力核验 | TC-T-CAP-01..06 |
| ITEM-002 租约预处理 | TC-T-PRE-01..07 |
| ITEM-003 检测后处理 | TC-T-DET-01..05 |
| ITEM-004 分割后处理 | TC-T-SEG-01..06 |
| ITEM-005 同帧两阶段 | TC-T-TWOPHASE-01..06 |
| ITEM-006 结果关联发布 | TC-T-PUB-01..05 |
| ITEM-007 语义白名单 | TC-T-SEM-01..05 |
| ITEM-008 跟踪协同 | TC-T-TRK-01..04 |
| ITEM-009 检测器与调度 | TC-T-DISP-01..06 |
| ITEM-010 其它职责语义 | TC-T-MIS-01..04 |
| ITEM-011 可选测距 | TC-T-DIST-01..03 |
| ITEM-012 AI 调试视图 | TC-T-DBG-01..03 |
| ITEM-013 自检与夹具 | TC-T-SELF-01..03 |

隔离声明：`unavailable` 判定不替代 `ready+空框`；跨代拒绝计数独立于 `ai_dropped`；host 夹具 pass 与板端真模型 pass 分档归档。

---

## 7 时间与证据管理

| 项 | 说明 |
|---|---|
| 证据目录 | `docs/vision/test_evidence/<TC-ID>/` — 原始命令+退出码+输出；每 TC 每 env 单独文件 |
| 命名 | `<TC-ID>-<env>-<date>-r<k>.{md,json,raw.txt}`；验证轮独立复跑加 `verify-` 前缀 |
| 附加产物 | rknn_query 张量描述 JSON、夹具张量/帧 JSON、--selftest JSON、letterbox 参数记录、掩码 CSV/图像、trace 输出 |
| 失败记录 | pass/fail/not_run + blocked 原因 + 复现命令；区分"无模型证据（unavailable）"与"有模型未检出（FN）" |
| 不混淆 | host 夹具 pass ≠ 板端真模型 pass；真模型探针 pass ≠ 精度验收；SimDetector pass ≠ 真 Detector pass；unavailable ≠ 无目标；文件源 pass ≠ 真实源 pass |

---

## 8 移交/边界

1. 依赖实现侧（发现缺口如实记录、交回实现，不替代假通过）：张量源注入接缝、受控确认/跟踪清理接口、真实温度接入调度与守卫。
2. 上游依赖：数据/训练/导出能力交付冻结现场测试集之前，精度类用例保持"待执行"挂起标记，不是通过。
3. 现场实测（ENV-D）需按环境授权执行并使用户完成人工视觉确认；自动截图/数值不冒充人工核验。
4. 环境事实：`rk3588_inspect=configured_unverified`；`rk3588_build/deploy/verify` 对测试能力 `unavailable_for_current_capability`。本设计不执行板端操作；执行轮按当前授权选择入口。
5. 精度验收目标值（单帧检出率 ≥70%/≥95%、置信度中位数 ≥0.4/≥0.5、空场景误检 ≤5%/≤1次/天/路、时序平滑 ≥90%/≥99% 等）来自 `feasibility_500m_person_detection.md` 历史方法，**需求方尚未确认为正式验收**；分割侧 Mask 指标无消费需求值。本文只固化测量方法与仪器，不代为冻结目标。

---

## 9 版本

- v1: 初版，对齐 VISION_DESIGN v1 + vision_tasks ITEM-001..013 + InterfaceContract v1.0.0；测试环境层 AI-HOST/AI-BOARD/ENV-C/ENV-D；故障注入 FI-AI-01..13；G1..G13 用例集；P0 冒烟 5 条；覆盖矩阵逐 ITEM 落点。
