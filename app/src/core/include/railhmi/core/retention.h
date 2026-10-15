#pragma once

// railhmi-core: 录像保留策略与缺口模型（ITEM-007）。
// 纯逻辑（不依赖 SQLite）：给定片段列表与容量/期限策略，计算淘汰顺序；
// 保护片段（critical）优先保留，普通片段（continuous）先按期限、再按容量最旧优先淘汰，
// 仅在仍超容量时才淘汰保护片段（最旧优先）。缺口按相机合并重叠/相邻区间。

#include <string>
#include <vector>

#include "railhmi/core/persistence_model.h"

namespace railhmi {
namespace core {

struct RetentionPolicy {
    int retention_days = 7;
    double max_gb = 20.0;
};

struct RetentionResult {
    std::vector<long long> evicted_ids;   // 淘汰顺序（先普通最旧，后保护最旧）
    long long evicted_bytes = 0;
    long long retained_bytes = 0;
    int evicted_continuous = 0;
    int evicted_critical = 0;
};

// 计算应淘汰的片段 id 列表（不修改输入）。now_ms 为当前 UTC 毫秒。
RetentionResult apply_retention(const std::vector<RecordingSegmentRow>& segments,
                                long long now_ms, const RetentionPolicy& policy);

// 追加缺口，若与同相机已有缺口重叠或相邻（闭合区间，相差≤1ms）则合并。
// 合并会吸收**所有**可合并区间（含被新缺口桥接的多个区间），reason 取最先存在的非空值；
// 结果保持按 (cam, t0) 排序。
void add_gap(std::vector<RecordingGapRow>& gaps, const std::string& cam,
             long long t0_ms, long long t1_ms, const std::string& reason);

}  // namespace core
}  // namespace railhmi
