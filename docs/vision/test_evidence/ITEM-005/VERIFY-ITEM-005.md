# ITEM-005 验证轮 — 同帧两阶段判定（VERIFY-20261019-item005-v1）

- Capability: `software_tester.software_verification`
- 被验证实现: `computer_vision_engineer.vision_inference_implementation` r4（commit f1c5d92，stamp `20261019-item005-r4`；r1→r3 为实现返工轮：r1 头文件默认构造重复定义、r2 不可用语义返回值错误、r3 探针悬垂临时数组）
- 工作项: `TL-2253ff62-6914-4107-9b3f-56d1d6d04814-tasks-ITEM-005`（同帧两阶段判定，front）
- 日期: 2026-10-19
- 结果: **TEST_PASS**

## 1. 验证模式（与实现方轮次独立）

| 步骤 | 手段 | 基线 |
|---|---|---|
| V-RUN | 交付入口 `app/tools/board_vision_two_stage.py` 独立复跑（`--keep-remote` 保留板端树），stamp `verify-20261019-item005-v1` | 全新板端会话：上传 292 文件 → CoreTests 构建 → core-tests → selftest → 17 fixtures → config → 取回 39 份产物 |
| V-CMP | 独立解析比对 `verify/verify_item005_compare.py`（83 判据，不采信交付 acceptance/RESULT 行） | 实现 r4 归档 + 独立重推导（掩码像素采样预言、缓存年龄算术、roi.yaml 源值、JUnit 判例名从单测源顶层逗号扫描器提取） |
| V-EXT | 验证自有扩展夹具 `verify/vext_item005.cpp` 板端 g++ 直编（链接 `build-core/src/core/librailhmi_core.a`，远离 probe=交付自检），33 运行判例 + CLI 负例 + 确定性 + 泄漏扫描 + 清理，驱动 `verify/verify_item005_vext.py` | 断言 core-tests/selftest 未覆盖的验收边界 |

环境: RK3588 192.168.0.103（host key `SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU`，与实现轮一致），Paramiko 登记 SSH 入口，凭据经 `TRAIN_RK3588_SSH_CREDENTIAL`（用户级导入本进程，不落日志）。g++ 11.4.0 / aarch64 Linux 6.1.141。

## 2. 独立复跑结果（fresh vs 实现 r4）

| 域 | 判据 | 实测 |
|---|---|---|
| T1 退出码 | 20 个步骤真实退出码（`*_EXIT` token 解析；外层 shell 恒 0 不可用——ITEM-002 教训①复现） | 全 0（build/core-tests/selftest×2/17 fixtures/config）；`--keep-remote` 无 cleanup；主机指纹在位；39 下载全 0 |
| T2 selftest | 判例数从探针源 `run_selftest` 段 `check(` 调用数统计（30，不手抄）；22 语义判例名独立逐条在场；`RESULT: PASS failures=0`；fresh vs r4 全文恒等 | 全部命中 |
| T3 17 夹具 | `type=two_stage` 行逐字段独立断言：像素采样预言由验证侧掩码几何独立重算（band=y∈[432,719] 全宽；中心 (0.5,0.8) 命中 / (0.5,0.2) 未命中 / (0.5,0.9) 命中 / (0.5,0.7) 在轨不报警）、缓存年龄独立算术（100ms≈复用、900ms>500ms 过期、跨 gen 拒绝）、越界中心 1.07>1 | 全部命中，含 multi 三人员 det 行逐字节（mask_hit=1,1,0 alarm=1,1,0）、stale=TWO_STAGE_STALE、frame_mismatch 独立 reason、5 类不可用行 ok=0 available=0 alarms=0 |
| T4 config | danger_zone 从 `app/config/roi.yaml` 源读 CAM-01 `x=0.08 y=0.82 w=0.84 h=0.16`（验证侧源文件重读比对） | `danger_zone=found` + zone 求交路径全命中 |
| T5 core JUnit | `tests=1084 failures=0`；fresh vs r4 判例名多重集恒等；两阶段域判例名从单测源 `railhmi_test::check(` 顶层逗号扫描器独立提取（26 名，非手抄）经 XML unescape 后逐名在场 | 全部命中 |
| T6 脱敏 | fresh 全部 41 产物按凭据语义（`user:pass@` URL / `password=` 键值）+ `TRAIN_RK3588_SSH_CREDENTIAL` 十字边界直接命中扫描（长度≥10 + 词边界——ITEM-002/004 教训复现） | 0 命中 |

比对脚本最终口径 `COMPARE SUMMARY passed=83/83`（退出码 0），归档 `verify/item005-compare-verify-20261019-item005-v1.json`。

## 3. 验证自有扩展夹具（33 运行判例，板上全 PASS）

CoreTests/selftest 未覆盖的边界（`verify/vext_item005.cpp`，g 编号；像素/ROI 预言全部经离线 float32 dry-run 先行推导）：

- 掩码像素语义：g01 中心恰在带起点像素行（float32 中心 0.6000000127→py=432 命中）；g01b 一行之上（py=431）未命中；g02 中心恰图右缘 px=1279 clamp 内命中；g02b 中心>1 越界丢弃；g03 stride=1288>width 带 padding 定位正确；g03b stride<width 整体不可用；g04 多掩码第 2 掩码命中；g06 1×1 像素掩码精确命中 (511,432)
- 空语义：g05 尺寸合法但 positive=0 的全零掩码 → UNAVAILABLE 且 detections 恒空（**不输出“轨道内安全”的结构性保证**）
- danger_zone 边界：g07 左上角 (0.08,0.82) 闭区间包含；g07b 右下角用同 double 算术 `z.x+z.w, z.y+z.h` 断言（字面 0.92/0.98 因 float 表示误差在边界外——**预期必须与实现同算术构造**）；g07c/g07d 紧贴外排除；g08/g08b float32 可构造中心紧贴 zone 上下缘内命中、g08c 缘外掩码命中但 zone 排除
- 缓存策略边界（TASK 书“登记最大年龄与失配策略”逐条）：g10 enabled 但 max_age_ms=0 恒不复用（登记要求强制）；g11 时间倒退 age<0 拒绝；g12 age==max_age_ms 恰等复用（≤ 语义）；g13 max_age+1ms 过期；g14 `reject_on_generation_change=false` 显式登记后跨 generation 复用（策略开关语义）；g14b 同配置下跨 session 仍拒绝；g15 缓存命中 detections 完整语义；g16 缓存禁用评估器 store 后 `cache().has()==false`
- 配置语义：g17 `require_same_frame=false` 显式允许跨帧；g18 null 输出指针参数错误；g19 非正图像尺寸 BAD_CONFIG；g20 persons=nullptr → NO_PERSON 可用；g21 masks=nullptr 不可用；g22 不可用时 detections/person_total 恒零；g23 7 个 reason token 稳定；g24 同参两轮确定性

驱动判定（`vext-item005-verify-20261019-item005-v1.json`）：9/9 检查 PASS — V0 保留树在位、V1 g++ 直编 1.4s、V2 33/0 + 源↔板判例名集合级恒等（原文扫描器，剥 `::` detail 后缀）、判定域 g01..g24 全在场、V3 CLI 负例 unknown-arg=2/no-action=2/help=0/unknown-fixture=2、V4 `--selftest` 同参两轮 sha256 恒等（`fab028dd…`）、V5 板端产物泄漏扫描 0 命中、V6 板端 `/tmp/railhmi_vision_item005_verify-20261019-item005-v1` 树与 vext 构件清理确认（`No such file or directory` + `CLEAN_DONE` 为删除后确认输出）。

## 4. 工作项要求逐条核对

| 任务书要求 | 验证证据 |
|---|---|
| 同一 FrameDescriptor 身份（session_id/generation/seq）下同帧运行分割与人员检测 | T3 center_in/out 逐字段 + T2 seg/detect_frame≠frame 两条 FRAME_MISMATCH selftest 判例 + vext g17 关闭开关后的显式语义 |
| 人员框中心点落 rail 掩码内 → in_roi=true 与 person 语义 | T3 det 行 `mask_hit=1 alarm=1` + label=person 透传（vext g09）；像素采样预言验证侧独立重算 |
| 配置 danger_zone 时与 roi.yaml 求交 | T4 config 路径（roi.yaml CAM-01 源值）；vext g07/g08 边界族；r4 danger_zone_hit/miss 双夹具 |
| 禁止跨帧/跨 generation 缓存作实时报警依据 | vext g10-g14b 缓存策略边界族（默认关/零年龄/倒退/恰等/过期/跨代/跨 session）；T3 cache_disabled/expired/gen_change 三夹具独立断言 available=0 used_cache=0 |
| 缓存启用必须登记最大年龄与失配策略且默认关闭 | vext g10（enabled+max_age=0 拒绝复用）+ g14/g14b（登记字段逐项到 lookup 行为面）+ T3 cache_hit 显式 used_cache=1 cache_age_ms=100；默认 `enabled=false, max_age_ms=0`（头文件默认值 + selftest 判例） |
| 旧 generation/乱序帧被拒绝 | T3 stale 夹具（TWO_STAGE_STALE）+ T2 selftest 重复/乱序/旧 generation 三判例；FrameGate 源码语义（stale_generation < 已纳代、duplicate/out_of_order seq）核对一致 |
| 分割失败或掩码为空时不输出轨道内安全结论而是标记不可用 | T3 seg_unavailable/mask_empty/mask_misaligned 三类 ok=0 available=0 + vext g05 全零正掩码 + g22 detections 恒空结构性断言 |
| 同帧命中判定可复现 | vext g24 同参两轮全字段一致 + V4 selftest sha256 恒等 + fresh vs r4 全文恒等 |

## 5. 观察项（不阻塞，交实现/后续参考）

1. **O-1 真实 RKNN 前向未执行**（实现文档 §6.1 已自报）：本增量用夹具张量/掩码验证判定逻辑；真实前向+端到端两阶段归 ITEM-009。夹具 pass ≠ 真实模型掩码质量。
2. **O-2 掩码 double 采样语义**：`mask_positive_at` 用 floor(cx*W) 单点采样（IMG 像素离散）；float32 中心在 zone 边界 ±1e-9 处的可区分性受表示精度限制（g07b 教训）——后续 ITEM 若要求亚像素边界精度需在设计层声明。
3. **O-3 缓存复用拷贝粒度**：整幅掩码（921600B）按值拷贝一次/复用帧（实现文档 §4 已自报）；多路并发下内存与耗时归 ITEM-009/执行轮。
4. **O-4 danger_zone 仅 CAM-01 注册**：roi.yaml 中 CAM-04/CAM-05 有其它 ROI 但两阶段危险区语义本期只接 CAM-01（与 ENV 配置 front 相机一致）；其余相机接入时需逐路登记。
5. **O-5 探针 R=`--config` 只测 danger_zone_hit 单路径**：未配置 danger_zone 相机的 config 路径（`danger_zone=missing` 歧义分支）探针返回 1 判 FAIL，无正例夹具覆盖“配置了其他相机 ROI 但 CAM-01 缺 danger_zone”的产品语义——登记参考，不判缺陷（与交付能力声明“front 相机”一致）。

## 6. 验证过程轮教训（记录在案，全部为验证侧构造错，无产品缺陷误报）

1. V-CMP dry-run 抓 2 处构造错：fixture 日志不含 `detail:` 行（探针不输出该字段——删除 T3h 空转断言）；frame_mismatch 有独立 reason 不应与 UNAVAILABLE 类同断言（预期先行自查②）。
2. vext 板端过程轮教训：① `check(name, fmt, ...)` 无 printf 风格变参重载（g++ 参数个数即报——ITEM-021 教训①同源再确认：**写调用前先定门面签名**）；② `mk()` 缺 `seg_ok` 参数使缓存测试第二帧 seg 恒可用、6 判例空转假失败（夹具助手签名契约先写注释再实现）；③ ROI 右/下边界断言必须与实现同 double 算术构造（`z.x+z.w`）而非十进制字面量 0.92/0.98（dry-run 已算出偏差仍误读dry-run——float 表示差 e < 1 ulp 但方向恒负偏，ITEM-022 FP 容差教训的方向变体）；④ PASS 名剥 `::` detail 后缀做集合级比对（g01 带运行时值时 only_board 恒非空假失败）；⑤ 汇总行正则 `(N failure)` 有右括号被字面 `\)` 转义模式失配（`...failure` 宽匹配替代，ITEM-018/020 教训④同源）；⑥ 夹具源扫描必须在原文上提取（剥串副本查不到名字——七处空转教训新变体：**解释器先剥串后提取的顺序假象**）。
3. 临时自查文件目录错置（3 个 `_verify_inspect*.py` 写到了 ITEM 根目录而非 verify/）已删除归还；同轮 V-CMP 首版下载数断言 38 与实际 39 一次失配（遗漏 transcript/log 双口径外的 1 份额外下载）按实测修正。模板复用时跨行文本残留已清。

## 7. 结论

ITEM-005（同帧两阶段判定）在验证自有驱动、独立比对与扩展夹具三口径下全部通过：1084/0 core 单测、30 判例 selftest、17 契约夹具（含 7 类不可用/拒绝负例）、roi.yaml 配置路径、缓存策略边界族（默认关/零年龄/倒退/恰等/过期/跨代/跨 session）、掩码像素与 danger_zone 边界（float32 同算术构造）、确定性、脱敏与清理全部满足；83/83 独立比对 + 33 运行判例（板上零失败）+ 9/9 驱动检查全绿。范围仅限 ITEM-005（夹具张量的纯逻辑判定），真实 RKNN 前向与端到端两阶段属 ITEM-009（实现已声明）；真实画面命中/误报人工视觉核验按当前契约保留给用户。
