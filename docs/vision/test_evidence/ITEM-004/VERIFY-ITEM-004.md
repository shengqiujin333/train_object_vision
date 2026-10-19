# ITEM-004 验证轮 — YOLOv8-seg 轨道分割后处理（VERIFY-20261019-item004-v1）

- Capability: `software_tester.software_verification`
- 被验证实现: `computer_vision_engineer.vision_inference_implementation` r1（commit 611dc62，stamp `20261019-item004-r1`，首次执行即通过无返工轮）
- 工作项: `TL-2253ff62-6914-4107-9b3f-56d1d6d04814-tasks-ITEM-004`
- 日期: 2026-10-19
- 结果: **TEST_PASS**

## 1. 验证模式（与实现方轮次独立）

| 步骤 | 手段 | 基线 |
|---|---|---|
| V-RUN | 交付入口 `app/tools/board_vision_seg.py` 独立复跑（`--keep-remote` 保留板端树），stamp `verify-20261019-item004-v1` | 全新板端会话：上传 287 文件 → CoreTests → selftest → 10 fixtures → config → 取回 25 份产物 |
| V-CMP | 独立解析比对 `verify/verify_item004_compare.py`（47 判据，不采信交付 acceptance/RESULT 行） | 实现 r1 归档 + 独立数学重推导（DFL softmax、闭式掩码计数、IoU+1、逆 letterbox 均由验证侧 `_oracle_calc.py` 独立推导后再写成断言） |
| V-EXT | 验证自有扩展夹具 `verify/vext_item004.cpp` 板端 g++ 直编（链接 `build-core/src/core/librailhmi_core.a`），37 运行判例（24 唯一名）+ CLI 负例 + 确定性 + 泄漏扫描 + 清理，驱动 `verify/verify_item004_vext.py` | 断言 core-tests/selftest 未覆盖的验收边界 |

环境: RK3588 192.168.0.103（`SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU`），Paramiko 登记 SSH 入口，凭据经 `TRAIN_RK3588_SSH_CREDENTIAL`（用户级导入本进程，不落日志）。g++ 11.4.0 / CMake 3.22.1。

## 2. 独立复跑结果（fresh vs 实现 r1）

| 域 | 判据 | 实测 |
|---|---|---|
| T1 退出码 | 15 个步骤真实退出码（`*_EXIT` token 解析；外层 shell 恒 0 不可用——ITEM-002 教训①复现） | 全 0（build/core-tests/selftest×2/10 fixtures/config）；`--keep-remote` 无 cleanup；主机指纹 `NzHFHZ…` 在位；25 下载全 0 |
| T2 selftest | 判例数从探针源 `run_selftest` 段 `check(` 调用数统计（22，不手抄）；22 判例名独立逐条在场；`RESULT: PASS failures=0`；fresh vs r1 全文恒等 | 全部命中 |
| T3 解码数学 | 独立 mirror：letterbox（1280×720→640×640: scale=0.5 pad_x=0 pad_y=140）+ DFL softmax 残差模型 + 掩码闭式计数（crop 128 行×55 正列=7040；nocrop 720×640=460800）+ 十夹具 `type=seg`/`box`/`mask` 行逐字段独立断言 | 全部命中：rail (0.45625,0.19999,0.10001,0.17779) score=0.9、zero_coeff 正=0 框保留、nocrop=460800、nms 独立 IoU+1=0.7808 落 (0.45,0.98) 抑制 1、empty SEG_EMPTY、badcount/badsegshape/badproto/badfmt 逐条错误串精确（`tensor count=12 expected=13` 等）、int8 0.1 scale 量化路径坐标容差 0.01 内恒等 |
| T4 config | `confidence_min` 从 `app/config/thresholds.yaml` 源读=0.5；探针行 `score_used=0.500000 (from thresholds.confidence_min)` | 全部命中 |
| T5 core JUnit | `tests=1059 failures=0`；fresh vs r1 判例名多重集恒等；24 个分割域判例名从单测源 `check(` 顶层逗号扫描器独立提取（非手抄）经 XML unescape 后逐名在场 | 全部命中 |
| T6 脱敏 | fresh 全部 25 产物按凭据语义（`user:pass@` URL / `password=` 键值）+ `TRAIN_RK3588_SSH_CREDENTIAL` 十字边界直接命中扫描（值恰为登记用户名 `admin`，子串同现假阳性已用长度≥10 + 词边界排除——ITEM-002 教训④同源） | 0 命中 |

比对脚本最终口径 `COMPARE SUMMARY passed=47/47`（退出码 0），归档 `item004-compare-verify-20261019-item004-v1.json`。

## 3. 验证自有扩展夹具（37 运行判例 / 24 唯一名，板上全 PASS）

CoreTests/selftest 未覆盖的边界（`verify/vext_item004.cpp`，g 编号）：

- 非平凡 DFL：g01 多 bin 混合 logits（{4:5,6:2}/{2:8}/{7:9}/{1:4,2:4}）→ 验证侧独立 softmax 求和预言 (0.3261869,0.0215695,0.1425754,0.0957470) 四坐标 2e-4 内命中（交付夹具仅覆盖独 hot bin 12 的近似 one-hot 路径）
- 掩码结构：g02-g04 box_index 关联 + dims/stride/size + 逐像素点积样本
- crop 几何闭式：g05 positive==7040（proto 半幅 pattern 前提下）、g06 行 240 正列恰 55（585..639）、g07 proto 改值后重解码无陈旧 plane（所有权独立）
- nocrop/NMS 掩码联动：g08 crop_to_box=false → 460800、g09 NMS 后掩码只随保留框
- NMS 手算：g10 IoU+1=0.7808 抑制、g11 阈值 0.79 恰在 IoU 上方 → 双保留（半开边界）、g12 bins(dl=5) 宽框 IoU=0.8025 抑制、g13 三分离框三保留（三 IoU 全独立复算：0/0/0.2539<0.45）
- 极端参数：g14 mask_threshold=-1 → crop 内 16384 全正（负阈值显式语义）、g15 同夹具阈值 0 → 连续语义全 crop 正（非布尔化）、g16 num_classes=2 双类标签 rail/cls_1
- 跨尺度联合：g17-g19 s1(15,15) 与 s2(12,10) 八坐标独立预言逐字段命中
- 畸形张量：g20 proto 非方 200×120 → MODEL_MISMATCH、g21 s2 分支（基偏移 8）seg 通道残缺 → MODEL_MISMATCH（v1 误用 s1 索引的夹具错已修）
- 确定性/边界：g22 两轮逐位 memcmp 恒等、g23 右缘裁剪 x+w==1.0 精确（1e-6）、g24 reason token 稳定 `SEG_OK`

驱动判定（`vext-item004-verify-20261019-item004-v1.json`）：9/9 检查 PASS — V0 保留树在位、V1 g++ 直编 1.3s、V2 37/0 + 源↔板判例名集合级恒等（全部 check/check_near 顶层逗号扫描提取，g05/g06 else 守卫恒不触发的集合级口径）、判定域 17 组全在场、V3 CLI 负例 unknown-arg=2/no-action=2/help=0、V4 `--selftest` 同参两轮 sha256 恒等（`e739df92…`）、V5 板端产物泄漏扫描 0 命中、V6 板端 `/tmp/railhmi_vision_item004_verify-20261019-item004-v1` 树与 vext 构件清理确认（`No such file or directory` + `CLEAN_DONE` 为删除后确认输出）。

## 4. 工作项要求逐条核对

| 任务书要求 | 验证证据 |
|---|---|
| 13 输出解析（3 尺度各 box/score/score_sum/seg 系数 + proto） | T1 十夹具全跑通 + T3 逐夹具 `type=seg` 行；T2 selftest `12 tensors -> MODEL_MISMATCH` |
| 分割头解码（DFL） | T3 rail 独立 mirror 精确坐标；vext g01 非平凡多 bin logits 独立 softmax 预言 |
| per-box 系数·proto 点积 → 掩码 | T3 g07/mask 行 positive=7040；vext g04a/g04b 逐像素 + g07 通道闭式 + g16 双类独立掩码 |
| 阈值化为二值 | T2 `mask binary + has positives`；vext g14/g15 阈值连续语义（负阈值/0 阈值两口径） |
| 逆 letterbox 到原图坐标 | T3 独立逆变换公开公式（scale/pad/clamp/归一化）；vext g17-g19 s1/s2 独立预言 + g23 x+w==1.0 |
| 原图尺寸独立二值 plane（stride/所有权） | T3 mask 1280x720 stride 1280 size 921600；vext g04/g07（proto 改值重解码无陈旧 plane = 独立所有权行为面） |
| 与 rail 框/分数一一对应 | T2 `box<->mask one-to-one`；vext g09 NMS 后 box_index==0、g13 三框三掩码、g16 双框双掩码 |
| 数量/形状不符显式 MODEL_MISMATCH | T3 四类负例错误串逐 token 精确 + selftest box63；vext g20 非方 proto / g21 s2 分支残缺（交付夹具只覆盖同位 s0 变体，vext 补 s2 位）|

## 5. 观察项（不阻塞，交实现/后续参考）

1. **O-1 阈值来源**：score 阈值取自 `thresholds.confidence_min`（T4 源核对）；`nms_iou`/`mask_threshold`/`crop_to_box` 为 `SegConfig` 参数传入无配置键（与 ITEM-003 O-1 同型，实现文档 §3 已自报）。
2. **O-2 夹具 DFL 残差**：logit 12 独热 softmax 有效值 ≈4.00045 而非解析 4；坐标与解析解差 <1e-3（selftest 容差内）。vext g01 用多 bin 混合 logits 无残差问题。真实模型量化输出的 DFL 语义映射交 ITEM-009 前向轮核。
3. **O-3 双线性中间量内存**：每框 640×640 float 中间量 ~1.6MB + 逐原图像素采样（720×1280 双循环）；多框大批量耗时/内存归 ITEM-009/执行轮（实现文档 §6.2 已自报）。
4. **O-4 r1 无返工**：本轮实现一次通过，验证三口径亦无产品缺陷发现；历史已部署解码（`code/rknpu2_postprocess.cc`）语义（NCHW/DFL/score_sum 预筛/+1 IoU/`>0` 阈值）逐项与实现核对一致。

## 6. 验证过程轮教训（记录在案，全部为验证侧构造错，无产品缺陷误报）

1. 比对 oracle dry-run 抓 5 处构造错：s0 stride 误写 16（640/80=8）、JUnit `<testcase name=` 正则未对 `<testcase classname="…" name="…"` 属性序归一（双重集空转——**新增非空守卫防断言空转**）、`->` entity 替换写成 `"->"` 致 `-->` 失配（ITEM-020 教训④变体：先手算替换链再落码）、凭据值恰为 `admin` 子串假阳性（长度≥10 + 词边界）、check 名提取单行正则只抓 7/24（条件含逗号 → 顶层逗号扫描器，ITEM-003 v1 同教训复现）。
2. vext 首轮板上 11 失败全为验证侧夹具构造错：proto 全 +1 下 7040/nocrop 460800/半幅前提丢失（`proto_half` 对照交付 `fill_proto` 补齐）、g12 同 cell 双 encode 覆盖（相邻网格 cell 才是正确设计）、g21 用 s1 索引 10 而非 s2 索引 11（`base=8+3`）、g23 选格 (70,79) 使 x1 先越界、g13 首框 (10,10) 落 pad 区被 `dropped_bounds` 丢弃（i=30 修）— 全部经诊断 detail（`res_sum` 摘要）定位后修订复跑，板上终态 37/0。
3. vext 计数判据两变形：行首正则在多行条件第二行也匹配（`check(std::fabs` 定义体）→ 集合级判例名比对（源顶层逗号扫描 ⊕ check_near 最后字符串实参展开）与板侧 PASS 名单逐名相等，替代易碎的线性调用计数；`check_near(a,b,tol,name)` 名在第 4 参非第 2 参——提取前先确认门面签名再选参位。
4. vext 驱动 `re.finditer(r"[^\\s\\n]check\\(",...)` 修正为行首形态 + lstrip 前缀排除（v1 直接 `startswith` 恒假——从 `^\s*` 起的 match 头必含空白）；`sum(genexp)` 需括号的语法坑一次编译即报，未上板。
5. 命令网关拒绝三类 shell 形态（grep/-c 多语句引号/heredoc），全部改用原生 Read/Grep/Write 工具流完成——未绕过 gate。

## 7. 结论

ITEM-004（轨道分割后处理）在验证自有驱动、独立数学与扩展夹具三口径下全部通过；1059/0 core 单测、22 判例 selftest、10 契约夹具（含 5 类 MODEL_MISMATCH 负例）、配置阈值路径、确定性、脱敏与清理全部满足；47 判据独立比对 + 37 运行判例（24 唯一名）板上零失败。范围仅限 ITEM-004（夹具张量解码），真实 RKNN 前向与端到端掩码属 ITEM-009（实现文档已声明）；夹具 pass ≠ 真实模型掩码质量（用户视觉核验边界按当前契约保留）。
