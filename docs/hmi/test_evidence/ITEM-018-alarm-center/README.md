# ITEM-018 证据 — 报警中心页面

- Work item: `TL-778f77eb-1fde-496d-9677-7330e26c6476-tasks-ITEM-018`
- 依据: `docs/hmi/INTERACTION_DESIGN.md` v2 §5.2（列表字段/默认排序/操作/筛选/空态/录像缺失标注）、§7.4（事件字段）；`docs/hmi/VISUAL_DESIGN.md` v2 §8.2（级别徽标列 + 录像可用性列、空态“暂无符合条件报警”+[清除筛选]、确认后行描边转 border）；`docs/hmi/DESKTOP_APP_DESIGN.md` §9（ack 只改状态不删事件）、§10/PLY-008
- 源码: `app/src/core/include/railhmi/core/alarm_center.h`、`app/src/core/src/alarm_center.cpp`、`app/src/core/tools/alarm_probe.cpp`、`app/tests/core/test_alarm_center.cpp`、`app/src/ui/src/alarm_model.{h,cpp}`、`app/src/ui/qml/pages/P2AlarmCenter.qml`、`app/src/ui/qml/components/FilterChip.qml`、`app/src/ui/qml/main.qml`、`app/src/ui/src/main.cpp`、`app/src/ui/qml.qrc`、`app/src/ui/CMakeLists.txt`、`app/tools/board_build.py`

## 1. 交付内容

1. **`railhmi-core` 报警中心模型**（无 Qt/SQLite）：
   - 展示行 `AlarmRow` + `make_alarm_row(EventRow, 相机名, 录像可用性)`：级别/类型/时间/来源摄像头/描述/处理状态/录像可用性全部派生，附中文标签。
   - 录像可用性三态 `RecordAvailability{Available,Incomplete,Unavailable}` → “可回放/录像不完整/录像不可用”（PLY-008，不静默）。
   - 默认排序 `sort_alarms_default`：**级别降序 → 时间降序**（一级最先）。
   - 六维筛选 `AlarmFilter`（时间窗=日期/时间段、摄像头、类型、级别、确认状态）+ `query_alarms`；`alarm_list_empty` 空态判定。
   - `acknowledge`（单个/批量）：**只改 ack_state 不删事件**，返回变化计数与 id；`build_ack_log` 产出维护/操作日志条目（HLT-007）。
   - UTC 时间工具：`format_time_text`/`parse_date_utc_ms`/`day_start_utc_ms`（civil-from-days，不依赖本地时区）。
2. **UI 桥接** `RailhmiAlarmModel`（QObject）：`query(level,type,camera,ack,from,to)`、`options()`、`ack(ids)` 全部委托 core（**过滤/排序/确认单一真相**，QML 不重复实现）；`changed` 信号驱动刷新。
3. **QML** `P2AlarmCenter.qml`：筛选栏（级别/类型/摄像头/确认状态 chip + 日期文本 + [清除筛选]/[批量确认]）；列表行含级别徽标（颜色+形状字形+文字）、类型、时间、来源摄像头、描述、处理状态、录像可用性（不可用/不完整行内黄条标注）；[确认]/[查看录像]/[查看快照]；空态“暂无符合条件报警”+[清除筛选]。“查看录像”跳转历史回放。
4. **`railhmi-alarm-probe` CLI**（core，无 Qt）：样例事件演示排序/筛选/空态/批量确认/维护日志。

## 2. 实测（板端 RK3588 本机；TestRun）

`board_build.py --mode TestRun` → `TESTRUN_EXIT=0` `ALARM_EXIT=0`。核心单测 **512 passed / 0 failed**（含 ITEM-018 新增报警中心断言）：
```
[PASS] level/type derived / type label / ack label / record label available / snapshot available flag / time text date
[PASS] recording unavailable labeled (PLY-008)
[PASS] L1 first, time desc within level / then L2 / then L3
[PASS] filter by level L1 -> 2 / filter by camera / filter unacked -> 3 / filter by type / filter by time window
[PASS] combined filter with no match -> empty state / combined filter hit
[PASS] batch ack changed 3 / 3 newly acked + 1 pre-acked = 4 (events not deleted) / ack does not delete rows / already-acked not recounted
[PASS] log action / log target ids / log detail count / log user
[PASS] 4 cameras in options / 4 types / 3 levels sorted L1 first / date round trip / day start / bad date rejected
```

`railhmi-alarm-probe`（板端原文）：
```
type=list rows=4 order=level_desc,time_desc
row id=1 level=L1 person_on_rail 2026-10-16 00:00:05 cam=CAM-01(前方) ack=unacked record=available
row id=4 level=L1 wheel_derail   2026-10-16 00:00:01 cam=CAM-05(车底) ack=acked   record=available
row id=3 level=L2 pantograph_abnormal ... record=incomplete [录像不完整]
row id=2 level=L3 side_camera_device ... record=unavailable [录像不可用]
type=filter name=level_L1 count=2 / unacked count=3 / window count=2 / combo_empty count=0 empty=true
type=ack changed=3
type=maint-log action=ack_alarm target=1,2,3 user=maintainer detail=count=3
type=acceptance batch_ack_changed=3 events_preserved=true empty_state_ok=true maint_log=1,2,3 result=PASS
```

判据核对（用户可观察点）：
- 列表字段齐全（级别/类型/时间/来源摄像头/描述/处理状态/录像可用性）✅
- 默认排序级别降序→时间降序（一级在前，同级时间降序）✅
- 筛选组合（级别+确认状态等）命中/空态正确，空态 `empty=true` ✅
- 批量确认后各行 `ack_label=已确认` 即时更新，且**事件未被删除**（`events_preserved=true`），产出维护日志 `ack_alarm target=1,2,3` ✅
- 录像不可用/不完整行内标注 ✅

## 3. 边界

- **持久化事件接入未接线**：`RailhmiAlarmModel` 已提供 `seedEvents(events, names, record)` 接口消费 `EventRow`（ITEM-007 `events` 表）与维护日志写入（`SqliteRepository`）；应用进程尚未打开数据库，因此默认列表为空（显示空态），仅在 `app.simulation=true` 时灌入样例用于页面自检。真实事件→数据库→报警中心接线归后续集成项。
- **QML 运行期未执行**：板端缺 Qt5 Quick/Qml 开发包（E-01b）；本项以 core 板端实跑（512/0）+ 探针 PASS + QML 源码/静态扫描 PASS 为证据。
- “查看录像”跳转历史回放页（P3）；回放页本体属后续工作项。

## 4. 证据文件

- `build-TestRun-...-ITEM018-r1.core-evidence_core-tests-...-r1.log` / `.xml`（512 passed / 0 failed）
- `...-ITEM018-r1.core-evidence_alarm-probe-...-r1.log`（排序/筛选/空态/批量确认/维护日志原文）
- `...-ITEM018-r1.transcript.json` / `.log`（构建 + 单测 + 探针 + 取回）
- 回归留痕：`ai-probe` / `status-probe` / `sim-inject`

## 5. 复现

```sh
# 板端
bash app/build.sh CoreTests && ./build-core/tests/railhmi-core-tests
./build-core/tools/railhmi-alarm-probe
# 经登记 SSH 入口
python app/tools/board_build.py --host <addr> --user <user> \
  --credential-env TRAIN_RK3588_SSH_CREDENTIAL --mode TestRun \
  --outdir docs/hmi/test_evidence/ITEM-018-alarm-center --stamp <tag>
```
