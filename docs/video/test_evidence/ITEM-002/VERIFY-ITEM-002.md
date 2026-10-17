# ITEM-002 单路取流管线工厂 — 独立软件验证 TestEvidence（VERIFY-20261017-v1）

- Artifact: TestEvidence（software_verification 独立复验）
- Capability: `software_tester.software_verification`
- 被验证实现: video_systems_engineer.video_system_implementation（交付文档 `docs/video/implementation/ITEM-002_PIPELINE_FACTORY.md`，实现提交 11065f1；交付执行证据 `ITEM-002-ENV-B-20261017-r1.md`，板端 stamp 20261017-item002-r6）
- 被测版本核对: 板端构建使用本轮独立上传的 `app/` 工作区副本（179 文件，其中 1 件为本轮清单维护新增行，源文件集与 commit 11065f1 交付一致）；板端产物 `build-media/tools/railhmi-stream-probe`（独立 MediaCheck 构建）
- 目标: compute_host 192.168.0.103（RK3588 / Ubuntu 22.04.5 / GStreamer 1.20.3 / librga / aarch64）
- host key fingerprint: `SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU`（与交付 r6 一致）
- 验证日期: 2026-10-17; stamp `20261017-item002-verify-v1`
- 验证范围: ITEM-002 工作项（单路取流管线工厂）与测试设计 G2/TC-V-PIPE-01..02 判据；**不外推**多路并发容量、端到端延迟、RGA 转换、录像回放（ITEM-005/006/007/011+）；file 源为非实时解码路径源，其 fps 不作实时/容量基准（与交付范围声明一致）

## 1. 验证方法与独立复现入口

独立复跑全部经交付运行入口 `app/tools/board_pipeline_check.py` 与验证侧自有扩展夹具：

```
python docs/video/test_evidence/ITEM-002/_verify_run.py --host 192.168.0.103 --user admin \
    --outdir docs/video/test_evidence/ITEM-002 --stamp 20261017-item002-verify-v1 --keep-remote
python docs/video/test_evidence/ITEM-002/_verify_compare.py   # impl r6 vs verify v1 结构一致性
python docs/video/test_evidence/ITEM-002/_verify_ext.py       # EXT-A..I 板端扩展判据（12 项）
python docs/video/test_evidence/ITEM-002/_verify_leakscan.py  # EXT-I 精化判据本地独立复检
```

凭据经 PowerShell 用户级环境变量导入本进程（工具链 `_verify_run.py`/`_verify_ext.py` 内置），不落盘、不打值；板端凭据经 0600 临时 env 文件机制（交付机制一致，本轮 ext 夹具独立复现），用后即删并确认。

## 2. 结果总览：TEST_PASS（附观察项 O-1）

| 判据 | 结果 | 证据 |
|---|---|---|
| 独立板端构建 MediaCheck | PASS（BUILD_EXIT=0；GStreamer 1.20.3 / librga yes） | transcript `build` 步 |
| 交付 16 项 selftest 独立复现 | PASS（16/16 PASS，逐行与交付 r6 完全一致） | transcript `selftest` 步 |
| 三类源（test/file/rtsp-auto/rtsp-h265）独立板端出帧 | PASS（4×final_state=live，全部 reconnect_count=0） | verify-v1 probe 四件 log/json |
| 三类源同管线工厂结构一致性 vs 实现 r6 | PASS（4/4 aligned_structure_identical=True，逐行 state/presentable/reconnect/summary 全同，双会话 time/帧值归一） | `_verify_compare.py` 输出 |
| file 源 EOS 循环（2s 素材连续出帧 5s） | PASS（t≥4000ms 槽位仍有帧） | EXT-B |
| FI-01 断流注入（cut-after 1s） | PASS（cut=FI-01 行 + cut 后 12/12 槽位 presentable=false + 错误行含 simulated stream cut (FI-01) + 无管线原文） | EXT-C |
| 参数错误退出码语义（--decode 非法/未知参数 exit=2） | PASS | EXT-D/E |
| 假凭据运行期错误脱敏（127.0.0.1 不可达） | PASS（错误行非空、无凭据值/user-pw/拼接 URL） | EXT-G |
| 真实相机错误凭据（FI-V-04 域） | PASS（明确错误行、错/真密码 0 泄漏、0600 env 用后即删确认） | EXT-H |
| 板端 pipeline/bus 原文不进输出 | PASS（EXT-C/F 所有探测全文无 `protocols=`/`user-pw` 形态） | EXT-C/F |
| 证据目录 26 文件凭据语义零泄漏 | PASS（26 文件扫描、cred-semantics 0 命中；SSH 字面量仅以用户名 token 形态合法出现） | EXT-I + `_verify_leakscan.py` |

## 3. 独立复跑三类源逐秒观测（verify-v1）

| 源 | 首样本 | 收敛 fps | final_state | reconnect |
|---|---|---|---|---|
| test | t=0 即 live | 25.5 | live | 0 |
| file | t=0 即 live | 636.5（非实时循环） | live | 0 |
| rtsp auto（decodebin） | connecting→live（t≈600ms 首帧） | 25.0~25.5 | live | 0 |
| rtsp h265（固化 mppvideodec） | t=0 即 live | 25.0~25.5 | live | 0 |

rtsp auto 与显式 h265 两路径同时在独立复跑中出帧，同交付 r6 结论：decodebin 自适应路径与固化路径均可读 CAM-01 实流。

## 4. 与实现者归档（r6）的独立一致性比对

`_verify_compare.py`：r6 与 verify-v1 是独立两轮板端会话（duration 6s/5s 两个会话参数差异归一）。对全部 4 个源的逐秒状态清洗后逐行比对：`test/file/rtsp-auto/rtsp-h265` 四路 **aligned_structure_identical=True（4/4）**——injection 行结构、state 序列、presentable 位、reconnect_count、summary 结构完全一致。交付 selftest 16 行与 deliverable 逐行同。

**验证观察（正确性面）**：rtsp-auto 的首样本 state=connecting（此时未出帧）→ 600ms 内 live，与 `stream_state.cpp` `first_frame_timeout_ms=5000` 一致（首帧不可达时将上升趋势指向 reconnect）；file/test 无此类差额段。本项也证实了交付调试记录里的「audio pad 进入解码链」问题已由 RTP 层 `application/x-rtp,media=video` 过滤消除——两路径在本轮独立复跑均无单路瞬时失败（ITEM-001 VERIFY O-1 类别在本工厂层未再复现）。

## 5. 验证侧扩展夹具（自有判据，交付 selftest 未覆盖的边界）

| id | 刺激（判据先独立构造，再取实测） | 实测 | 结果 |
|---|---|---|---|
| EXT-A | 三类源行为级同工厂: 4 份 verify-v1 probe 日志独立解析 — ≥3 live 槽位有帧 + 末槽 live + summary final_state=live | 4/4 live | PASS |
| EXT-B | file EOS 循环: 2s 素材 5s 运行，t≥4000ms 槽位 fps>0 | 2 槽 | PASS |
| EXT-C | FI-01 cut: cut=FI-01 injection 行；cut 后 12/12 槽位 presentable=false；错误行含 simulated stream cut (FI-01)；final_state∈{connecting,reconnecting,frozen,offline}（默认阈值 6s 内不会升级 offline）；无管线原文 | exit=0 全判据真 | PASS |
| EXT-D | `--decode zzzz` → exit=2 + "bad --decode" | exit=2 | PASS |
| EXT-E | `--nope` → exit=2 + "unknown arg" | exit=2 | PASS |
| EXT-F | 空 rtsp URL: 解析期不报错（gst_parse_launch 可构造），运行期 bus error → error 行 + 无管线原文 + 正常退出 | exit=0，error 行 6 | PASS |
| EXT-G | 假凭据 127.0.0.1 不可达 rtsp: 运行期 error 行非空 + DUMMY/user-pw/拼接 URL 全文 0 泄漏 | err_lines=7，no_leak | PASS |
| EXT-H | 真实相机 0600 env 注入错误密码（FI-V-04 域）: t= error 行非空 + 错/真密码 0 泄漏 + user-pw 形态 0 出现 + env 文件删除确认 | err_lines=9，env_removed | PASS |
| EXT-I | 目录 26 文件凭据语义形态（user-pw/password=value/拼接 URL）对三组真实凭据 0 命中；SSH 字面量仅以用户名 token 形态出现（非凭据语义） | 0 命中 | PASS |

夹具调试过程（透明记录，均为验证侧期望/实现构造错误，非产品缺陷）：

- v1 过程轮 EXT-C final_state 期望缺 `connecting`——`simulate_cut` 断流后状态走的是「源错误 → reconnecting →(backoff)→ connecting →(5s timeout)→ recon #2」循环，默认阈值下 6 秒运行尚未升级 offline；按 `stream_state.cpp` tick 语义构造期望修正。同轮 `EXIT=` 解析当外层 shell 码（`echo EXIT=$?` 外层恒 0）导致 exit=2 判据误报假 FAIL——退出码必须从 EXIT token 内部解析。
- v1 过程轮 EXT-F 期望「空 URL → exit=3 启动失败」落在解析期约定上——实测 `gst_parse_launch` 对空 location 是**运行期 bus error**（非解析期）；判据按「运行期 error 行存在 + 无管线原文 + 正常退出」修正。
- v1 过程轮 EXT-I 文件级字符串同现把非机密用户名 token（`--user admin`）误判为泄漏——精化为**凭据语义形态**（attr 赋值/密码场景/拼接 URL）时值与语义形态才计命中；SSH 字面量 = 用户名 token 合法出现单列确认，是验证侧判据过宽修正，不是产品缺陷。

## 6. TC-V-PIPE-01/02 判据映射

| TC | 判据 | 本轮证据 |
|---|---|---|
| TC-V-PIPE-01 | rtsp/file/test 同管线工厂；rtsp 含 `protocols=udp` 无 `protocols=tcp`（无 TCP 回退路径）；URL 参数来源 config（rtsp_url 未含凭据→工厂拆分/显式属性） | PASS：selftest 16/16 中 `test_videotestsrc/file_filesrc/rtsp_udp/rtsp_no_tcp/rtsp_user_prop/rtsp_pw_prop/rtsp_no_creds_in_url` 独立复现；行为级三类源独立板端出帧；生成本地分析确认同状态机字段全同（§4） |
| TC-V-PIPE-02 | 显式 h265/h264 固化路径: `rtph26xdepay ! h26xparse ! mppvideodec`, 不插 decodebin | PASS：selftest `rtsp_explicit_h265/rtsp_explicit_no_decodebin/rtsp_explicit_h264` 独立复现 **+ 显式 h265 路径在真实相机上独立实测出帧**（对自检只做字符串核对的升级面） |

## 7. 观察项（不阻塞 TEST_PASS，交实现/参考）

- **O-1（rtsp-auto 首样本态时序）**：独立复跑 rtsp-auto 首样本为 connecting（首帧 t≈600ms 才到），但 h265 固化路径首样本即在 live——auto 路径在 decodebin 类型探测上有可测的额外首帧时延。属两路径差异行为学观察，不判缺陷（设计允许 decodebin auto 路径协商时延）；ITEM-006+ 多路并发与延迟仪器期可再量化。本轮不作为缺陷上报。
- **O-2（file 源非实时性声明与 web 一致）**：verify-v1 file 源 ~636 fps 与实现者在 r6 同一量级（非实时、循环播放、`sync=false`）；交付文档和执行证据均明确声明 file 源不作实时/容量基准——本项**不判缺陷**，此观察用于防下游 ITEM-017 预算仪器（file 源 fps 数据）误用。
- **O-3（板端 eth1 link speed）**：同 ITEM-001 O-3，平台/网络职责，不在本项验收内，不重复计缺陷。

## 8. 结论

**TEST_PASS**：ITEM-002（单路取流管线工厂）在当前实现版本（工作项 ITEM-002 交付，基线 11065f1 板端独立复现栈）下，TC-V-PIPE-01/02 软件侧判据全部通过，独立板端复现三类源同一工厂出帧 + rtsp 强制 UDP/认证属性/RTP 视频过滤/显式固化解码路径可出帧 + pipeline/bus 原文与凭据全程不泄漏。**本验证只证明 ITEM-002 单路管线工厂**；多路并发、端到端延迟、RGA 转换正确性、录像回放等外延不外推（后续 ITEM 范围）。

## 9. 原始证据索引（本目录）

| 文件 | 说明 |
|---|---|
| `pipe-192_168_0_103-20261017-item002-verify-v1.log` | 验证轮独立复跑全流程人类可读 transcript（build/selftest/gen-file/test/file/rtsp auto+h265/下载） |
| `pipe-192_168_0_103-20261017-item002-verify-v1.transcript.json` | 同上结构化（命令+exit+输出） |
| `pipe-…-verify-v1.core-evidence_probe-{test,file,rtsp-auto,rtsp-h265}-….{log,json}` | 四源独立板端逐秒采样 |
| `video-cap-ext-192_168_0_103-20261017-item002-verify-v1.json` | 验证侧 ext 夹具结构化结果（EXT-A..I 12 项 + 判据明细） |
| `_verify_run.py` | 验证轮包装入口（用户级凭据导入本进程后运行交付 runner） |
| `_verify_compare.py` | impl r6 vs verify-v1 四源结构一致性比对（time 归一口径可复核） |
| `_verify_ext.py` | 验证侧可复现扩展夹具（板端操作全部有限超时，0600 凭据文件用后即删确认，EXT tmp 清理确认） |
| `_verify_leakscan.py` | EXT-I 精化判据本地独立复检（纯本地，无板端） |

## 10. 资源清理

- 板端 `/tmp/railhmi_ext002`（EXT 夹具临时目录）: 已 `rm -rf` 并确认 `No such file or directory`。
- 板端 `/tmp/railhmi_pipe`（verify 轮构建树, --keep-remote 保留后）: 本轮取证完成后已 `rm -rf`，确认 `board_tree_cleaned`。
- 板端 `/.cam_env*.sh`(0600) 凭据文件: runner 轮 `cleanup-envfile removed` + EXT-H `extenv_removed` 双重确认删除。
- 本机临时无遗留进程或后台任务（本会话的全部后台 runner/ext 任务均 completed）。
