#pragma once

// railhmi UI: 设备维护页的操作与日志（2026-11-17 现场需求）。
//
// 页面分成两大块：日志显示 / 功能操作。所有功能操作的过程信息都写进同一份日志，
// 日志可保存、可导出。本类只做「能真做的事」：
//   * 摄像头检测：逐个 ping（1 包 / 1s 超时），如实报 RTT 或超时；
//   * 录像与存储：按 nvr.yaml 真实连接并登录（NvrClient::fetchChannels），拿到通道即登录成功；
//   * 系统信息：OS/内核/架构/版本/Qt/GStreamer/CPU/NPU/内存/磁盘 + 模型目录；
//   * 蓝牙扫描、故障自检由页面用已有真值（railhmiDoorDevices / railhmiChannels /
//     railhmiFaults）组装后调用 appendLog 写入，避免在此重复一套判定。
// 未接入/失败一律如实记录，不编造成功。

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <memory>

#include "railhmi/core/config.h"

class QProcess;
class NvrClient;

class MaintenanceOps : public QObject {
    Q_OBJECT
    Q_PROPERTY(QStringList logLines READ logLines NOTIFY logChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString lastExportPath READ lastExportPath NOTIFY logChanged)
public:
    MaintenanceOps(const railhmi::core::AppSettings& settings, const QString& configDir,
                   QObject* parent = nullptr);
    ~MaintenanceOps() override;

    QStringList logLines() const { return log_; }
    bool busy() const { return busy_; }
    QString lastExportPath() const { return last_export_; }

    // QML 可调用
    Q_INVOKABLE void appendLog(const QString& tag, const QString& text);
    Q_INVOKABLE void clearLog();
    Q_INVOKABLE QString exportLog();
    // cameras: [{id,name,url}]；逐个 ping（异步串行，不阻塞界面）。
    Q_INVOKABLE void pingCameras(const QVariantList& cameras);
    // 录像与存储：真实连接 NVR 并登录（读取通道即成功）。
    Q_INVOKABLE void checkNvr();
    // 系统信息：[{group,key,value}]
    Q_INVOKABLE QVariantList systemInfo() const;
    // WiFi 驱动与链路自检（多行文本，供「故障自检」写进日志）。
    // 检查：无线网卡是否存在 / 驱动是否绑定且模块已加载 / 是否连上 AP / 信号 / IP / 驱动报错。
    Q_INVOKABLE QString wifiCheckSummary() const;

    // U 盘导出（2026-11-17 现场需求）：报警数据 + 设备维护日志。
    // 返回检测到的可写挂载点（无则空串）；UI 用它给出明确提示，不假装成功。
    Q_INVOKABLE QString usbTarget() const;
    // 导出到 U 盘；alarms 为报警行（键值对）。返回导出目录（失败返回空串，原因写进日志）。
    Q_INVOKABLE QString exportToUsb(const QVariantList& alarms);

signals:
    void logChanged();
    void busyChanged();

private:
    void addLine(const QString& line);
    void nextPing();
    void finishPing();
    void emitInfo(const QString& tag, const QVariantList& info);

    railhmi::core::AppSettings settings_;
    QString config_dir_;
    QStringList log_;
    QString last_export_;
    bool busy_ = false;

    QVariantList ping_queue_;
    int ping_index_ = 0;
    int ping_token_ = 0;          // 每路一个令牌：防止上一路的看门狗误杀下一路的进程
    QProcess* ping_process_ = nullptr;
    std::unique_ptr<NvrClient> nvr_;
};
