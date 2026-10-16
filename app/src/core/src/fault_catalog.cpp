#include "railhmi/core/fault_catalog.h"

namespace railhmi {
namespace core {

std::vector<FaultPrompt> fault_catalog() {
    std::vector<FaultPrompt> out;
    out.push_back({FaultKind::StreamCut, fault_id(FaultKind::StreamCut), "断流",
                   "画面占位：重连中（显示已重连次数与最近帧时间），旧帧不作为实时",
                   "等待自动重连（退避）或维护页手动重连；达阈值升级为设备报警"});
    out.push_back({FaultKind::Freeze, fault_id(FaultKind::Freeze), "画面冻结",
                   "画面冻结：叠加占位层，最近帧时间停止推进",
                   "确认摄像头/网络后重连；恢复出帧自动转在线"});
    out.push_back({FaultKind::AiExit, fault_id(FaultKind::AiExit), "AI 退出",
                   "状态区 AI 项转“AI 不可用”，相关检测类报警标注“AI 不可用”（不静默）",
                   "重启推理线程/服务；恢复后 AI 项转“运行”，前方 AI READY"});
    out.push_back({FaultKind::DiskLow, fault_id(FaultKind::DiskLow), "磁盘不足",
                   "存储项提前告警；录像写入标注容量不足",
                   "按保留策略淘汰普通片段（保护片段优先）；扩容或清理后恢复"});
    out.push_back({FaultKind::PtzOutOfTolerance, fault_id(FaultKind::PtzOutOfTolerance), "云台超差",
                   "云台卡显示目标值 vs 实际值 + 容差校验失败（超差），前方 AI 进入 NOT_READY",
                   "一键归位 / 重调前方预置位（二次确认）后复校；成功转 READY"});
    out.push_back({FaultKind::RecordingInterruption, fault_id(FaultKind::RecordingInterruption), "录像中断",
                   "时间轴出现录像缺口标记，事件标注“录像不完整/不可用”（不静默）",
                   "恢复录像进程/存储；缺口记录保留可检索"});
    return out;
}

std::vector<SelfCheckStep> self_check_list() {
    std::vector<SelfCheckStep> out;
    out.push_back({"SC-01", "railhmi-sim-inject --fi 01（断流）",
                   "该路画面占位 + 重连中 + 重连次数/最近帧时间", "自动重连或手动重连；恢复出帧转在线"});
    out.push_back({"SC-02", "railhmi-sim-inject --fi 02（冻结）",
                   "画面冻结占位，最近帧时间停更", "重连恢复；出帧自动转在线"});
    out.push_back({"SC-03", "railhmi-sim-inject --fi 03（AI 退出）",
                   "状态区 AI 项“AI 不可用”，关联报警标注", "重启推理；恢复后 AI 运行/READY"});
    out.push_back({"SC-04", "railhmi-sim-inject --fi 04（磁盘不足）",
                   "存储项告警 + 录像容量不足标注", "保留淘汰/清理后恢复"});
    out.push_back({"SC-05", "railhmi-sim-inject --fi 05（云台超差）",
                   "云台卡超差 + 前方 AI NOT_READY", "归位/预置位二次确认后复校"});
    out.push_back({"SC-10", "railhmi-sim-inject --fi 10（录像中断）",
                   "时间轴缺口标记 + 事件“录像不可用”", "恢复录像进程/存储"});
    out.push_back({"SC-BOOT", "systemctl restart railhmi（或断电重启）",
                   "显示启动恢复进度（8 步），完成后进入可用状态", "systemd Restart=always 自动拉起；失败查日志"});
    return out;
}

}  // namespace core
}  // namespace railhmi
