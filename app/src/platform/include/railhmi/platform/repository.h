#pragma once

// railhmi-platform: SQLite 仓储（ITEM-007）。
// 在 railhmi-core 的数据模型/schema 之上提供 9 张表的写入/查询、保留策略执行与缺口查询。
// 仅在 RAILHMI_HAVE_SQLITE 时编译。

#ifdef RAILHMI_HAVE_SQLITE

#include <string>
#include <vector>

#include "railhmi/core/persistence_model.h"
#include "railhmi/core/retention.h"
#include "railhmi/platform/sqlite_store.h"

namespace railhmi {
namespace platform {

class SqliteRepository {
public:
    bool open(const std::string& path, std::string* err = nullptr);
    void close();
    bool is_open() const { return store_.is_open(); }
    bool init_schema(std::string* err = nullptr);
    std::string journal_mode(std::string* err = nullptr);

    // 写入：返回新行 id（失败 -1）。事件/片段/缺口/流事件使用 rowid。
    long long insert_event(const core::EventRow& e, std::string* err = nullptr);
    bool insert_event_target(const core::EventTargetRow& t, std::string* err = nullptr);
    bool insert_maintenance(const core::MaintenanceLogRow& m, std::string* err = nullptr);
    bool insert_health(const core::DeviceHealthRow& h, std::string* err = nullptr);
    long long insert_segment(const core::RecordingSegmentRow& s, std::string* err = nullptr);
    long long insert_gap(const core::RecordingGapRow& g, std::string* err = nullptr);
    long long insert_stream_event(const core::StreamEventRow& s, std::string* err = nullptr);
    bool upsert_integration(const core::IntegrationRow& i, std::string* err = nullptr);
    bool set_config(const core::ConfigKvRow& c, std::string* err = nullptr);
    std::string get_config(const std::string& key, std::string* err = nullptr);

    long long count(const std::string& table, std::string* err = nullptr);

    // 读取
    core::EventRow get_event(long long id, bool* found = nullptr, std::string* err = nullptr);
    std::vector<core::RecordingSegmentRow> query_segments(std::string* err = nullptr);
    std::vector<core::RecordingGapRow> query_gaps(const std::string& cam, std::string* err = nullptr);
    std::vector<core::StreamEventRow> query_stream_events(std::string* err = nullptr);

    // 保留策略：按 core::apply_retention 计算并删除被淘汰片段；返回淘汰结果。
    core::RetentionResult apply_retention(const core::RetentionPolicy& policy, long long now_ms,
                                          std::string* err = nullptr);

private:
    SqliteStore store_;
};

}  // namespace platform
}  // namespace railhmi

#endif  // RAILHMI_HAVE_SQLITE
