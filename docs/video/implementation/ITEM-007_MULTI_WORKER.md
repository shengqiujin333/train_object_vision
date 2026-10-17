# 多路并发与单路失败隔离 — 实现交付（VideoSystemImplementation, ITEM-007）

- Artifact: VideoSystemImplementation（视频代码配置及执行入口）
- Capability: `video_systems_engineer.video_system_implementation`
- 工作项: 视频任务 ITEM-007「多路并发与单路失败隔离」
- 上游: `docs/video/VIDEO_SYSTEM_DESIGN.md`、`docs/interfaces/INTERFACE_CONTRACT.md` v1.0.0（IF-03/§5/§6）、`docs/video/VIDEO_TEST_DESIGN.md`（G4/TC-V-WORKER-01..02）
- 基线 commit: `9d9d0eb`（本次新增/修改文件见下；Runtime 负责交接提交）
- 日期: 2026-10-17

## 1. 交付内容

| 文件 | 状态 | sha256(16) | 说明 |
|---|---|---|---|
| `app/src/media/include/railhmi/media/multi_stream.h` | 重写 | `396c46c8dcc10c0f` | `ChannelSpec{start_failure}` + worker 化 `MultiStreamManager`（逐路 worker/快照/单路请求/索引） |
| `app/src/media/src/multi_stream.cpp` | 重写 | `64aa1180a4e39dd9` | 每路一个 `CameraWorker`；`start_all` 单路失败隔离；`stats/latest_frame/is_presentable/start_error` |
| `app/src/media/tools/multi_probe.cpp` | 重写 | `ecfff54585defe80` | `railhmi-multi-probe`：worker 化并发探针 + `--selftest` + `--fail-index` 启动失败隔离判定 |
| `app/tools/board_multi_worker_check.py` | 新增 | `37b6a7639465441c` | 板端执行入口（MediaCheck + selftest + 六路并发 cut/fail 运行） |

取代旧的顺序 `pump_all` 版 `MultiStreamManager`（仍是本仓库唯一多路管理器，供 `railhmi-multi-probe` 使用）；UI/其他模块未直接依赖其旧 API。

## 2. 契约与行为

1. **每路独立 worker**：`MultiStreamManager` 构造时为每个 `ChannelSpec` 建一个 `CameraWorker`（专用线程）；`pump` 只在各自 worker 线程执行，调用方（含 GUI）只读快照。
2. **单路启动失败隔离**：`start_all()` 逐路启动；某路 `start()` 失败或 `start_failure`（故障注入）只记录该路 `start_error` 并跳过，**不中止其它路**，返回成功启动数。
3. **槽位顺序稳定**：`ids_`/`workers_` 严格按构造顺序；失败/断流路仍占其槽位（`stats()` 返回 offline 快照），**不重排、不丢格、不重启其他路**。
4. **单路请求**：`request_cut/restart/freeze/resume(index)` 只作用于该 index 的 worker。
5. **线程安全快照**：`stats()`（逐路 `StreamStats`，顺序=构造顺序）、`latest_frame(index|camera_id)`、`is_presentable(index)`、`start_error(index)`、`started(index)`、`index_of(camera_id)`。
6. **presentable 语义**：沿用 ITEM-006 的 `CameraWorker::is_presentable()`（`may_present_as_live(now)` 且租约属于当前 generation）；断流路 `presentable=false` 且 `last_frame_ms` 冻结（可渲染占位 + 最近帧时间）。

## 3. 运行入口

```bash
app/build.sh MediaCheck
build-media/tools/railhmi-multi-probe --selftest
build-media/tools/railhmi-multi-probe --count 6 --source test --duration 8 \
    --cut-index 3 --cut-after 4 --fail-index 5 --step 500 --out m.log --json m.json
```

板端一键：
```bash
python app/tools/board_multi_worker_check.py --host <addr> --user <user> \
    --outdir docs/video/test_evidence/ITEM-007 --stamp <tag>
```

## 4. 板端验证结果（RK3588）

- `--selftest`：**6/6 PASS**（`manager_size / slot_order_config / index_of / start_all_isolates_single_failure / fail_channel_reported / slot_order_after_start`）。
- 六路并发（test 源，`fail-index=5` 模拟 CAM-06 启动失败，`cut-index=3` 在 t=4s 断流 CAM-04）：
  - `started=5`；CAM-06 `started=false error="FI: forced start failure"`；槽位 idx 0..5 = CAM-01..06 保持不变。
  - 断流前每拍 `grid cells=6 live=5 presentable=5`。
  - 断流后 `grid cells=6 live=4 presentable=4`：CAM-04 `state=reconnecting/reconnect=1 presentable=false last_frame_ms` 冻结于 354057673；其余非故障路继续 `presentable=true`。
- `type=acceptance before_ok=true slot_order_stable=true cut_not_presentable=true others_still_presentable=true cut_last_frame_frozen=true fail_isolated=true result=PASS`。
- 证据：`docs/video/test_evidence/ITEM-007/ITEM-007-ENV-C-20261017-r1.md` 与同目录原始日志/JSON。

## 5. 边界与移交

- 本项交付**多路 worker 管理器与单路失败/断流隔离**；自动重连控制器（退避到期重建）属 ITEM-014；端到端延迟/吞吐仪器属 ITEM-017。
- 故障注入 `start_failure` 为测试用标记（`ChannelSpec.start_failure`），不影响生产路径。
- 未改 core/wire 契约、SQLite DDL、既有 Qt 桥接签名；未新增配置字段。
- 备注：旧 `MultiStreamManager::pump_all` 已移除（worker 自行推进）；若有外部调用方按新 API 读取 `stats()`/`is_presentable()`/`latest_frame()`。
