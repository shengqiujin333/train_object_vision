#pragma once

// railhmi UI: BLE 门磁运行时状态适配（HCI 直读为主，BlueZ D-Bus 降级）。
//
// 现场加速改造（原「反应太慢」根因，11-16 深测后修正）：
//   * 旧实现所有 BlueZ 调用都在 GUI 线程以 QDBus::Block(3000) 同步执行，
//     每次最长卡界面 3 秒；且 10s 才做一次全量 GetManagedObjects。
//   * PropertiesChanged 订阅曾写成不存在的接口
//     "org.freedesktop.DBus.PropertiesChanged"（正确接口是
//     org.freedesktop.DBus.Properties），主动推送完全没进 HMI。
//   * 更深一层是控制器/内核 BLE 去重（Filter duplicates=Enabled）：厂家标称
//     1s 广播被丢成 2~25s 一次，帧计数跳号；BlueZ DuplicateData=true 关不掉它。
//   * 最终结论：裸 HCI 命令与 BlueZ discovery 互斥。bluetoothd 在 discovery
//     期间约每 10s 重新下发 LE Set Scan Enable 并重新打开去重；第三方裸 HCI
//     命令会让 BlueZ 卡在 InProgress / Discovering=False / Device1=0。
//   * 生产主路径因此改为 DoorSensorWorker 在独立 QThread 独占 HCI_CHANNEL_RAW，
//     自行下发 filter_dup=0 并直接解析 LE Advertising Report（1~2s 一帧），
//     不碰 BlueZ discovery；无 CAP_NET_ADMIN/CAP_NET_RAW 时才降级到 BlueZ。
//   * 本对象（GUI 线程）只接收纯 QVariant 帧做状态汇总，D-Bus 等待为零。
//
// 厂商 PDF 只定义广播（蓝牙 4.x legacy advertising），没有 GATT 服务/特征值，
// 也没有配对/绑定流程；因此这里**不连接、不配对、不写特征值**。
//
// 真值约束（INTERFACE_CONTRACT §10）：未核验 / 格式不符 / 过期 / BlueZ 失败一律
// 显示不可用，绝不按「门已关」默认；没有设备时如实显示「未接入」。

#include <QByteArray>
#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <map>
#include <string>

#include "railhmi/core/config.h"
#include "railhmi/core/door_sensor.h"

class QThread;
class QTimer;
class DoorSensorWorker;

class DoorSensorRuntime : public QObject {
    Q_OBJECT
public:
    explicit DoorSensorRuntime(const railhmi::core::DoorSensorConfig& cfg,
                               QObject* parent = nullptr);
    ~DoorSensorRuntime() override;

    void start();

    QVariantList devices() const { return devices_; }    // 维护页逐设备行
    QVariantMap summary() const { return summary_; }    // 顶栏/关键状态区/报警条汇总
    bool enabled() const { return cfg_.enabled; }

signals:
    void changed();                                      // 快照变化（GUI 线程）
    void doorAjar(const QString& id, const QString& description);  // 由非未关严 -> 未关严

private slots:
    void onScanResult(const QVariantList& frames, const QString& adapterPath,
                      int deviceCount, int matchCount);
    void onPushedFrame(const QVariantMap& frame);
    void onSourceError(const QString& reason, const QString& detail);
    void onWorkerInfo(const QString& text);
    void onIoMode(const QString& mode, const QString& adapter);
    void refreshAges();                              // 1s 轻量重算「N 秒前」/超时，不读 D-Bus
    void injectFollowupFrame();                      // 仿真「后续帧」回归用

private:
    struct Entry {
        railhmi::core::DoorSensorState state;
        QByteArray fingerprint;      // 最近一次观察到的广播指纹，用于判断新鲜度
        bool was_ajar = false;
        // 是否曾经解析到合法厂商帧。门磁 Key 0x08 是「状态变化立即上报」、
        // 电池 Key 0x63 约 5 分钟一次，因此后续广播可能只带 RSSI 或只带电池；
        // 不能因为某一帧没带厂商数据就把已核验设备打回未核验、丢掉最后的门状态。
        bool ever_verified = false;
        QByteArray lastFrameHex;             // 最近一次广播字节（维护页现场诊断用）
    };

    void seedRegistered();
    void injectSimulationFrame();                     // 显式环境变量注入（仅验证用）
    // RAILHMI_DOOR_SIM_FOLLOWUP / _MS：在首帧之后补一帧「无门磁 Key」的广播，
    // 专门回归「后续电池帧 / 纯 RSSI 帧不得把已核验门状态抹掉」。同样仅验证用。
    void rebuildSnapshots();
    void setSourceError(const QString& reason, const QString& detail);
    long long nowMs() const { return clock_.elapsed(); }
    bool isCandidate(const QString& mac, const QString& name) const;
    const railhmi::core::DoorSensorDeviceConfig* registeredFor(const QString& mac) const;
    void applyAdvertisement(Entry& e, const QVariantMap& props, const QString& mac,
                            const QString& name, long long now, bool forceFresh);
    void applyFrame(Entry& e, const QVariantMap& frame, long long now);
    void applyPayloads(Entry& e, const QList<QByteArray>& payloads, int rssi,
                       const QString& name, const QString& mac, long long now,
                       const QByteArray& fingerprint);

    railhmi::core::DoorSensorConfig cfg_;
    QThread* thread_ = nullptr;
    DoorSensorWorker* worker_ = nullptr;
    QTimer* ageTimer_ = nullptr;
    QElapsedTimer clock_;
    QString adapterPath_;
    QString lastError_;
    QString ioMode_;                     // "hci-raw" / "bluez-fallback"（真值，不臆测）
    QString source_ = "disabled";
    int pollCount_ = 0;
    int scanDevices_ = 0;                   // 本次 BlueZ 快照里的 Device1 总数
    int scanMatched_ = 0;                   // 其中通过门磁发现闸门的数量
    int pushEvents_ = 0;                    // 事件驱动推送累计次数（验收用）
    long long lastPushMs_ = -1;              // 最近一次推送到达时刻（验收用）
    std::map<QString, Entry> entries_;   // key = 大写 MAC
    QVariantList devices_;
    QVariantMap summary_;
};

QString door_reason_label(const std::string& token);
