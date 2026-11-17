#include "system_metrics.h"

#include <QFile>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <algorithm>

#include "railhmi/platform/log.h"

namespace {

const char* kProcStat = "/proc/stat";
const char* kProcMeminfo = "/proc/meminfo";
const char* kNpuDebugLoad = "/sys/kernel/debug/rknpu/load";
const char* kNpuDevfreqLoad = "/sys/class/devfreq/fdab0000.npu/load";
const int kSampleIntervalMs = 2000;

QString readText(const char* path) {
    QFile f(QString::fromLatin1(path));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return QString();
    return QString::fromUtf8(f.readAll());
}

int clampPct(long long v) {
    return static_cast<int>(std::max<long long>(0, std::min<long long>(100, v)));
}

// /proc/stat 的 cpuN 行：user nice system idle iowait irq softirq steal guest guest_nice
QVector<SystemCpuTick> readCpuTicks() {
    QVector<SystemCpuTick> out;
    const QString text = readText(kProcStat);
    for (const QString& line : text.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        if (!line.startsWith(QLatin1String("cpu"))) continue;
        const QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (parts.isEmpty()) continue;
        const QString name = parts.at(0);
        if (name == QLatin1String("cpu") || name.size() < 4) continue;
        bool numeric = true;
        for (int i = 3; i < name.size(); ++i)
            if (!name.at(i).isDigit()) { numeric = false; break; }
        if (!numeric) continue;
        quint64 total = 0, idle = 0;
        for (int i = 1; i < parts.size(); ++i) {
            const quint64 v = parts.at(i).toULongLong();
            total += v;
            if (i == 4 || i == 5) idle += v;   // idle + iowait
        }
        SystemCpuTick t;
        t.total = total;
        t.busy = total > idle ? total - idle : 0;
        out.append(t);
    }
    return out;
}

long long meminfoValue(const QString& line) {
    const QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    return parts.size() >= 2 ? parts.at(1).toLongLong() : 0;
}

int memoryUsedPct() {
    const QString text = readText(kProcMeminfo);
    long long totalKb = 0, availKb = -1;
    for (const QString& line : text.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        if (line.startsWith(QLatin1String("MemTotal:"))) totalKb = meminfoValue(line);
        else if (line.startsWith(QLatin1String("MemAvailable:"))) availKb = meminfoValue(line);
    }
    if (totalKb <= 0 || availKb < 0) return -1;   // -1 = 未取到，不填 0 冒充
    return clampPct((totalKb - availKb) * 100 / totalKb);
}

// "NPU load:  Core0:  0%, Core1:  0%, Core2:  0%," -> [0, 0, 0]
QVariantList npuPerCore() {
    QVariantList out;
    const QString text = readText(kNpuDebugLoad);
    if (text.isEmpty()) return out;
    for (const QString& part : text.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const int core = part.indexOf(QLatin1String("Core"));
        const int pct = part.indexOf(QLatin1Char('%'));
        if (core < 0 || pct < 0) continue;
        const int colon = part.indexOf(QLatin1Char(':'), core);
        if (colon < 0 || colon >= pct) continue;
        bool ok = false;
        const int v = part.mid(colon + 1, pct - colon - 1).trimmed().toInt(&ok);
        if (ok) out.append(clampPct(v));
    }
    return out;
}

// "100@1000000000Hz" -> 100（devfreq 聚合负载，非分核值）
int npuAggregate() {
    const QString text = readText(kNpuDevfreqLoad).trimmed();
    if (text.isEmpty()) return -1;
    bool ok = false;
    const int v = text.section(QLatin1Char('@'), 0, 0).trimmed().toInt(&ok);
    return ok ? clampPct(v) : -1;
}

}  // namespace

SystemMetricsRuntime::SystemMetricsRuntime(QObject* parent) : QObject(parent) {
    timer_ = new QTimer(this);
    timer_->setInterval(kSampleIntervalMs);
    connect(timer_, &QTimer::timeout, this, &SystemMetricsRuntime::sample);
    sample();                 // 建首个 CPU 基准；此时各核为 0%（无差值可算）
    timer_->start();
}

void SystemMetricsRuntime::sample() {
    const QVector<SystemCpuTick> ticks = readCpuTicks();
    QVariantList cpuCores;
    long long cpuSum = 0;
    for (int i = 0; i < ticks.size(); ++i) {
        int pct = 0;
        if (i < prevCpu_.size()) {
            const quint64 dTotal = ticks[i].total > prevCpu_[i].total
                                       ? ticks[i].total - prevCpu_[i].total : 0;
            const quint64 dBusy = ticks[i].busy > prevCpu_[i].busy
                                      ? ticks[i].busy - prevCpu_[i].busy : 0;
            if (dTotal > 0) pct = clampPct(static_cast<long long>(dBusy * 100 / dTotal));
        }
        cpuCores.append(pct);
        cpuSum += pct;
    }
    prevCpu_ = ticks;

    const int memPct = memoryUsedPct();
    const QVariantList npu = npuPerCore();
    const int aggregate = npuAggregate();
    const bool perCore = !npu.isEmpty();
    const bool anyNpu = perCore || aggregate >= 0;

    QVariantMap m;
    m["memoryPct"] = memPct;                       // -1 = 未取到
    m["cpuCores"] = cpuCores;
    m["cpuCount"] = cpuCores.size();
    m["cpuAvgPct"] = cpuCores.isEmpty() ? 0 : clampPct(cpuSum / cpuCores.size());
    m["npuCores"] = npu;
    m["npuCount"] = npu.size();
    m["npuAggregatePct"] = aggregate;
    m["npuPerCoreAvailable"] = perCore;
    m["npuAvailable"] = anyNpu;
    m["npuNote"] = perCore ? QString()
        : (anyNpu ? QString::fromUtf8("分核不可读·显示聚合值")
                  : QString::fromUtf8("NPU 负载未接入"));
    m["cpuSource"] = QString::fromLatin1(kProcStat);
    m["npuSource"] = QString::fromLatin1(perCore ? kNpuDebugLoad : kNpuDevfreqLoad);

    if (!loggedOnce_) {
        loggedOnce_ = true;
        railhmi::platform::Logger::instance().info(
            "system-metrics: cpu=" + std::to_string(cpuCores.size()) + " cores, mem=" +
            std::to_string(memPct) + "%, npu=" + std::to_string(npu.size()) +
            " cores, perCore=" + (perCore ? "yes" : "no") +
            ", aggregate=" + std::to_string(aggregate));
    }
    if (m == metrics_) return;
    metrics_ = m;
    emit changed();
}
