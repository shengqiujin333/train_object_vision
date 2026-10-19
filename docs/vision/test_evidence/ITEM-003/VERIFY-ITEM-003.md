# ITEM-003 验证轮 — YOLOv5 人形检测后处理（VERIFY-20261019-item003-v1）

- Capability: `software_tester.software_verification`
- 被验证实现: `computer_vision_engineer.vision_inference_implementation` r3（commit 3971283，stamp `20261019-item003-r3`）
- 工作项: `TL-2253ff62-6914-4107-9b3f-56d1d6d04814-tasks-ITEM-003`
- 日期: 2026-10-19
- 结果: **TEST_PASS**

## 1. 验证模式（与实现方轮次独立）

| 步骤 | 手段 | 基线 |
|---|---|---|
| V-RUN | 交付入口 `app/tools/board_vision_detect.py` 独立复跑（`--keep-remote` 保留板端树），stamp `verify-20261019-item003-v1` | 全新板端会话：上传→CoreTests→selftest→6 fixtures→config→取回 17 份产物 |
| V-CMP | 独立解析比对 `verify/verify_item003_compare.py`（53 判据，不采信交付 acceptance/RESULT 行） | 实现 r3 归档 + 独立数学重推导（含对历史已部署 `code/yolov5_postprocess.cc` 公式的独立镜像） |
| V-EXT | 验证自有扩展夹具 `verify/vext_item003.cpp` 板端 g++ 直编（链接 `build-core/src/core/librailhmi_core.a`），30 判例 + CLI 负例 + 确定性 + 泄漏扫描 + 清理，驱动 `verify/verify_item003_vext.py` | 断言 core-tests/selftest 未覆盖的验收边界 |

环境: RK3588 192.168.0.103（`SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU`），Paramiko 登记 SSH 入口，凭据经 `TRAIN_RK3588_SSH_CREDENTIAL`（用户级导入本进程，不落日志）。g++ 11.4.0 / CMake 3.22.1 / upload 282 文件。

## 2. 独立复跑结果（fresh vs 实现 r3）

| 域 | 判据 | 实测 |
|---|---|---|
| T1 退出码 | 10 个步骤真实退出码（`*_EXIT` token 解析；外层 shell 恒 0 不可用——ITEM-002 教训①复现） | 全 0（build-core/core-tests/selftest×2/6 fixtures/config）；`--keep-remote` 无 cleanup；指纹在位；17 下载全 0 |
| T2 selftest | 判例数从探针源 `run_selftest` 段 `check(` 调用数统计=19（不手抄）；19 判例名独立逐条在场；`RESULT: PASS failures=0`；与 r3 全文恒等 | 全部命中 |
| T3 解码数学 | 独立 letterbox 公式（1280×720→640×640: scale=0.5 rw=640 rh=360 pad_x=0 pad_y=140）+ 夹具编码↔YOLOv5 sigmoid 后解码正反推镜像 + 逆 letterbox 归一化 → 六夹具 `type=detect`/`box` 行逐字段独立断言 | 全部命中：person (0.31250,0.44444,0.10000,0.17778) score=0.81 label=person cell=41,29 anchor=2 scale=0；nms boxes=1 suppressed=1（独立 IoU=0.6247 落 (0.45,0.98)）；iou0.99→2（配置联动）；nonperson dropped_class=1 boxes=0；empty DETECT_EMPTY；padbox dropped_bounds=1；int8 逐字段与验证侧独立反量化（**float32 scale 精度**）重算 0.31228/0.44409/0.10050/0.17831/score=0.8065 恒等 |
| T4 config | `confidence_min` 从 `app/config/thresholds.yaml` 源读=0.5；探针行 `score_used=0.500000 (from thresholds.confidence_min)`；conf=0.5 下 person 框保留（score=0.81>0.5） | 全部命中 |
| T5 core JUnit | `tests=1035 failures=0`；fresh vs r3 判例名多重集恒等；32 处 `check(` 调用名从 test 源括号深度扫描器独立提取（非手抄）在场全核（`->` 经 XML unescape 比对——ITEM-020 教训④） | 全部命中 |
| T6 脱敏 | fresh 全部 19 产物按凭据语义（`user:pass@` URL / `password=` 键值）+ 三凭据环境变量值直接命中扫描 | 0 命中 |

比对脚本最终口径 `COMPARE SUMMARY passed=53/53`（退出码 0），归档 `item003-compare-verify-20261019-item003-v1.json`。

## 3. 验证自有扩展夹具（vext 30 判例，板上全 PASS）

CoreTests/selftest 未覆盖的边界（`verify/vext_item003.cpp`，g 编号）：

- 三尺度联合：g01 三尺度各一框全部解码、g02 三框来自三尺度、g03 三 scale index 全部在场、g04 跨尺度分数混排降序（0.81/0.72/0.63）、g05 同目标跨尺度双候选 NMS 聚合到 1
- 置信度/白名单边界：g06 score 恰等阈值保留（`score < threshold` 半开边界）、g07 空白名单显式 `DET_BAD_CONFIG`（不是空框）、g08 白名单 {7} 时 person 丢弃并计数（白名单独立性）、g09 多类混合仅 person 保留、g10 跨类别不互抑（不同 cell person/car 高 IoU 双保留 suppressed=0）
- NMS 数学：g11 相接框（独立区间）双保留、g12 相接框 +1 约定 IoU==0、g13 等框 IoU>0.99
- letterbox 逆变换边界：g14 右/下越界裁剪 x+w≤1、g15 左越界裁剪到 0、g16/g17 1280×1024 letterbox pad_x==0/pad_y==64/scale==0.5、g18/g19 偏置 letterbox 逆变换 (256,0)-(960,512) 精确坐标
- 空图/畸形张量：g20 空 outputs 向量 → ok DETECT_EMPTY、g21 null err 指针安全、g22 uint8 路径独立编码解码 person（score 容差 0.02）、g23 uint8 scale=0 无崩溃行为锚定
- 排序/截断/统计：g24 max_detections=1 截断并 truncated=2、g25 截断保留最高分、g27 候选守恒 `candidates = boxes + dropped_class + suppressed + dropped_bounds + truncated`
- 确定性与一致：g28 两轮均 ok、g29 逐字段恒等（memcmp 对含 `std::string` 的 DetectBox 不可靠——逐字段）、g30 reason 稳定 token `DETECT_OK`、g31 label 词表 `person`/`cls_2`

驱动判定（`vext-item003-verify-20261019-item003-v1.json`）：9/9 检查 PASS — V0 保留树在位、V1 g++ 直编 1.4s、V2 板上 30/0 + 计数对账（源 30 处 check 调用）、14 个判例域在场、V3 CLI 负例 unknown-arg=2/no-action=2/help=0、V4 `--selftest` 同参两轮 sha256 恒等（`0e594f89…`）、V5 板端产物泄漏扫描 0 命中、V6 板端 `/tmp/railhmi_vision_item003_verify-20261019-item003-v1` 树与 vext 构件清理确认（`No such file or directory` + `CLEAN_DONE` 为删除后确认输出）。

## 4. 工作项要求逐条核对

| 任务书要求 | 验证证据 |
|---|---|
| 三尺度 YOLOv5 头解码 | T3：独立镜像公式（对照已部署 `code/yolov5_postprocess.cc`）+ vext g01-g05 三尺度联合/混排/跨尺度聚合 |
| 置信度过滤（阈值来自配置，不写死历史常量） | T4：score 阈值从 `thresholds.confidence_min` 读=0.5（源 YAML 独立核对）；CLI `--conf` 覆盖路径存在（探针 `--conf`）；vext g06 半开边界 |
| NMS（阈值来自配置，不写死历史常量） | T3：`DetectConfig.nms_iou` 传入；selftest `nms threshold from config changeable :: boxes=2`（iou0.99 联动）；vext g10/g11/g12 数学边界；实现文档明记 `nms_iou` 无配置字段以参数传入（不写死） |
| letterbox 逆变换到原图并归一化、x+y 边界不变量 | T3 person 精确坐标 + vext g14-g19 边裁/偏置 letterbox；全体 box 行 `x+w≤1`、`x,y≥0` 断言在位 |
| 只保留 person（cls_id==0），其余丢弃并可计数 | T3 nonperson dropped_class=1；vext g08 granting {7} 时 person 丢弃计数=1、g09 多类混合、g10 跨类别不互抑 |
| 夹具张量结果确定可复现 | V2 selftest 两轮 sha256 恒等 + vext g28-g29 逐字段恒等；六夹具输出行数/坐标/计数逐字段与 r3 恒等 |
| 空图产生空框而非错误 | T3 empty DETECT_EMPTY + vext g20 空 outputs 向量 → DETECT_EMPTY |
| 非 person 不进入结果 | person 保持独立 cell；vext g09 白名单 {0} 时 car 结果 0 框 + dropped 计数（非静默） |

## 5. 观察项（不阻塞，交实现/后续参考）

1. **O-1 `nms_iou` 无配置字段**：产品路径 score 阈值来自 `thresholds.confidence_min`；NMS IoU 以 `DetectConfig` 参数传入，探针默认 0.45 且证据记录来源。工作项要求"NMS 阈值来自配置，不写死历史常量"——当前实现满足"不写死"（无硬编码常量），但"来自配置"的配置面若产品层需要 NMS 配置键，属后续配置扩展（实现文档 §6.3 已自报，登记一致，不判缺陷）。
2. **O-2 NMS 在模型像素空间做 IoU，从归一化坐标重建时依赖 letterbox.scale/pad 重建**（`vision_detect_postprocess.cpp:266-277`）。数学上等价；若未来 letterbox 参数在两步间被修改会产生错位（当前同请求内一致，记录供后续调用方参考）。
3. **O-3 自实现 `apply_sigmoid=true` 路径**（logits 输入）仅由 default_false 覆盖，未验证 logits 路径（当前模型为 sigmoid 后输出，`apply_sigmoid=false` 是正确口径；本项不改判据）。
4. **O-4 r1/r2 返工轮**确认的实现侧自报 3 缺陷（`zp` 字段名、补边框夹具、int8 空背景语义）与保留的返工前证据一致；r2→r3 修复后 夹具移位/`q(0.0)` 空背景全部在 fresh 会话复核通过，无复发现。

## 6. 验证过程轮教训（记录在案，全部为验证侧构造错，无产品缺陷误报）

1. 比对 oracle 初版 53 判据中 7 处构造错（dry-run 预演零板成本全抓）：`fixlog` 占位序、逆 letterbox 漏除 scale（T3a/T3b 实偏 0.156 vs 0.312）、int8 反量化缺 two's complement、int8 用 double 精度 scale 而实际是 C++ float（229.49999→round 229→byte 101→0.8065，double 会错推 0.8135）、T5 提取器朴素逗号正则和括号扫描后取字符串两种皆不可靠 → 顶级逗号扫描器、EXIT 正则需 `[A-Z_0-9]+`、JUnit 判例名 `->` 需 XML unescape。
2. vext V1 首次编译失败：check() 无 detail 参重载（ITEM-023 教训①复现——"写判据调用前先定 check 签名"）。
3. vext g10 v1 同 cell 双类别：一个网格单元 argmax 只有一个类别，双类别须不同 cell（夹具设计根因，非产品缺陷）。
4. vext g17/g19 v1 letterbox 1280×1024 期望算错（把 144 宽 pad 记到纵向；正确 pad_x=0 pad_y=64）。
5. vext g29 v1 用 memcmp 比较 DetectBox（含 `std::string` 非 POD）→ 逐字段比较。
6. V3 负例范围：`--fixture` 缺名/未知名是探针参数面容忍行为（回退默认 person / else 分支），非负例语义——负例判据限于显式参数面错误（unknown-arg/no-action/help）。

## 7. 结论

ITEM-003（人形检测后处理）在验证自有驱动、独立数学与扩展夹具三口径下全部通过；1035/0 core 单测、19 判例 selftest、6 契约夹具、配置阈值路径、确定性、脱敏与清理全部满足；53 判据独立比对 + 30 扩展判例板上零失败。范围仅限 ITEM-003（夹具张量解码），真实 RKNN 前向与端到端检测属 ITEM-009（实现文档已声明）。
