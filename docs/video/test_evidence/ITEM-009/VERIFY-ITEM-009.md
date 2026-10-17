# VERIFY-ITEM-009 — 显示分发与纹理交接 独立验证（TestEvidence）

- Artifact: TestEvidence（software_tester.software_verification）
- Capability: `n013_software_tester.software_verification`（CALL-cfd20abe-…-ITEM-009）
- 被测交付: `docs/video/implementation/ITEM-009_DISPLAY_HANDOFF.md`（commit `8f469f6`，stamp `20261017-item009-r2`）
- 验证轮: `VERIFY-20261017-v1`，2026-10-17
- 目标: compute_host `192.168.0.103`（aarch64 / GStreamer 1.20.3 / librga 1.10.1）
- host key fingerprint: `SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU`

## 结果: TEST_PASS

判定范围 = ITEM-009 当前版本的声明判据面：不可变拥有像素显示快照（cpu BGR888 深拷贝）、
presentable 语义、每相机单一来源、页面切换不重建会话（generation 恒定）、
gl 路径依赖缺失时显式不可用（不静默降级）。不含：生产 UI 纹理上屏与页面渲染
（桌面 UI 职责）、真实相机六路画面主观验收（用户确认）、gl 零拷贝实现（待 qmlglsink）。

## 1. 验证手段与结果

### 1.1 交付入口独立复跑（板端）

`app/tools/board_display_check.py`（--keep-remote）独立复跑 stamp
`20261017-item009-VERIFY-v1`：8 步全 exit=0（mkdir/upload 212 文件/build-media/
selftest/run-display/双 download；树保留给后续步骤）。见
`display-192_168_0_103-20261017-item009-VERIFY-v1.transcript.json`。

### 1.2 独立解析比对（不采信 acceptance 行）

`compare_item009.py`：60/60。impl r2（交付方原始证据）与 verify v1（验证方 fresh 复跑）
对 `core-evidence` 日志独立正则解析后判据逐行对齐：

| 判据 | 结果 |
|---|---|
| cpu available=true / gl available=false reason=missing:qmlglsink（两轮 identical） | ✅ V-03/V-04 |
| multi source=test channels=6 started=6 | ✅ V-05 |
| 三阶段 snapshot_valid/presentable/format=BGR888/1280x720/generation=1 | ✅ V-06..V-10 |
| 阶段序 P1(3 路)→P4(6 路)→P5(1 路) | ✅ V-11/V-12 |
| updates 逐阶段单调递增（同源持续出帧，未重建） | ✅ V-13 |
| summary 四标志独立解析全 true | ✅ V-14 |
| 非 phase 判据行两轮逐行 identical | ✅ V-15 |
| JSON acceptance/page_switch_no_rebuild 恒 true | ✅ V-16/V-17 |

### 1.3 板端 CLI 负例与复现（board_ext 13/13）

- `--selftest` 复跑 PASS×2（cpu_path_available、gl_status_explicit）。
- `--count 0` 走完流程 exit∈{0,3}（合法生产面观察口径）。
- 未知参数 → exit 2；`--help` → exit 0。
- 同参二次运行（B5）：channels=6、page_switch_no_rebuild/single_source=true、
  gl 显式 unavailable reason=missing:qmlglsink 全部复现。

### 1.4 自有 C++ 夹具（板端 g++ 直编，链接交付构建树对象）

- `display_ext.cpp` 11/11：probe_render_path cpu 恒可用 / gl 显式配对
  （available⇔reason 空缺一致 + 不可用时 reason=missing: 前缀）；无效租约 update
  不生成快照不计数不上屏；gl 分发器初始 presentable=false；registry 未知相机
  no-snapshot/not-presentable/updates=0。
- `display_ext2.cpp` 9/9：`nv12_to_bgr_raw` 纯函数语义（640×480 合法 NV12 →
  w*h*3 BGR、灰源三通道近中性 spread<8、纯蓝源 B 通道主导（绿帧回归判据）、
  uv_offset==0 / stride<width / 奇数宽高三类显式拒绝）。
- `display_ext3.cpp` 7/7：RGA 失败→恢复语义（ undersized 必失败 err 非空输出清空；
  失败后的随后成功转换不被污染、err 清空、数值正确）。
- 夹具自省校正两处（验证侧构造错，非产品缺陷）：RGA2 YUV 最小尺寸约束使 64×64
  正常态必失败（ITEM-005 既有 640×480 成功证据对照后改尺度）；C++ 夹具锁作用域
  判定（camera_worker.cpp R-B4）需括号深度分析而非首版 regex 锚点。

### 1.5 源码级断言（ext_items009.py 16/16）

`display_registry.cpp` 每相机恰好一处 `new DisplayDistributor` 并接到
`CameraWorker::set_frame_callback`；回调在 worker 锁作用域块**外**调用（周worth 深度
分析确证，无 update→snapshot 死锁路径）；快照 data 为转换缓冲 `std::move` 持有
（深拷贝语义源码确证）；gl 分支 `last_error="gl path not implemented"` 显式拒绝；
runner 凭据仅经环境变量进入 paramiko（泄漏扫描无相机网段地址、无裸密码字面量）。

## 2. 观察项（不阻塞本判定）

**O-1 — librga 并发初始化间歇性 RGA_BLIT fail（建议实现方关注）**：
六路并发 run 的 log 中偶发 `RGA_BLIT fail: Invalid argument`（impl r2 一次/verify v1
两次；追加 5 轮采样 0/0/14/2/16 次波动；**单路 `--count 1` 三轮全 0**）。失败后
立即恢复：updates 恒 41→83→118 单调、presentable 恒 true、acceptance 全 PASS，
无状态污染。判别特征：仅多路并发启动初期出现，疑似 librga 全局上下文并发
初始化竞态；`convert_failures` 隔离符合交付契约 §2.6（失败不静默、不阻塞取流）。
不属 ITEM-009 声明判据面，登记交实现方在转换/初始化层评估（如串行 init 或
首帧前同步 getRgaVersion 预热）。

## 3. 覆盖与边界

- 版本: commit `8f469f6` 工作树；板端构建产物为 fresh 上传构建（双轮独立）。
- 手段: 交付入口独立复跑 + 独立正则解析比对 + 板端 CLI 负例 + 板端自有 C++ 夹具
  (11+9+7) + 本地源码级断言 16 + 失败率判别采样（单路×3、六路×5+2）。
- 未覆盖（交对应能力/用户）: 生产 UI 纹理上传与页面渲染上屏（桌面 UI）、
  真实相机六路主观画面（用户）、gl 零拷贝路径实测（qmlglsink 缺失显式不可用）。
- 板端收尾: `/tmp/railhmi_display` 已 `rm -rf` 并确认 `CLEANED`；本地无 pycache
  残留；无凭据落入任何证据文件。

## 4. 原始证据（本目录，均为本轮/交付原始产出）

| 文件 | 内容 |
|---|---|
| `display-…-VERIFY-v1.transcript.json` / `.log` | 验证轮 Paramiko transcript（8 步） |
| `display-…-VERIFY-v1.core-evidence_display-…-VERIFY-v1.log/.json` | fresh 复跑核心判据日志与汇总 JSON |
| `ext_cpp_board.log` | display_ext.cpp 11/11 板端输出 |
| `ext_cpp2_board.log` | display_ext2.cpp 9/9 板端输出 |
| `ext_cpp3_board.log` | display_ext3.cpp 7/7 板端输出（含 hexdump 级 RGA 失败 dump 原文） |
| `board_b7.log` | 六路 b7 采样（分离 stderr 计数 0 复核：RGA log 走 stdout 继承通道） |
