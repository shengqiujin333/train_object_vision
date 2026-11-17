#pragma once

// NVR 连接状态运行时（顶栏「录像」位 NVR正常/NVR异常 的唯一真值来源）。
// 现场要求：录像位按「与 NVR 的连接情况」显示，而不是按 nvr.yaml 是否存在、
// 也不是按回放页面是否登录过。这里只做**真实 TCP 连接探测**：
//   * 每次探测重新读 nvr.yaml（换地址/关开关无需重启），取 address 的 host/port；
//   * connectToHost 成功 -> NVR正常；拒绝/超时/地址无效/未启用/配置损坏 -> NVR异常；
//   * 不写任何业务请求、不登录、不查询、不修改 NVR，也不复用回放页的 NvrClient，
//     因此不会把 P3 的「正在连接/已连接」提示与顶栏状态互相污染。
// 语义边界：这里证明的是**链路可达**，不等于回放能力或录像完整性（见接口契约 §8）。
// 探测结果只在跃迁时打日志并发信号，15s 周期内不会反复刷新顶栏。

#include <QObject>
#include <QString>
#include <QTcpSocket>

#include "nvr_client.h"   // NvrSettings / loadNvrSettings

class QTimer;

class NvrStatusRuntime : public QObject {
    Q_OBJECT
public:
    explicit NvrStatusRuntime(const QString& configFile, QObject* parent = nullptr);

    bool connected() const { return connected_; }
    QString endpoint() const { return endpoint_; }

    // 立即发起一次真实连接探测（启动、定时器、外部复核都走同一入口）。
    Q_INVOKABLE void probe();

signals:
    void changed(bool connected);

private:
    void finish(bool ok, const QString& reason);
    void setConnected(bool value, const QString& reason);

    QString configFile_;
    QString endpoint_;
    QTcpSocket* socket_ = nullptr;
    QTimer* timeoutTimer_ = nullptr;
    QTimer* probeTimer_ = nullptr;
    bool connected_ = false;
    bool probing_ = false;
};
