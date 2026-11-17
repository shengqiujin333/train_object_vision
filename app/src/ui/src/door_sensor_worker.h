#pragma once

// railhmi UI: BLE 门磁的**独立 I/O 工作线程**。
//
// 现场问题与结论（11-16 深测修正）：
//  * 原来所有 BlueZ 调用都在 GUI 线程用 QDBus::Block(3000) 同步执行，且
//    PropertiesChanged 订阅写成了不存在的接口
//    "org.freedesktop.DBus.PropertiesChanged"，事件驱动路径完全失效。
//  * 更深一层根因是控制器/内核 BLE 广播去重：btmon 抓到
//    "Filter duplicates: Enabled"，1s 一次的广播只剩 2~25s 一次且帧计数跳号；
//    BlueZ 的 DuplicateData=true 只管 BlueZ 内部合并，关不掉控制器去重。
//  * 最终结论：**裸 HCI 命令与 BlueZ discovery 互斥**。bluetoothd 在 discovery
//    期间约每 10s 重新下发 LE Set Scan Enable 并重新打开去重；一旦第三方下发
//    裸 HCI 命令，BlueZ 会卡在 StartDiscovery=InProgress / Discovering=False /
//    Device1 对象数=0。因此不能一边用 BlueZ discovery、一边周期发 HCI 命令。
//
// 生产主路径（需要 CAP_NET_ADMIN + CAP_NET_RAW，由 systemd ExecStartPre 授予）：
//  * 独占 HCI_CHANNEL_RAW 套接字，自己下发 LE Set Scan Parameters +
//    LE Set Scan Enable(filter_dup=0)，并周期性（2s）重发以抵消 bluetoothd。
//  * 直接解析 LE Advertising Report 里的 0xFF07 厂商字段，把帧立即推给 GUI。
//    实测 35s 收到 13 个 T1 广播，间隔 1.0~6.0s，<=5s 占 10/12；新状态
//    随下一帧广播即可到达，不受 BlueZ D-Bus 推送节流影响。
//  * 完全不调用 StartDiscovery / GetManagedObjects，不占用 BlueZ 发现状态。
//
// 降级路径（无 HCI 能力时）：保留原 Qt5::DBus + BlueZ 方案，
// SetDiscoveryFilter(Transport=le, DuplicateData=true) + StartDiscovery +
// 1s GetManagedObjects 轮询 + 正确接口名的 PropertiesChanged 订阅。
// 此时延迟回到 BlueZ 缓存节奏，会明确告警，不谎称达到 <=5s。
//
// 本对象运行在专用 QThread：阻塞式 D-Bus 调用与 HCI 读取都留在该线程内，
// GUI 线程只接收纯 QVariant 帧。

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QElapsedTimer>
#include <vector>

#include "railhmi/core/config.h"

class QTimer;
class QSocketNotifier;

class DoorSensorWorker : public QObject {
    Q_OBJECT
public:
    explicit DoorSensorWorker(const railhmi::core::DoorSensorConfig& cfg,
                              QObject* parent = nullptr);
    ~DoorSensorWorker() override;

public slots:
    void init();                 // 在工作线程内建立 I/O 通道
    void poll();                 // 降级路径：全量 GetManagedObjects（阻塞，但在工作线程内）

signals:
    // 全量扫描结果：每帧含 mac/name/rssi/path/payloads/vendor/forceFresh。
    void scanResult(const QVariantList& frames, const QString& adapterPath,
                    int deviceCount, int matchCount);
    // 事件驱动：一次广播到达即刻发出（HCI 直读或 BlueZ PropertiesChanged）。
    void pushedFrame(const QVariantMap& frame);
    void sourceError(const QString& reason, const QString& detail);
    void info(const QString& text);
    // 实际生效的 I/O 模式，供维护页展示真值："hci-raw" / "bluez"。
    void ioMode(const QString& mode, const QString& adapter);

private slots:
    void onPropertiesChanged(const QString& interfaceName, const QVariantMap& changed,
                             const QStringList& invalidated, const QDBusMessage& message);
    void onAdvData();            // HCI 原始通道可读
    void refreshRawScan();       // 周期性重发 filter_dup=0

private:
    QVariantMap makeFrame(const QString& objectPath, const QVariantMap& props,
                          bool forceFresh) const;
    bool isCandidate(const QString& mac, const QString& name) const;
    void applyDiscovery(const QString& adapterPath);
    void parseManagedObjects(const QDBusMessage& reply);
    void startRawHci();
    void startBluezFallback();
    void parseAdvReports(const unsigned char* data, int len);
    QVariantMap makeRawFrame(const QString& mac, const QString& name, int rssi,
                             const QByteArray& vendorValue) const;

    railhmi::core::DoorSensorConfig cfg_;
    QDBusConnection bus_;
    QTimer* timer_ = nullptr;
    QTimer* rawScanTimer_ = nullptr;
    QSocketNotifier* advNotifier_ = nullptr;
    int advFd_ = -1;
    bool rawMode_ = false;
    bool rawFrameLogged_ = false;
    QString adapterPath_;
    bool filterApplied_ = false;
    bool filterLogged_ = false;
    bool discoveryLogged_ = false;
    bool subscribed_ = false;
    QElapsedTimer clock_;
    long long lastDiscoveryMs_ = -60000;
};
