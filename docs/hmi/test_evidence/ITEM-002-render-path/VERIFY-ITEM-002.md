# 软件验证 TestEvidence — ITEM-002 渲染路径选型 spike 交付（software_verification）

- Capability: `software_tester.software_verification`
- 被检对象: `n005_embedded_linux_engineer.desktop_app_implementation.implementation`
  （artifact_implementation.yaml → `app/spikes/render_path/README.md`；对应仓库 commit `5494dde` 之工作树）
- 历史尝试对比: CALL-813a68cc（WORK_FAILED）→ CALL-bef9cdd6（WORK_FAILED）→ CALL-eb4ab17c（IMPLEMENTED，本轮被检对象）
- 被检工作项: `TL-…-tasks-ITEM-002`（完成渲染路径选型 spike：板端单路 RTSP 实测两管线，回填 build_env_check.md §14）
- 验证日期: 2026-10-15（验证人本机执行，Windows 10 / MINGW64，仓库 `E:\work\rail_issue\rk3588_handover`）
- 结论: **TEST_FAIL**（口径见 §0；未满足点与根因见 §3）

---

## 0. 结论口径（防止跨层外推）

1. 本 FAIL 的判定对象是**工作项声明的用户可观察点**："build_env_check.md 追加实测数据与选定的主/回退路径"。
   该点在本轮交付物中**未产出**——`build_env_check.md` §14.1/§14.2 与 `render_path_spike.md` §5 均如实标注
   "板端实测未执行 / UNMEASURED"，§14.4 显式声明"待授权后执行"。按当前能力契约，
   "缺失真实执行结果的目标评价不能算通过"，故返回 TEST_FAIL。
2. 本 FAIL **不判定** spike 源码/判据为缺陷：实现侧工具链（`main.cpp`、QML、CMake、
   `run_render_spike.sh`、`analyze.py`）本轮独立复现全部通过（§1 S1–S4），判据自检 6/6，入口脚本贯通，
   文档与代码一致。未满足部分是**板端执行与实测数据回填**，其根因为项目设备工具禁用（无 RK3588 板、
   无相机授权），属环境/授权缺口。
3. 按 `E2E_PLAN.md` §3（项目自有规则）："板端档失败若根因属板端资源/驱动/BSP/设备授权 → 记录为依赖缺口，
   不算软件功能失败"。本验证依据 **TEST_DESIGN** §0.1（"板上的任何均未实测，不得写成已通过"）与
   §1 环境分层（ENV-C/D 均为"待实现 + 设备授权"），将缺口如实写入失败记录（§4 交接），
   **不由验证循环反复重试解决**——重试上限由 Runtime 控制。
4. 本验证未改动任何被检实现文件（capability 边界：不改产品源码）；本轮新增文件仅
   本 TestEvidence 与 §5 列出的验证产物。

---

## 1. 独立复现（实际执行记录）

| # | 动作 | 命令（本机实际执行） | 结果 |
|---|---|---|---|
| S1 | 入口自检 | `bash app/spikes/render_path/run_render_spike.sh --dry-run` | 退出码 0；输出构建→gl 测量→cpu 测量→判据选择四步计划，未构建、未访问板端，路径与产物命名与 README §运行 一致 |
| S2 | 判据自检 | `python app/spikes/render_path/analyze.py --selftest` | **6/6 通过**：gl 可用选 gl / gl 失败回退 cpu / 双失败 none / 色彩不通过否决 / 帧率不足否决 / 无数据 UNMEASURED |
| S3 | 无输入行为 | `python app/spikes/render_path/analyze.py`（无测量数据） | 输出 UNMEASURED、退出码 2——忠实遵守"未实测前不假定主路径成立"，且不修改 build_env_check.md §14 空表 |
| S4 | 原始输出归档 | 同上 S1/S2 输出重定向至 `verify-*.txt` | 归档至 `docs/hmi/test_evidence/ITEM-002-render-path/verify-selftest-20261015.txt`、`verify-dryrun-20261015.txt` |

（包内执行注意事项，非工具问题：Windows Git Bash 下 `python3` shim 对 `analyze.py` 报错退出 49；
以 `python` 运行正常。板端脚本按 README 使用 `python3`，属目标板环境默认命名，不受影响。）

## 2. 核对当前交付（不只看包装文件）

- `app/spikes/render_path/README.md`：三条命令为其各自声明用途（板端运行/替代源/无板校验），未把主机校验表述为板端通过；状态为其声称状态。
- `docs/hmi/render_path_spike.md` §1–§7 与 `DESKTOP_APP_DESIGN` §4.3/§6 口径一致；§7 下游强制约束（未选定前保留双路径、默认不预设 gl、待实测后冻结）已逐字核对，该约束保护"不得在未实测前假定主路径成立"的工作项红线。
- `docs/hmi/build_env_check.md` §14.1/§14.3/§14.4 三处交叉读取一致：实测表全空、选定为 UNMEASURED、板端待授权命令已就绪；无"未实测但写为已选/已通过"的表述（即未回填伪造数据）。
- `analyze.py` 判据（playing/fps_source/fps_display/色彩/无 fatal error → gl→cpu→none→UNMEASURED）与 README §判据、render_path_spike.md §3 三处一致。
- 上游反馈闭环：TEST_DESIGN TC-STREAM-08 明示依赖"ITEM-001/002 结论"，spike 完成后该用例的 C/D 档才能跑——本缺口影响范围见 §4。

## 3. 判据逐条核对 + 实际观察

| 判据（来自当前调用） | 实际观察 | 判定 |
|---|---|---|
| 板端跑通 `mppvideodec→glupload→glcolorconvert→qmlglsink` 并显示 | 无执行记录；板端未连接、未 SSH、未访问相机；项目设备工具禁用（capability 环境声明） | **未满足** |
| 探测失败时实测回退路径 `RGA NV12→BGR + Qt 纹理上传` | 同上，无板端执行数据 | **未满足** |
| 记录 fps、CPU/GPU 占用、色彩正确性 | 实测表（build_env_check.md §14.1 / render_path_spike.md §5）无任何数字 | **未满足** |
| build_env_check.md **追加实测数据** | §14 表仍为空占位（仅注明"待板端填写"）；本轮未追加任何实测值 | **未满足** |
| 给出选定的主/回退路径 | 选定值 = UNMEASURED（未选定） | **未满足** |
| 实现侧工具具备（"产生测量工具与判据"前提） | S1–S4 独立复现全部通过（见 §1） | 满足 |
| 未实测前不假定主路径成立 | 交付物三处文档一致保持 UNMEASURED，且把该约束写入下游强制说明 | 满足 |

**未满足点复现条件**：任何具备板端授权的执行者按 §5 即可消除全部前 5 行缺口；除设备授权外无其他缺口
（源码、判据、入口、文档均已可执行）。

## 4. 依赖与交接（记录给 Runtime / 后续能力）

1. **设备授权缺口（根因）**：ENV-C/D 板端执行权被禁用（build_env_check.md §13 同类缺口；
   `TEST_DESIGN` ENV-B/C/D 亦标"待实现/待授权"）。属环境/授权事实，非 spike 源码缺陷。
   需 Runtime/授权方解锁 RK3588 访问后，由具备板上执行职责的能力按 §5 命令补测并回填数据。
2. **交接命令（已就绪、待授权后一次执行）**：
   ```sh
   cd app/spikes/render_path
   ./run_render_spike.sh --url 'rtsp://<user>:<pass>@<cam>/...' --duration 20 --snapshot
   # 或无相机时（ENV-C 替代源，仍能测两管协商/换算，色彩判据同样适用）
   ./run_render_spike.sh --source test --duration 10
   # 产物 gl/cpu/selection-<stamp>.json(+.png) 与 selection-<stamp>.md 自动生成，
   # 内容可直接追加 build_env_check.md §14.1 表
   ```
3. **下游受阻范围**：README §状态、render_path_spike.md §7、TC-STREAM-08（依赖 ITEM-001/002 结论）。
   在 selection 落定前，渲染实现必须保留双路径配置开关（实现者已写为强制约束，本验证确认其存在且语义正确）。
4. **NEXT 建议（供实现循环重试 / 授权后推进）**：
   - 若 Runtime 决定给本轮重试机会：重试的价值在于补板端实测，而非改 spike 源码——建议先解锁设备授权再重试，
     避免再次"无板可跑"。
   - 主机无 Qt/GStreamer/librga 工具链（独立复核未检出），spike 无法在编译器可见前进入主机编译档；
     编译与运行仍属目标板范围（与 render_path_spike.md §7.2 一致）。

## 5. 本验证生成的新文件

- `docs/hmi/test_evidence/ITEM-002-render-path/VERIFY-ITEM-002.md`（本文件）
- `docs/hmi/test_evidence/ITEM-002-render-path/verify-selftest-20261015.txt`（S2 原始输出）
- `docs/hmi/test_evidence/ITEM-002-render-path/verify-dryrun-20261015.txt`（S1 原始输出）

## 6. 结论

**TEST_FAIL** — 工作项 ITEM-002 的核心可观察点（board 端实测数据 + build_env_check.md 追加实测值与选定路径）
未产出；实现侧工具与判据独立复现通过、文档诚实未伪造数据。根因为 **ENV-C/D 设备授权缺失**（环境/授权缺口，
非 spike 源码缺陷），记录依赖与交接于 §3–§4，交 Runtime 作后续调度（解锁设备授权后执行板上实测）。
