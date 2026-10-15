# 软件验证 TestEvidence — ITEM-001 目标构建依赖基线核验交付（software_verification）

- Capability: `software_tester.software_verification`
- 被检对象: `n005_embedded_linux_engineer.desktop_app_implementation.implementation`（commit `09c105f`，2026-10-15 14:38:15 +0800）
- 被检工作项: `TL-…-tasks-ITEM-001`（冻结目标构建依赖基线；判据来源 `DESKTOP_APP_DESIGN` §3.4）
- 验证日期: 2026-10-15（验证人本机执行，环境与归档证据一致：`DESKTOP-476JGQ4`，MINGW64 x86_64）
- 结论: **TEST_PASS**（口径见 §0）

---

## 0. 结论口径（防止跨层外推）

1. **本 PASS 的对象是 ITEM-001 的交付物质量与证据完整性**：探针工具可运行、可复现、如实记录，`build_env_check.md` 每条命令对应真实原始输出、未执行项显式标注未核验、主机结论与板端口径分离。该三项即本工作项声明的用户可观察点。
2. **本 PASS 不等于任何一条板端依赖满足**：E-01..E-11 的板端结论保持“未核验”，`docs/hmi/build_env_check.md` §2/§12 的“依赖基线未冻结（板端核验未执行）”声明经独立核对属实。
3. 板端执行需设备授权（项目设备工具当前禁用，见 capability 环境声明）；此缺口属环境事实，非交付物缺陷，交 Runtime/授权方推进，不由本验证循环重复重试解决。
4. 本机（Windows 主机）无 gcc/g++/cmake/pkg-config（独立复核），`DESKTOP_APP_DESIGN` §15 的 ENV-A 主机预览构建当前不可执行——属环境依赖缺口，记录在案。

---

## 1. 验证方法与独立复现（实际执行记录）

均在 `E:\work\rail_issue\rk3588_handover` 项目仓库内、只读方式对被检对象执行：

| # | 动作 | 结果 |
|---|---|---|
| V1 | `git show --stat 09c105f` 核对交付文件清单 | 与声明一致（探针、入口脚本、核验文档、原始报告 md/json） |
| V2 | 独立重跑探针：`python app/tools/env_probe.py --out …-verify.md --json-out …-verify.json` | 运行成功；生成的 `docs/hmi/test_evidence/ENV-B-dependency/env-probe-host-20251015-verify.md` 与归档报告 `env-probe-host-20251015.md` **除生成时间戳外逐行一致**（diff 验证），确认探针结果是确定性的、非伪造 |
| V3 | 归档 JSON 结构核对（`env-probe-host-20251015.json`） | 12 个检查段 E-00..E-11 齐备，共 43 条命令记录，每条含 `available/exit/output`；E-11 状态 `not_run_needs_authorization` 如实标注 |
| V4 | 抽查主机事实（不经探针，独立命令） | `uname`=MINGW64 x86_64；gcc/g++/cmake/pkg-config/dpkg 均 not found；`qmake -v`=Qt 5.15.2（Anaconda x86_64）；`sqlite3 --version`=3.50.6(32-bit)；`df -h /`=118G/115G/98%；`/sys/kernel/debug/rknpu`、`/proc/driver/mali`、`/tmp/.X11-unix` 均不存在——与归档报告一致 |
| V5 | `build_env_check.md` §2 状态表逐项对照原始 JSON/md | 每条摘录值均能在原始输出中找到（Qt/qmake、sqlite 3.50.6、ldconfig rga 空、rknpu 不存在 exit=1、DISPLAY 等） |
| V6 | 可疑值专项复核：归档报告 `printenv DISPLAY` → `needs-to-be-defined` | 经 `bash -lc` 登录 shell 实测确认该值真实来自主机环境（非探针代码臆造；探针 `env_probe.py:227` 的占位字面量是 `(unset)`，若伪造会不同），证据未被篡改 |
| V7 | `run_env_check.sh` 入口脚本读检 | 只调用探针、不改系统、输出三件套（md/raw/json），可复现入口成立 |

---

## 2. 判据逐条核对（工作项用户可观察点）

| 判据（来自工作项描述） | 实际观察 | 判定 |
|---|---|---|
| 新增 `docs/hmi/build_env_check.md`，每条命令与真实结果 | 文档 §3–§11 每条命令列出板端命令、板端状态（未核验）与主机原始输出摘录；摘录抽查（V5）与归档原件一致；原始结果完整归档于 `docs/hmi/test_evidence/ENV-B-dependency/` | 满足 |
| 未执行的项显式标注未核验 | §0 声明"本轮未连接 RK3588 目标板"；§2 表 E-01..E-11 板端状态全部标“未核验”或“未执行”；§12 明确"无一项在板端执行" | 满足 |
| 不得把主机可运行当作板端可运行 | §0/§2/§12 反复声明主机≠板端；探针报告自带环境判定规则（machine=aarch64 + rknpu 存在才算板端）；E-00/§3 也未把主机 Qt 5.15.2 当板端 Qt 覆盖 | 满足 |
| 对不可用项给出替代结论 | §3–§11 每条给"替代结论"（缺装路径/回退渲染/版本匹配要求/NVR 缺口归属等），并区分阻塞归属（平台、视频、模型、现场） | 满足 |
| `env_probe.py` 覆盖 §3.4 检查项 | E-01 Qt5.15+平台插件、E-02 gstreamer1.0-qt5/qmlglsink、E-03 Mali EGL/GLES、E-04 mppvideodec/glupload/glcolorconvert、E-05 rgavideoconvert+librga、E-06 librknnrt、E-07 sqlite3、E-09 磁盘均有对应检查函数；运行时协商实测按设计留给 ITEM-002（文档已声明） | 满足 |

## 3. 已核验主机原始数据摘要（补充归档，本轮验证生成）

- 我方独立复现报告: `ENV-B-dependency/env-probe-host-20251015-verify.md` / `.json`（内容与实现者归档一致）
- 本轮独立抽查的原始命令输出已记录于 §1 V4 表（未另行留全文，命令均可按表重复）

## 4. 结论

**TEST_PASS** — ITEM-001 交付物（依赖探针 + 板级核验流程 + 诚实标注的基线文档 + 原始证据）独立复现成立、证据免除疑问、判据逐条满足。板端依赖基线本身为“未核验”，不变更、不放宽任何验收判据；核验推进依赖设备授权，交 Runtime 调度后续授权与环境职责。
