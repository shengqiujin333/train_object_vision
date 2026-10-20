# UI 实现评审证据 — N019 桌面应用完整交付（overall complete_delivery）

- Capability: `web_frontend.ui_implementation_review`（节点 `n019_web_frontend_ui_implementation_review`）
- 被评对象: `runtime.artifacts.delivered.n005_embedded_linux_engineer_desktop_app_implementation.implementation`
  （DesktopAppImplementation，声明工件 `app/README.md`），含 ITEM-001..025 与发布阻塞修复轮 F-01/F-02
- 版本锚定: 当前工作树 HEAD `e868b24`（工作树干净）；`packaging/VERSION` = `0.1.0`（`rk3588-aarch64`）
  - UI 层相对上次整体 PASS（`a92d008`）：`git diff` 仅 `P3Replay.qml` +23（已由 REPAIR-n008-v2 覆盖）；
    其后视频线/视觉线新增约 3.2 万行落在 `app/src/core`、`app/src/media`（桌面应用链接的同一层）
- 参照设计: `docs/hmi/INTERACTION_DESIGN.md` v2、`docs/hmi/VISUAL_DESIGN.md` v2、`docs/hmi/DESKTOP_APP_DESIGN.md`、`docs/hmi/design_tokens.yaml`、`docs/hmi/PERMISSION_MATRIX.md`
- 本能力历史: 26 份分项评审 `docs/hmi/ui_review/REVIEW-ITEM-001..025.md` + `REVIEW-REPAIR-n008.md`(FAIL) / `-v2.md`(PASS)
- 关联上游验证: `n018_software_tester_software_verification` **TEST_PASS**（`VERIFY-N018-OVERALL/VERIFY-N018-OVERALL-v1.md`，1452/1452 + 六页导航）——**独立结论，不免除本能力自身观察**
- 评审日期/环境: 2026-10-20；Windows 主机（无 Qt）+ RK3588 `192.168.0.103`（登记 Paramiko SSH，指纹核验 `SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU`）
- 判定: **TEST_PASS**（完整交付 UI 侧：**当前版本实际构建/装载/六页导航观察通过**；分项判据已在 26 份评审逐项通过；边界与遗留观察见 §6/§7）

---

## 0. 覆盖范围（不以便利样本代替整体）

| 覆盖对象 | 证据 |
|---|---|
| 当前版本装配层（Qt `Release` 构建 + QML 装载） | 本轮实际构建 `build_exit=0` + 装载 `WIN_UP=1` |
| 六页界面（P1..P6）实际渲染 | 本轮六张 1920×1080 截图（非空白、互不相同） |
| 运行期 QML 错误 | 本轮 app.log 4 行 INFO，错误关键字 0 命中 |
| 分项交互/字段/权限/状态/恢复（26 项） | `REVIEW-ITEM-001..025.md` + `REVIEW-REPAIR-n008*.md`（逐项） |
| 跨组件接线（状态条/报警卡→回放/权限/浮层） | 本轮源码核对（§5） |
| 媒体/视频/视觉 v2 集成到 UI | **不在本能力交付范围**（其他 producer + 后续集成，见 §7） |

---

## 1. 方法

1. **实际运行观察（本轮关键）**：在目标板本机构建 Qt `Release`，在**虚拟显示 Xvfb**
   （`QT_QUICK_BACKEND=software`，不占用操作员 X 会话）启动，`xdotool` 按 NavBar 真实槽位
   （`top_bar_height=48` + `nav_width=88`，槽心 y=92+88i，x=44）逐页导航 P1..P6，`ffmpeg x11grab`
   截屏 1920×1080，读取应用运行日志并按关键字检查 QML 错误。
2. **分项源码/证据评审**：ITEM-001..025 与 REPAIR-n008 v1/v2（本仓库 `docs/hmi/ui_review/`）。
3. **跨组件接线核对**：`main.qml` 的 `statusNavigate` / `replayRequested` 连接、角色导航过滤、
   `DeniedView`、权限门控、恢复浮层。
4. 未修改任何产品源码/界面/判据；仅使用评审工具（`docs/hmi/ui_review/tools/board_ui_capture.py`）
   与 `/tmp` 临时观察配置（`simulation=true`、`default_role=admin`；仓库 `app/config` 未改）。

---

## 2. 本轮实际运行证据

| 项 | 值 |
|---|---|
| Qt `Release` 构建 | `build_exit=0`（345 文件上传；79s） |
| 产物 | `build-target/src/ui/railhmi`，ELF aarch64，940520 B，sha256 `7294645a235a7517717d57458a22af6e16955e8a11479c8b8d5cacbda133578c` |
| 装载 | `WIN_UP=1`；`configuration OK: 6 cameras` / `render path: cpu` |
| 应用日志 | **4 行 INFO**；`TypeError`/`is not a type`/`Cannot open`/`chipName`/`doesNotExist`/`QML ` **各 0 命中** |
| 六页截图 | 1920×1080，字节 52273/91723/86170/54598/96072/154074（两两不同、非空白） |

证据（`docs/hmi/test_evidence/VERIFY-N019-UI/`，stamp `20261020-uiobs-n019`）：
`20261020-uiobs-n019-{p1_realtime,p2_alarm_center,p3_replay,p4_all_cameras,p5_ai_debug,p6_maintenance}.png`、
`20261020-uiobs-n019-app.log`、`20261020-uiobs-n019-xvfb.log`、`ui-capture-20261020-uiobs-n019.{log,transcript.json}`。

运行后板端清理核验：无残留 `Xvfb`/`railhmi` 进程，`/tmp/railhmi_uireview` 已删除。

---

## 3. 六页界面覆盖（本轮实际渲染）

| 页面 | 实际观察 | 分项判据来源 |
|---|---|---|
| P1 实时监控 | 页面渲染（三联/状态区/报警条骨架），无 QML 错误 | REVIEW-ITEM-011/014/016 |
| P2 报警中心 | 渲染（列表/筛选/行内录像三态），无 QML 错误 | REVIEW-ITEM-018 |
| P3 历史回放 | 渲染（时间轴/关键帧占位/三联标题/同步偏差提示/详情），无 QML 错误 | REVIEW-ITEM-020 v3 + REPAIR-n008-v2 |
| P4 全摄像头 | 渲染（六格网格） | REVIEW-ITEM-012/015 |
| P5 AI 调试 | 渲染（叠加开关/指标面板） | REVIEW-ITEM-017/021 |
| P6 设备维护 | 渲染（可滚动分区卡/云台/日志/权限矩阵/恢复/故障自检） | REVIEW-ITEM-022/023/024/025 |

---

## 4. 分项判据整体状态（26 项，逐项证据在各自评审）

- **全部通过**：ITEM-001..025（安全/布局/配置/状态/规则/持久化/单测/模拟/令牌/外壳/注册表/取流/
  渲染/网格/状态区/推理/报警中心/片段/回放/权限/健康恢复/打包自检）——详见各
  `REVIEW-ITEM-0NN.md` §7。
- **修复后通过**：ITEM-020（D-01..D-09 → 修复轮），REPAIR-n008（F-01 `chipName` / F-02 缩略图基址
  → 修复轮 2，本轮再次实际观察确认缺席）。
- 未发现新的**界面运行期**错误（本轮 app.log 0 命中）。

---

## 5. 跨组件交互（源码核对 + 运行支撑）

| 交互 | 接线 | 状态 |
|---|---|---|
| 关键状态项/顶部条 → 页面（含无权限显示） | `main.qml:93-94` `statusNavigate.connect(onStatusNavigate)` | 在位 |
| P2 报警行「查看回放」→ P3 定位并锁摄像头 | `main.qml:95-96` `replayRequested.connect(onReplayRequest)` → `selectAlarm` | 在位 |
| 角色化导航（受限入口不渲染） | C++ `railhmiNav` 过滤 + `DeniedView` | 在位 |
| 导出/配置二次确认 + 维护日志 | `railhmiPermissions` 门控 + 模态 | 在位（ITEM-023） |
| 启动恢复浮层/健康与日志刷新 | `RecoveryOverlay` + `Connections{onChanged}` | 在位（ITEM-024 v2） |
| 六页实际导航 | 本轮 xdotool NavBar 逐页点击 | 运行确认 |

**运行交互覆盖说明**：本轮运行时交互为**逐页导航**（真实点击）；页面内部控件点击未逐件脚本化，
其行为由分项评审（源码 + 板端模型/探针）与上述接线核对支撑，见 §6/§7 边界。

---

## 6. 遗留观察项（非阻断；已在分项评审登记，交后续集成/运行期）

1. **媒体/视频未接入 QML 运行时**：视频渲染面仍为占位（`VideoSurface` 空层、P1/P3「媒体层提供」、
   P4/P5 占位/「未启动」）；`MultiStreamManager` 未桥接到 QML（REVIEW-ITEM-013/014/015）。
   属视频线/集成职责（§7）。
2. **视觉/推理未接入 QML**：`main.cpp` 未见 vision/video 注入；P5 仍显示样例/占位，AI 状态条与
   `ai_alarm_annotation` 未消费（REVIEW-ITEM-017/021）。属视觉线/集成职责（§7）。
3. **P2「查看快照」仍为未接信号**（`snapshotRequested` 未连接，REVIEW-ITEM-018 D-01）。
4. **`DeniedView.onBack` 未复位 `currentPageId`**（REVIEW-ITEM-011 D-01/D-02 残留；页面自身有守卫）。
5. **连接状态三重编码/`QualityFlags`** 在 CameraTile/视图未完整呈现（跨切面，REVIEW-ITEM-012/013/016）。
6. **P3 播放/倍速仅状态**（媒体推进属视频线）；**P3 导出无即时 toast**；**P6「码流」口径**、
   重连计数双源、AI 卡范围（REVIEW-ITEM-020/022/023）。
7. **O-1 banner `simulation=off`** 为编译期宏、与 YAML `app.simulation` 同名不同源（仅日志语义）。

以上均已有分项记录与期望，不改变本次整体 UI 判定。

---

## 7. 边界与交接

- **不在本能力交付范围**（其他 producer，各自验证；后续集成节点接线）：视频线（n010/n012）、
  视觉线（n014/n016）的真实取流/推理结果进入 QML；真实 RKNN 画面、真实缩略图。
- **未执行**：真实 6 路 RTSP 下端到端页面实测；`systemctl enable`/断电重启自启（ENV-E）；
  操作员真实 X 会话人工视觉确认（本轮 Xvfb 虚拟显示，视觉观感/眩光等属用户核验）。
- **评审工具/环境**：`xvfb` 作为截图依赖（`environment_repair` test scope，此前轮已安装）；
  临时配置副本仅用于观察，未改仓库配置；板端进程已清理。

---

## 8. 判定

**TEST_PASS**（范围：桌面应用**完整交付的 UI 侧**——当前版本装配/装载/六页实际渲染与分项判据）。
- 依据：① 当前工作树版本 Qt `Release` 构建成功、QML 全装载、六页实际渲染且运行日志**无 QML
  错误**（应用日志 4 行 INFO，错误关键字 0 命中）——证明共享 core/media 大幅变更后组装层仍可用；
  ② 26 份分项评审（ITEM-001..025 + REPAIR-n008 v1/v2）逐项通过，ITEM-020 与 F-01/F-02 已修复并
  本轮复验缺席；③ 跨组件关键接线（状态导航、报警→回放、角色过滤、权限门控、恢复浮层）在位。
- **边界**：本 PASS 限于桌面应用自身 UI 交付；**媒体/视频/视觉进入界面的端到端**、真实设备取流、
  真机自启与操作员主观视觉确认属其他 producer/现场（§7），不由本结论背书。§6 遗留观察项按分项
  记录交后续集成与运行期复评，不改变本次判定。

### 复现

```sh
# 目标构建 + 虚拟显示装载 + 六页导航截屏 + 日志错误关键字检查（需 TRAIN_RK3588_SSH_CREDENTIAL）
python docs/hmi/ui_review/tools/board_ui_capture.py --host 192.168.0.103 --user admin \
  --expected-fingerprint SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU \
  --out-dir docs/hmi/test_evidence/VERIFY-N019-UI --stamp <tag>
```
