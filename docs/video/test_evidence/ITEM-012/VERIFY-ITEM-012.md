# ITEM-012 保护片段固化与缺口记录 — 独立验证证据（TestEvidence, VERIFY-20261017-v1）

- Artifact: TestEvidence（软件验证轮独立复跑与扩展夹具证据）
- Capability: `software_tester.software_verification`
- 工作项: 视频 TASK ITEM-012（保护片段固化与缺口记录）+ 更新 ITEM-012_CLIP_SOLIDIFY.md
- 被测实现版本: commit `18d50a0`（实现 stamp `20261017-item012-r5`；基线 `86ae2bc`）
- 日期: 2026-10-17
- 目标: compute_host `192.168.0.103`（RK3588 / aarch64 / Ubuntu 22.04 / GStreamer 1.20.3）
- host key fingerprint: `SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU`（与实现轮一致）
- 验证入口: `app/tools/board_clip_solidify_check.py`（deliverable runner；`--keep-remote` 独立复跑）+ `_verify_ext.py`（自有板端扩展夹具）

## 1. 覆盖范围与版本核对

- 实现交付共 11 个文件（`docs/video/implementation/ITEM-012_CLIP_SOLIDIFY.md` §1），sha256(16) 与 working tree 一致（本轮 git 状态仅新增验证侧文件，未触碰产品文件）。
- 上游阅读：`docs/interfaces/INTERFACE_CONTRACT.md` §7/§8（IF-06/IF-07 ClipRequest/缺口/三态/写盘顺序）、`docs/video/VIDEO_SYSTEM_DESIGN.md` §7.1、`docs/video/VIDEO_TEST_DESIGN.md` TC-V-CLIP-01/02 与 §5 冒烟 TC-V-SMOKE-05、`app/config/thresholds.yaml`（clip_pre=30/clip_post=60）。
- 结论范围：**幂等 request_id、关键帧对齐 actual_t0/actual_t1、真实可解析媒体、原子发布顺序、缺口记录、可用性三态**。真实相机 RTSP 固化（ENV-D）、provider 查询/回放（ITEM-013）、SQLite 事务（平台层）不在本项边界（实现证据已如实声明，本验证同口径）。

## 2. 独立复跑（不采信实现 r5 归档）

驱动：`_verify_run.py`（PowerShell 用户级凭据导入 → 同进程交付 runner，全部步骤有限超时）。

| 步骤 | 结果 |
|---|---|
| build-core → `railhmi-core-tests` | **802 passed / 0 failed**（含 clip_request 组 19 例） |
| build-media | exit=0 |
| `railhmi-clip-solidify --selftest` | 8/8 PASS |
| `railhmi-recording-tap --selftest`（ITEM-011 回归） | 9/9 PASS |
| run-clip（板端真实压缩管线三态+幂等+失败路径） | acceptance `result=PASS`（8 旗全 true） |
| 下载日志/JSON/真实 .ts | 3 件成功 |

core-tests PASS 行 802 逐行与实现 r5 归档**逐行 identical**（列表级 equality，负责人 802/802）；JUnit 无 FAIL。

## 3. 独立语义比对（`_verify_compare.py`，7 checks 全 PASS）

不采信交付 acceptance 行，独立重推导两轮（impl r5 vs verify-v1）：

| 判据 | 结果 |
|---|---|
| semantics_impl-r5 / semantics_verify-v1 | PASS（config 回显/my 幂等链/三态绑定键/覆盖数学（容差 1000ms）/缺口区间=[actual_t1,req_t1)/buffer 界/JSON-日志交叉/complete 行 reason 空） |
| stable_config / stable_idem / stable_acc | PASS（跨轮逐字段恒等：config 30/60、eff 3/2、buffer 7s；idem added/duplicate/conflict；acceptance 八旗） |
| packets_window | PASS（complete 案例包数 impl=125 vs verify=125，|Δ|=0） |
| size_window | PASS（size 1332168 vs 1327656，|Δ%|=0.34% ≤ 15% x264enc 码率自然波动窗——逐字节差来自编码器端口决定论，不属确定性判据域） |

真实媒体独立核对（两侧）：`.ts` 文件头同步字节 `47 40 00 31`、size % 188 == 0、全包 0x47 同步（7086/7086 与 7062/7062 全同步）；分配器格式经交付 `tsdemux → EOS` 校验链路证明可解码；非 `.json` 清单、非空文件。

## 4. 扩展夹具（`_verify_ext.py`，13/13 PASS）

| EX | 内容 |
|---|---|
| EX-OFFLINE/dryrun | 归档离线 selfcheck（impl r5 log/json/resume/transcript 全解析）：parse regex、逐键核对、802passed 行、transcript 双 selftest OK 与 cleanup 步——上板前已清算三遍预期构造 |
| EX-A | 独立参数轮（pre=2/post=3/warmup=8/extra=3，buffer 7s）：config 回显逐字段、幂等链、4 case 行 kv 全键集闭包（complete 9 键/incomplete 6 键/unavailable 4 键/mux_failed 3 键）、按容差独立重算 S1 期望可用性（本参数轮与标注一致 incomplete+PRE_ROLL_SHORTAGE 缺口如实标注）、S2 缺口端别 token 由 [actual_t1,req_t1) 容差数学独立重算、buffer span ≤ 7s+100ms、media/publish 不变量（有文件+published=true+无 .part 残留） |
| EX-B | CLI 负例 3 例：`--bogus-flag` exit=2+unknown arg 行、`--help` exit=0 含 --selftest/--outdir、`--post-seconds abc`（atoi 容错文件级语义回退 0，运行不崩）exit=0 |
| EX-C | 板端 g++ 直编自有夹具 `_verify_vext.cpp`（链接交付 clip_writer.cpp + clip_request.cpp + file_store.cpp + pkg-config gstreamer×2）35/35 PASS：clip_path 形状+request_id sanitize（`/`→`_`）+尾斜杠归一、窗口算术（-30/+60→[t-30s,t+60s]）+duration、注册表 Added/Duplicate/Conflict+size==1（repeat 不重插/冲突不覆盖/find 返回原作业指针）、camera/trigger 差异→Conflict、record_unavailable 缺口字段（cam/t0/t1=请求窗全跨度+reason token）、无媒体 solidify 情景 4 判例（unavailable/no_media_gap/无 path 无 size/**无目录副作用——不以空目录或清单冒充录制**）、可用性三态 parse/roundtrip/reject bogus、容器缺省/命名/扩展名、容差/封装超时/校验开启缺省。 |
| EX-D | 同参两轮（a1/a2）独立语义重推导 holds + config/idem/acceptance 稳定行恒等 + exit=0（size 自然波动 0.34%/0.45% 非判据域） |
| EX-E | 凭据泄漏扫描（本目录全部 .py/.md/.json/.log/.cpp/.ts 文件，凭据语义形态正则配 SSH secret 同现 0 命中） |
| CLEAN | 板端 `/tmp/railhmi_clip`（树/ext 构件/vexta 产物）+ `/tmp/vext_clips*` 全清确认（`BOARD_CLEAN_DONE` + 二次 ls 确认 `No such file or directory`） |

## 5. 过程轮与教训（验证侧自负）

- v1 过程轮三处夹具构造错全部自查后修正，无产品缺陷误报：① EX-A 首选参数 warmup≠pre+周转使请求 t0 落到 7s 缓冲窗之外 → PRE_ROLL_SHORTAGE 是**交付正确语义**（落地实际起点від关键帧对齐）而非缺陷——改容差重推导借 EX-A 参数自由；② no-args 是合法默认运行（探针契约）非负例，改 `--post-seconds abc` 容错面；③ C++ 夹具首版缺 `ClipPublisher` using 与 platform 头（g++ 直报），已补。`--config`/`--outdir` 表链格式在 selftest/dryrun 上板前零成本抓出。
- 逐轮 teaching（验证侧认知）: EX-A oracle 最终形态是**参数无关的按容差重推导**（交付标注的可用性/缺口端别由 [t0,t1)×tol 独立推出并要求一致），在 mounted-in 探针与 impl r5 归档两侧都成立；probe 参数改变（30/60→2/3）与合成源 buffer 滚动可平移三态标签，不改语义。

## 6. 结论

**TEST_PASS**。本项被测版本（commit 18d50a0，stamp 20261017-item012-r5）在独立复跑与扩展夹具下满足任务书全部用户可观察判据：

1. 触发后产生可定位**真实 MPEG-TS 保护片段**，时长/覆盖与配置符合（容差内、缺口端别如实）。
2. 相同 `request_id` 重复请求返回同一作业（duplicate），不同参数 **CONFLICT**（不覆盖已登记作业）。
3. 中断/失败处写缺口（`NO_MEDIA`/`MKDIR_FAILED`/`POST_ROLL_SHORTAGE`/`PRE_ROLL_SHORTAGE`/`PRE_POST_ROLL_SHORTAGE` 实测在案，两侧轮一致），事件录像可用性**如实三态**（complete/incomplete/unavailable）。
4. 原子发布顺序（fsync→rename→fsync 目录→发布回调）；无 `.part` 残留；无 `.json`/空文件冒充录像。
5. 显式边界（与实现一致，不外推）：真实相机 ENV-D、provider/回放（ITEM-013）、SQLite 发布侧（平台）未在本项覆盖；ITEM-011 回归面（recording-tap selftest）验证保持全绿。

## 7. 原始证据文件（本目录）

| 文件 | 内容 |
|---|---|
| `clip-192_168_0_103-20261017-item012-verify-v1.log` | 验证轮板端独立复跑全 transcript（build-core/core-tests/build-media/selftest/recording-tap-selftest/run-clip/downloads） |
| `clip-192_168_0_103-20261017-item012-verify-v1.transcript.json` | 结构化 transcript（指纹+每步 exit） |
| `clip-…verify-v1.core-evidence_clip-solidify-…verify-v1.log` | fresh 配置/缓冲/幂等/三态固化/acceptance 逐行 |
| `clip-…verify-v1.core-evidence_clip-solidify-…verify-v1.json` | fresh 片段三态汇总 JSON |
| `clip-…verify-v1.core-evidence_clips_CAM-01_evt-100-CAM-01.ts` | fresh 真实 MPEG-TS 保护片段（1327656 B，只读归档，未经转码） |
| `_verify_compare.py` / `_verify_compare_result.json` | 独立比对器与结构化结果（7 checks PASS） |
| `_verify_run.py` | 验证侧独立复跑驱动 |
| `_verify_ext.py` / `_verify_vext.cpp` | 自有扩展夹具（板端执行）与 C++ 源（35 判例） |
| `clip-ext-192_168_0_103-20261017-item012-verify-v1.json` | EXT 结构化结果（13/13 PASS） |
