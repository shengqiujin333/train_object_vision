#include "nvr_status.h"

#include <QAbstractSocket>
#include <QTimer>
#include <QUrl>

#include "railhmi/platform/log.h"

namespace {
const int kProbeIntervalMs = 15000;   // 复核周期：断网后最多 15s + 3s 超时置为 NVR异常
const int kConnectTimeoutMs = 3000;   // 单次连接超时
const int kFirstProbeDelayMs = 1500;  // 启动后稍等网络/配置就绪再首探
}

NvrStatusRuntime::NvrStatusRuntime(const QString& configFile, QObject* parent)
    : QObject(parent), configFile_(configFile) {
    socket_ = new QTcpSocket(this);
    timeoutTimer_ = new QTimer(this);
    timeoutTimer_->setSingleShot(true);
    probeTimer_ = new QTimer(this);

    // 只依赖 stateChanged，避开 Qt 5.15 起 deprecated 的 QAbstractSocket::error 重载；
    // 即便某条错误路径不发状态变化，3s 超时也会兜底判为异常。
    connect(socket_, &QAbstractSocket::stateChanged, this,
            [this](QAbstractSocket::SocketState state) {
        if (!probing_) return;
        if (state == QAbstractSocket::ConnectedState)
            finish(true, QStringLiteral("tcp connected"));
        else if (state == QAbstractSocket::UnconnectedState)
            finish(false, QStringLiteral("connection refused or closed"));
    });
    connect(timeoutTimer_, &QTimer::timeout, this, [this]() {
        finish(false, QStringLiteral("connect timeout"));
    });
    connect(probeTimer_, &QTimer::timeout, this, [this]() { probe(); });

    railhmi::platform::Logger::instance().info(
        "nvr-status: real TCP probe enabled, config=" + configFile_.toStdString());
    probeTimer_->start(kProbeIntervalMs);
    QTimer::singleShot(kFirstProbeDelayMs, this, [this]() { probe(); });
}

void NvrStatusRuntime::probe() {
    if (probing_) return;             // 上一次探测未收敛，等超时或结果，不并发
    QString problem;
    NvrSettings settings;
    if (!loadNvrSettings(configFile_, &settings, &problem)) {
        endpoint_.clear();
        setConnected(false, QStringLiteral("nvr config invalid: ") + problem);
        return;
    }
    if (!settings.enabled) {
        endpoint_.clear();
        setConnected(false, QStringLiteral("nvr disabled in config"));
        return;
    }
    const QString host = settings.address.host();
    const int port = settings.address.port(
        settings.address.scheme() == QLatin1String("https") ? 443 : 80);
    if (host.isEmpty() || port <= 0 || port > 65535) {
        endpoint_.clear();
        setConnected(false, QStringLiteral("nvr address invalid"));
        return;
    }
    endpoint_ = host + QLatin1Char(':') + QString::number(port);
    probing_ = true;
    timeoutTimer_->start(kConnectTimeoutMs);
    socket_->connectToHost(host, static_cast<quint16>(port));
}

void NvrStatusRuntime::finish(bool ok, const QString& reason) {
    if (!probing_) return;
    probing_ = false;
    timeoutTimer_->stop();
    socket_->abort();                 // 探通即断开，不占用 NVR 连接数
    setConnected(ok, reason);
}

void NvrStatusRuntime::setConnected(bool value, const QString& reason) {
    if (value == connected_) return;   // 只在跃迁时通知，避免每 15s 重建顶栏
    connected_ = value;
    railhmi::platform::Logger::instance().info(
        "nvr-status: " + endpoint_.toStdString() + " -> " +
        (connected_ ? "NVR正常" : "NVR异常") + " (" + reason.toStdString() + ")");
    emit changed(connected_);
}
