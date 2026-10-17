# 视觉/推理应用端到端测试计划（E2EPlan）

- Artifact: E2EPlan
- Capability: `software_tester.software_test_design`
- 版本: v1（2026-10-18）
- 伴生: `docs/vision/VISION_TEST_DESIGN.md`（用例细节在伴生文件，本文只定义完整旅程与环境升降级门）
- 上游: `docs/vision/VISION_DESIGN.md` v1、`docs/interfaces/INTERFACE_CONTRACT.md` v1.0.0、`docs/video/VIDEO_E2E_PLAN.md` v1（同构旅程，AI 域细分）

---

## 1 环境分层（对齐 VISION_TEST_DESIGN §2）

ENV-AI-HOST（夹具张量/帧单测）→ ENV-AI-BOARD（板端真模型 `rknn_query` 探针）→ ENV-C（板端 file/test 源 + 真模型链路）→ ENV-D（板端真实 RTSP front）。
每条旅程注明最低环境档，前档全绿才进后档；不跨档外推。板端操作经登记 `rk3588_inspect` Paramiko 入口，有限等待，操作前备份既有模型/产物，测试输入放 /tmp 或独立工作目录。

---

## 2 E2E 旅程

### E2-T-01 夹具后处理与判定闭环（__selftest）— ENV-AI-HOST
准备: 构建主机单测含推理模块。刺激: `--selftest` 运行全部 FI-AI 注入路径。观测: 合成图→捏造张量→后处理→同帧判定→结构化 JSON；非法帧/陈旧结果/未知 label 路径分别返回确定结论。判据: 全部路径可复现、JSON 可归档、判定与手工期望一致（容差 1e-4 归一化）。清理: 无板端操作。证据: `docs/vision/test_evidence/E2-T-01/`。

### E2-T-02 板端真模型探针与匹配（probe）— ENV-AI-BOARD
准备: 板端部署后真模型路径与 `librknnrt.so` 可见；先备份现有 install 目录模型。刺激: 运行真模型 `rknn_query` 探针（yolov8s-seg 与 yolov5s）。观测: 逐张量 name/dims/dtype/zp/scale 归档；与后处理期望逐项对比结论 ready/MODEL_MISMATCH。判据: 匹配结论只由真 query 数据驱动；模型资产 sha256 归档可关联。清理: 不改 `install/` 现有模型（备份机制限内）。证据: `docs/vision/test_evidence/E2-T-02/`。

### E2-T-03 真实取流→推理→发布链路（file/test 源）— ENV-C
准备: 板端实现部署（验收部署模型后一并核验）；file/test 源配好夹具图像循环（含掩码内person、掩码外 person、空场景）。刺激: 启动应用 → 观察 P5 数据 + envelope 流。观测: 30s 内 envelope 链路 trace 完整；帧→派发→推理→规则/P5 连通；P5 每路 model_version/available/reason/fps/latency 真实；温度守卫/预算调度的实测温度通路接入；链路 RSS 稳态。判据: 计数器（stale/dup/order/ai_dropped/调度计数）单调不减、无 resize 异常；掩码叠加在真实掩码时显示、未知值不格式化为 0。清理: 应用退出；feeder 停止；日志归档。证据: `docs/vision/test_evidence/E2-T-03/`。

### E2-T-04 front 真实画面确认（ENV-D，现场）
准备: front 相机授权 + 500m/距离梯度现场场景（荒地/人在轨道/人不在轨道/空场景，多时段）。刺激: 应用接入真实 RTSP front。观测: 每场景置信度分布、检出样本、AI 不可用路径显示、（可选）距离值。判据: **数值不冒充人工视觉确认** — 用户最终核验框/掩码合理性；测试轮只提供分布与样本作辅助。.clip 精度目标值待需求方确认（VISION_TEST_DESIGN §8.5）。证据: `docs/vision/test_evidence/E2-T-04/`。

### E2-T-05 AI 不可用语义审核（全档）
准备: 运行任一档旅程（host/板端均可）。刺激: 一次模型缺失 + 一次分割失败 + 一次 stop。观测: 每次事件 UI/P5/日志中"不可用+原因"明确显示，与"ready+空框（无目标）"区分；不产生"轨道内安全"语义。判据: 零违规 — 现有规则既有模式下无 status=normal 误映射；脱敏审计零命中。证据: `docs/vision/test_evidence/E2-T-05/`。

---

## 3 升降级准则

- 升级: ENV-AI-HOST 全绿才进 ENV-AI-BOARD；ENV-AI-BOARD 探针 ready 才进 ENV-C；ENV-C 旅程全绿 + 部署可用才进 ENV-D。
- 降级/挂起: 模型精度类现场用例（E2-T-04 的达标判定）在需求方冻结验收值前保持"待执行"；挂起≠通过。板端部署缺口保持如实记录，不标 fail 不标 pass。
- 证据: 按 `VISION_TEST_DESIGN.md` §7 命名归档；host 夹具/板端真模型/真机三源证据分文件不合并。

---

## 4 版本

- v1: 初版，对齐 VISION_TEST_DESIGN v1，五条 E2E 旅程（host 自检闭环、板端真模型探针、file 源全链路、front 真机确认、不可用语义审核）。
