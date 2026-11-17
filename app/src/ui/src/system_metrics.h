#pragma once

// railhmi UI: 顶栏系统资源读数（真实采样，不造假）。
//
// 内存    : /proc/meminfo   (MemTotal - MemAvailable) / MemTotal
// CPU     : /proc/stat      每个 cpuN 的相邻两次差值（busy / total）
// NPU     : /sys/kernel/debug/rknpu/load 的 Core0/1/2（RK3588 三核）；
//           该文件默认仅 root 可读，读不到时退到 devfreq 聚合 load，并明确标注
//           「聚合」，绝不把单值复制成三个核的假分核读数；两者都不可读时显示未接入。
//
// 采样在 GUI 线程做，只读 /proc 与 /sys 的小文本，不做阻塞式外部调用。

#include <QObject>
#include <QVariantMap>
#include <QVector>
#include <QtGlobal>

class QTimer;

struct SystemCpuTick {
    quint64 total = 0;
    quint64 busy = 0;
};

class SystemMetricsRuntime : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap metrics READ metrics NOTIFY changed)
public:
    explicit SystemMetricsRuntime(QObject* parent = nullptr);
    QVariantMap metrics() const { return metrics_; }

signals:
    void changed();

private slots:
    void sample();

private:
    QTimer* timer_ = nullptr;
    QVector<SystemCpuTick> prevCpu_;
    QVariantMap metrics_;
    bool loggedOnce_ = false;
};
