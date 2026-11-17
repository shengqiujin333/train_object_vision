#include "maintenance_ops.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSysInfo>
#include <QStorageInfo>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>
#include <cstdio>
#include <fstream>
#include <string>

#include "nvr_client.h"
#include "railhmi/core/version.h"
#include "railhmi/platform/log.h"

#ifdef RAILHMI_HAS_MEDIA
#include <gst/gst.h>
#endif

namespace {

QString readFirstLine(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QString();
    const QString line = QString::fromLatin1(f.readLine(512)).trimmed();
    return line;
}

bool readKeyValue(const QString& path, const QString& key, long long* out_mb) {
    // 按整文件解析（/proc/meminfo 的行宽/空格数不固定，逐行 readLine 容易被截断）。
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QString all = QString::fromLatin1(f.readAll());
    for (const QString& raw : all.split(QLatin1Char('\n'))) {
        const QString line = raw.simplified();
        if (!line.startsWith(key)) continue;
        const QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (parts.size() < 2) return false;
        bool ok = false;
        const long long kb = parts[1].toLongLong(&ok);
        if (!ok) return false;
        if (out_mb) *out_mb = kb / 1024;
        return true;
    }
    return false;
}

// 运行时库可能在 /usr/lib、/usr/lib/aarch64-linux-gnu、/usr/local/lib 等目录；
// 逐个候选查找，找到就报路径与大小，全都没有才说「未找到」。
QString findLib(const QStringList& names) {
    const QStringList dirs = {QStringLiteral("/usr/lib/aarch64-linux-gnu"),
                              QStringLiteral("/usr/lib"),
                              QStringLiteral("/usr/local/lib"),
                              QStringLiteral("/lib/aarch64-linux-gnu")};
    for (const QString& name : names) {
        for (const QString& dir : dirs) {
            QFileInfo fi(dir + QLatin1Char('/') + name);
            if (fi.exists())
                return QStringLiteral("%1（%2 KB）").arg(fi.absoluteFilePath()).arg(fi.size() / 1024);
        }
    }
    return QString();
}

}  // namespace

MaintenanceOps::MaintenanceOps(const railhmi::core::AppSettings& settings, const QString& configDir,
                               QObject* parent)
    : QObject(parent), settings_(settings), config_dir_(configDir) {
    addLine("维护日志已就绪（最多保留最近 800 行；可保存/导出）");
}

MaintenanceOps::~MaintenanceOps() {
    if (ping_process_) {
        ping_process_->kill();
        ping_process_->deleteLater();
    }
}

void MaintenanceOps::addLine(const QString& line) {
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    log_.append(QStringLiteral("[%1] %2").arg(stamp, line));
    while (log_.size() > 800) log_.removeFirst();
    railhmi::platform::Logger::instance().info("[maint] " + line.toStdString());
    emit logChanged();
}

void MaintenanceOps::appendLog(const QString& tag, const QString& text) {
    addLine(tag.isEmpty() ? text : QStringLiteral("[%1] %2").arg(tag, text));
}

void MaintenanceOps::clearLog() {
    log_.clear();
    last_export_.clear();
    addLine("日志已清空");
}

QString MaintenanceOps::exportLog() {
    QString base = config_dir_;
    if (base.endsWith(QLatin1Char('/'))) base.chop(1);
    QDir dir(base + QStringLiteral("/../logs"));
    if (!dir.exists() && !QDir().mkpath(dir.absolutePath())) {
        addLine("导出失败：无法创建目录 " + dir.absolutePath());
        return QString();
    }
    const QString name = QStringLiteral("maintenance-%1.log")
                             .arg(QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss"));
    const QString path = dir.absoluteFilePath(name);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        addLine("导出失败：无法写入 " + path);
        return QString();
    }
    f.write(log_.join(QLatin1Char('\n')).toUtf8());
    f.write("\n");
    f.close();
    last_export_ = path;
    addLine("日志已保存：" + path);
    return path;
}

void MaintenanceOps::pingCameras(const QVariantList& cameras) {
    if (busy_) {
        addLine("摄像头检测：上一次检测仍在进行，本次忽略");
        return;
    }
    ping_queue_ = cameras;
    ping_index_ = 0;
    busy_ = true;
    emit busyChanged();
    addLine(QStringLiteral("===== 摄像头检测开始（%1 路，逐个 ping 1 包/1 秒超时）=====")
                .arg(cameras.size()));
    QTimer::singleShot(0, this, &MaintenanceOps::nextPing);
}

void MaintenanceOps::nextPing() {
    if (ping_index_ >= ping_queue_.size()) {
        finishPing();
        return;
    }
    const QVariantMap cam = ping_queue_[ping_index_].toMap();
    const QString id = cam.value(QStringLiteral("id")).toString();
    const QString name = cam.value(QStringLiteral("name")).toString();
    const QString url = cam.value(QStringLiteral("url")).toString();
    const QString host = QUrl(url).host();
    if (host.isEmpty()) {
        addLine(QStringLiteral("%1 %2：地址里没有主机名（%3），跳过").arg(id, name, url));
        ++ping_index_;
        QTimer::singleShot(0, this, &MaintenanceOps::nextPing);
        return;
    }

    addLine(QStringLiteral("%1 %2 → ping %3 …").arg(id, name, host));
    const int token = ++ping_token_;
    ping_process_ = new QProcess(this);
    ping_process_->setProcessChannelMode(QProcess::MergedChannels);

    QObject::connect(ping_process_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                     this, [this, id, name, token](int code, QProcess::ExitStatus) {
        if (token != ping_token_ || !ping_process_) return;
        const QString out = QString::fromLocal8Bit(ping_process_->readAll());
        ping_process_->deleteLater();
        ping_process_ = nullptr;
        QString rtt;
        const int t = out.indexOf(QStringLiteral("time="));
        if (t >= 0) rtt = out.mid(t + 5).section(QLatin1Char(' '), 0, 0);
        if (code == 0) {
            addLine(QStringLiteral("%1 %2：在线（%3）")
                        .arg(id, name, rtt.isEmpty() ? QStringLiteral("RTT 未解析") :
                                                       QStringLiteral("RTT ") + rtt));
        } else {
            addLine(QStringLiteral("%1 %2：不在线（ping 失败，exit=%3）").arg(id, name).arg(code));
        }
        ++ping_index_;
        QTimer::singleShot(0, this, &MaintenanceOps::nextPing);
    });
    QObject::connect(ping_process_, &QProcess::errorOccurred, this,
                     [this, id, name, token](QProcess::ProcessError err) {
        if (token != ping_token_ || err != QProcess::FailedToStart) return;
        addLine(QStringLiteral("%1 %2：ping 命令无法执行").arg(id, name));
        if (ping_process_) {
            ping_process_->deleteLater();
            ping_process_ = nullptr;
        }
        ++ping_index_;
        QTimer::singleShot(0, this, &MaintenanceOps::nextPing);
    });
    ping_process_->start(QStringLiteral("ping"),
                         {QStringLiteral("-c"), QStringLiteral("1"), QStringLiteral("-W"),
                          QStringLiteral("1"), host});

    // 看门狗：2.5s 未返回则强制结束，避免某个网络黑洞把整轮检测挂住。
    // 用 token 区分「这一路」：上一路的定时器不能杀掉下一路的进程。
    QTimer::singleShot(2500, this, [this, id, token]() {
        if (token != ping_token_ || !ping_process_) return;
        ping_process_->kill();
        addLine(QStringLiteral("%1：ping 超时（>2.5s），判定不在线").arg(id));
        if (ping_process_) {
            ping_process_->deleteLater();
            ping_process_ = nullptr;
        }
        ++ping_index_;
        QTimer::singleShot(0, this, &MaintenanceOps::nextPing);
    });
}

void MaintenanceOps::finishPing() {
    busy_ = false;
    emit busyChanged();
    addLine("===== 摄像头检测结束 =====");
}

void MaintenanceOps::checkNvr() {
    const QString path = config_dir_ + QStringLiteral("/nvr.yaml");
    NvrSettings settings;
    QString error;
    if (!loadNvrSettings(path, &settings, &error)) {
        addLine("NVR 检测：配置读取失败 " + error);
        return;
    }
    if (!settings.enabled) {
        addLine("NVR 检测：nvr.yaml 未启用（enabled=false），无法登录");
        return;
    }
    QString credential = qEnvironmentVariable("TRAIN_NVR_CREDENTIAL");
    if (credential.isEmpty())
        credential = qEnvironmentVariable(settings.credentialEnv.toUtf8().constData());
    addLine(QStringLiteral("NVR 检测：连接 %1（账号 %2）…")
                .arg(settings.address.toString(), settings.username));
    if (credential.isEmpty()) {
        addLine(QStringLiteral("NVR 检测：缺少凭据环境变量 %1，无法登录")
                    .arg(settings.credentialEnv));
        return;
    }

    nvr_.reset(new NvrClient(settings, this));
    QObject::connect(nvr_.get(), &NvrClient::channelsReady, this, [this](const QVariantList& chans) {
        addLine(QStringLiteral("NVR 检测：在线且登录成功，读取到 %1 个通道").arg(chans.size()));
        for (const auto& c : chans) {
            const QVariantMap m = c.toMap();
            addLine(QStringLiteral("  · 通道 %1 %2")
                        .arg(m.value("id").toString(), m.value("name").toString()));
        }
    });
    QObject::connect(nvr_.get(), &NvrClient::failed, this,
                     [this](const QString& op, const QString& reason) {
        addLine(QStringLiteral("NVR 检测失败（%1）：%2").arg(op, reason));
    });
    nvr_->setCredentials(settings.username, credential);
    nvr_->fetchChannels();
}

QVariantList MaintenanceOps::systemInfo() const {
    QVariantList out;
    auto add = [&out](const QString& group, const QString& key, const QString& value) {
        QVariantMap m;
        m[QStringLiteral("group")] = group;
        m[QStringLiteral("key")] = key;
        m[QStringLiteral("value")] = value;
        out.append(m);
    };

    add(QStringLiteral("系统"), QStringLiteral("操作系统"), QSysInfo::prettyProductName());
    add(QStringLiteral("系统"), QStringLiteral("内核"),
        QSysInfo::kernelType() + QLatin1Char(' ') + QSysInfo::kernelVersion());
    add(QStringLiteral("系统"), QStringLiteral("架构"), QSysInfo::currentCpuArchitecture());
    add(QStringLiteral("系统"), QStringLiteral("应用版本"),
        QString::fromStdString(railhmi::core::version_string()) + QLatin1Char(' ') +
            QString::fromStdString(railhmi::core::build_description()));
    add(QStringLiteral("系统"), QStringLiteral("Qt 版本"), QString::fromLatin1(qVersion()));
#ifdef RAILHMI_HAS_MEDIA
    add(QStringLiteral("系统"), QStringLiteral("GStreamer"), QString::fromLatin1(gst_version_string()));
#endif
    add(QStringLiteral("系统"), QStringLiteral("CPU 核心数"),
        QString::number(QThread::idealThreadCount()));
    add(QStringLiteral("系统"), QStringLiteral("负载"),
        QString::fromLatin1(readFirstLine(QStringLiteral("/proc/loadavg")).toLatin1()));

    long long total_mb = 0, avail_mb = 0;
    readKeyValue(QStringLiteral("/proc/meminfo"), QStringLiteral("MemTotal:"), &total_mb);
    readKeyValue(QStringLiteral("/proc/meminfo"), QStringLiteral("MemAvailable:"), &avail_mb);
    if (total_mb > 0) {
        const long long used = total_mb - avail_mb;
        add(QStringLiteral("资源"), QStringLiteral("内存"),
            QStringLiteral("%1 MB / %2 MB（占用 %3%）")
                .arg(used)
                .arg(total_mb)
                .arg(total_mb > 0 ? used * 100 / total_mb : 0));
    } else {
        add(QStringLiteral("资源"), QStringLiteral("内存"), QStringLiteral("不可读（/proc/meminfo）"));
    }

    QStorageInfo storage(QStringLiteral("/"));
    storage.refresh();
    if (storage.isValid()) {
        add(QStringLiteral("资源"), QStringLiteral("磁盘 /"),
            QStringLiteral("%1 GB 可用 / %2 GB")
                .arg(storage.bytesAvailable() / (1024.0 * 1024 * 1024), 0, 'f', 1)
                .arg(storage.bytesTotal() / (1024.0 * 1024 * 1024), 0, 'f', 1));
    }

    const QString npu_load = readFirstLine(QStringLiteral("/sys/kernel/debug/rknpu/load"));
    add(QStringLiteral("资源"), QStringLiteral("NPU 负载"),
        npu_load.isEmpty() ? QStringLiteral("不可读（debugfs 未开放）") : npu_load);
    const QString npu_ver = readFirstLine(QStringLiteral("/sys/kernel/debug/rknpu/version"));
    add(QStringLiteral("资源"), QStringLiteral("NPU 驱动"),
        npu_ver.isEmpty() ? QStringLiteral("不可读") : npu_ver);
    const QString devfreq =
        readFirstLine(QStringLiteral("/sys/class/devfreq/fdab0000.npu/load"));
    if (!devfreq.isEmpty())
        add(QStringLiteral("资源"), QStringLiteral("NPU 聚合负载"), devfreq);

    // 运行时库：逐个候选路径查找，只报「找到的路径与大小」或「未找到」。
    const char* libs[][3] = {
        {"librknnrt.so", "librknnrt.so.2", "librknnrt.so.1"},
        {"librga.so", "librga.so.2", "librga.so.1"},
        {"libgstreamer-1.0.so.0", "libgstreamer-1.0.so", nullptr}};
    const char* lib_labels[] = {"librknnrt.so", "librga.so", "libgstreamer-1.0.so.0"};
    for (int i = 0; i < 3; ++i) {
        QStringList names;
        for (int j = 0; j < 3; ++j)
            if (libs[i][j]) names << QString::fromLatin1(libs[i][j]);
        const QString found = findLib(names);
        add(QStringLiteral("库"), QString::fromLatin1(lib_labels[i]),
            found.isEmpty() ? QStringLiteral("未找到") : found);
    }

    add(QStringLiteral("AI"), QStringLiteral("模型目录"),
        QString::fromStdString(settings_.app.ai.models_dir));
    for (const auto& m : settings_.app.ai.models) {
        add(QStringLiteral("AI"),
            QStringLiteral("模型配置 %1").arg(QString::fromStdString(
                railhmi::core::to_string(m.profile))),
            QStringLiteral("%1（%2）")
                .arg(QString::fromStdString(m.name), QString::fromStdString(m.version)));
    }
    return out;
}
