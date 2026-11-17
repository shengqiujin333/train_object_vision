#pragma once

// 门磁 BLE 广播的 HCI 层直读通道。
//
// 现场实证（11-16 深测修正）：
//  1) BlueZ 的 Adapter1.SetDiscoveryFilter({DuplicateData:true}) 只影响 BlueZ
//     内部对相同数据的合并，**不会关闭控制器去重**；btmon 抓到
//     "Filter duplicates: Enabled" 时，1s 一次的广播只剩 2~25s 一次，
//     厂商帧计数器跳号（丢包）。
//  2) 更关键的是：bluetoothd 在 discovery 期间约每 10s 会重新下发
//     `LE Set Scan Enable` 并把 Filter duplicates 重新打开；而一旦有第三方
//     下发裸 HCI 命令，BlueZ 的 mgmt 发现状态会卡在 InProgress /
//     Discovering=False / Device1 对象为 0。也就是说「BlueZ discovery +
//     裸 HCI 关去重」两条路径互斥，不能并存。
//  3) 因此生产路径改为：门磁独占 HCI 原始通道，自己下发 LE Set Scan Enable
//     (filter_dup=0) 并直接解析 LE Advertising Report，完全不依赖 BlueZ
//     Device1 对象、PropertiesChanged 推送节流和 GetManagedObjects 缓存。
//     实测 35s 收到 13 个 T1 广播，间隔 1.0~6.0s，<=5s 占 10/12。
//
// 需要 CAP_NET_ADMIN + CAP_NET_RAW（或 root）。无权限时返回负值，调用方必须
// 降级到 BlueZ discovery 路径并明确告警，绝不假装直读通道已建立。
namespace railhmi {

// 一次性关闭控制器去重（保留给诊断工具/降级路径使用）。
// 返回 0=成功；-1=权限不足；-2=平台不支持或调用失败。
int disableBleDuplicateFilter(unsigned short deviceId = 0);

// 打开一条常驻、非阻塞的 HCI_CHANNEL_RAW 套接字，并设置只接收
// HCI_EVENT_PKT / EVT_LE_META_EVENT 的过滤器。成功返回 fd(>=0)，失败返回负值。
int openBleAdvSocket(unsigned short deviceId = 0);

// 在已打开的 fd 上下发 LE Set Scan Parameters（被动扫描，60ms 间隔/窗口）。
int configureBleScan(int fd);

// 在已打开的 fd 上下发 LE Set Scan Enable(enable=1, filter_dup=0)。
// 需周期性重发，抵消 bluetoothd 约每 10s 重新打开去重。
int enableBleScanNoDup(int fd);

// 退出时把扫描关掉，不把控制器留在扫描态。
int disableBleScan(int fd);

// 非阻塞读一个 HCI 事件包；返回实际字节数，<=0 表示 EAGAIN 或错误。
int readBleAdvPacket(int fd, unsigned char* buffer, int capacity);

void closeBleAdvSocket(int fd);

}  // namespace railhmi
