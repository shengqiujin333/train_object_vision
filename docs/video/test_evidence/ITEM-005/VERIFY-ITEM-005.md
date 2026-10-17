# ITEM-005 librga NV12→BGR 格式转换 — 软件验证 TestEvidence（VERIFY-20261017-v1）

- Artifact: TestEvidence（software_verification）
- Capability: `software_tester.software_verification`
- 工作项: 视频任务 ITEM-005「librga NV12→BGR 格式转换」（task_id `…-tasks-ITEM-005`）
- 实现版本: commit `9f36751`（交付 `docs/video/implementation/ITEM-005_RGA_CONVERT.md`，板端 stamp `20261017-item005-r3`）
- 验证 stamp: `20261017-item005-verify-v1`（本目录 `rga-…verify-v1.log/.transcript.json` + `rga-ext-….json`）
- 日期: 2026-10-17；目标: compute_host `192.168.0.103`（RK3588 / aarch64 / GStreamer 1.20.3 / librga rga_api 1.10.1）
- host key fingerprint: `SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU`（与实现方 r3 同板同 key）

## 1. 结论

**TEST_PASS**（范围=ITEM-005 当前版本：`rga_hstride` padding 数学、`nv12_to_bgr_raw/lease` 转换与双成功码、显式拒绝族、取流 worker 内转换统计与失败不丢帧、管线无 `rgaconvert`；不含显示上屏 ITEM-009、推理预处理 ITEM-010、多路并发容量 ITEM-007/017、BT709 caps 归一）。

## 2. 独立复跑（不采信实现方归档）

交付入口 `app/tools/board_rga_convert_check.py --host 192.168.0.103 --user admin --outdir <本目录> --stamp 20261017-item005-verify-v1 --keep-remote`（凭据经 PowerShell 用户级环境导入当进程），全部 8 步 exit=0：

| 步骤 | 结果 | 与实现方 r3 对比 |
|---|---|---|
| build-media | `BUILD_MEDIA_EXIT=0`（media 层全量重编含 `rga_convert.cpp`） | 一致 |
| selftest | 8/8 PASS（hstride×4 + 无 rgaconvert×2 + decodebin/mppvideodec×2） | 逐行一致 |
| rga-test（test 源） | 合成判据 3 行 + worker_convert + fail_keep_frames + summary，`bgr_failed=0` | 合成三行逐字段一致；worker published 76/76 同 r3，bgr_means 138/123/124、spread 16 与 r3 相同 |
| rga-rtsp（真实 CAM-01） | 同序判据行全部在位 | 合成三行一致；worker published 73 vs r3 72、bgr_means 133/133/128 vs 119/111/106 —— 帧数/画面均值是**自然波动项**（拍摄内容/到达时机不同），非判据；判据 `bgr_converted==published 且 failed=0` 两侧均成立 |

合成判据（实现 r3 vs 验证 verify-v1，逐字段相同）：

- `synthetic_padded … b=200 g=200 r=200 spread=0 min_px=200 max_px=200 padding_not_leaked=true`（padded stride=1408、uv_offset=1408×720）
- `synthetic_blue … b=239 g=15 r=15 blue_dominant=true`
- `failure_paths null_rejected=true(null_arg) odd_rejected=true(odd_dimensions) notready_rejected=true(NOT_READY)`

## 3. 验证侧扩展夹具（EXT 11/11，自有判据；`rga-ext-…verify-v1.json`）

绑定 `--keep-remote` 保留的 `/tmp/railhmi_rga` 树，黑盒驱动交付入口 + 板端 g++ 直编：

| # | 判据 | 结果 |
|---|---|---|
| A1 | 探针独立再跑一轮，解析 `synthetic_padded` 行（不采信交付 acceptance 行）：灰 200 精确三通道、spread=0、min/max=200（0 padding 不泄漏） | PASS |
| A2 | `synthetic_blue` 独立判据：b>g+100 且 b>r+100 | PASS |
| A3 | 失败路径 token 独立判据：`null_arg / odd_dimensions / NOT_READY` 三者显式非空 | PASS |
| A4 | `fail_keep_frames still_producing=true 且 published_after>before`（失败不丢帧用户可观察） | PASS |
| B | 确定性：同参两轮 `synthetic_padded`/`synthetic_blue` 行逐字段 identical（纯转换路径无自然波动项） | PASS |
| C | 未知参数 `--bogus-flag` → exit 2 且报 `unknown arg`；`--help` → exit 0 含 `--selftest` | PASS |
| D | 板端 g++ 直编 `src/media/src/rga_convert.cpp`（+frame_lease/core/platform，链 librga/gst）**验证侧自有 28 判例** → `vext OK (0 failure(s))` VEXT_EXIT=0 | PASS |
| E | worker 段独立判据（1s test 源轮）：`bgr_converted==published>0 且 bgr_failed=0 且 last_error=` | PASS |
| F | 凭据泄漏扫描：本目录全部 ITEM-005 证据文件对两相机凭据值 0 命中（SSH 凭据=admin 同 ITEM-002/004 口径不计 token 假阳性） | PASS |

D 项自有向量（28 判例，均不与实现 selftest 重合）：

- hstride 数学：1408/720、1280/720、1920/1080 单行（`hstride=1` 边界可整除值）、非整数倍/零 stride/负 stride/零 uv_offset 拒绝（8）。
- **红/绿纯色 BT601_FULL 正向向量**（实现探针只有灰/蓝）：红 Y=76,U=84,V=255 → R 主导（r>b+100、r>=200、b,g<=80）；绿 Y=149,U=43,V=21 → G 主导 —— 证明通道序/方向在三个基色上全对，绿色结果正常且非绿屏（10）。
- **0xFF 乱值 padding 注入**（实现探针 padding 恒 0）：stride=1408 全 padding 列/行填 0xFF、像素区灰 200 → 输出 min/maxpx 均==200，证明来自 stride 的垃圾字节未泄漏（2）。
- **UV(U/V) 交换负对照**：U=240,V=110 蓝主导 vs 交换后红主导 —— 直接证明源 U/V 平面喂反会被检出（4）。
- 拒绝族：null/负维/奇维/stride 过小/uv_offset 非整数倍/空租约 NOT_READY（6）。
- spread 语义：空图 -1、灰图 0（2）。

## 4. 版本指纹

- 板端复跑 stamp `20261017-item005-verify-v1`；上传 195 文件（本地 `app/`）。
- 新证据 sha256(16)：VERIFY 文档 `7ae592cb3c11b218`、transcript `5a65f9decd32dd43`、复跑日志 `e30792dc3f70d1bf`、EXT JSON `83c068c14546d044`、test/rtsp 原始件 `1b59adbeaaf2d15d`/`98c9761e962b9263`。
- 判据为当前版本行为，不外推其它组件/版本。

## 5. 边界与说明

- 本验证不覆盖：BT709 输入色彩差异（caps 未携带明确 BT709 元数据，Design 允许 BT601_FULL 基线并显式报告）、显示上屏（ITEM-009）、推理取 `BgrImage`（ITEM-010）、多路并发 RGA 争用（ITEM-007/017）。
- 观察项 O-1（交实现参考，非缺陷）：`rga_hstride(1280,1280)=1`（uv_offset 仅一行）通过显式校验后由 librga 决定行为；实现拒绝条件按设计契约（非整数倍/ystride≤0/uv_offset=0），未新增验收判据。
- 观察项 O-2（非缺陷）：真实 RTSP 通道 BGR 均值每轮不同（画面内容决定）；「三通道均值近似相等」只对中性/灰图成立，真实场景按 `bgr_converted==published` 与纯色负对照判定。
- 验证侧过程记录：v1 夹具一处构造错（D 编译链接路径写 `build-media/src/librailhmi_core.a`，实际树布局为 `build-media/src/core/...`，ITEM-004 upload_tree 平铺教训同源），上板第一轮暴露后修正复跑全绿；无产品缺陷误报。
- 转换失败时机允许注入点有限（坏 descriptor 场景在 D 项自有负例覆盖 `nv12_to_bgr_raw` 拒绝族；`rga_error:` 前缀路径需要驱动层 mock，不在黑盒夹具范围，交 ITEM-006+ 状态机轮自然覆盖 `bgr_failed/bgr_last_error` 行为面）。

## 6. 复现

```bash
python docs/video/test_evidence/ITEM-005/_verify_run.py   # 独立复跑（需板端/相机凭据 env）
python docs/video/test_evidence/ITEM-005/_verify_ext.py   # EXT 夹具（需 --keep-remote 树在板）
```
