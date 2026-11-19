#include "door_sensor_worker.h"

#include <QByteArray>
#include <QList>
#include <QDBusArgument>
#include <QDBusObjectPath>
#include <QDBusVariant>
#include <QElapsedTimer>
#include <QMetaType>
#include <QSocketNotifier>
#include <QThread>
#include <QTimer>
#include <QVariant>
#include <QtGlobal>

#include <algorithm>
#include <cstdint>

#include "hci_scan_filter.h"
#include "railhmi/core/door_sensor.h"

namespace {

using railhmi::core::kDoorVendorCompany;

constexpr unsigned char kHciEventPacket = 0x04;
constexpr unsigned char kLeMetaEvent = 0x3e;
constexpr unsigned char kLeAdvReport = 0x02;   // legacy LE Advertising Report
constexpr unsigned char kAdTypeNameShort = 0x08;
constexpr unsigned char kAdTypeNameFull = 0x09;
constexpr unsigned char kAdTypeManufacturer = 0xFF;

QByteArray variantToBytes(const QVariant& v) {
    if (!v.isValid()) return QByteArray();
    if (v.userType() == QMetaType::QByteArray) return v.toByteArray();
    if (v.userType() == qMetaTypeId<QDBusVariant>())
        return qvariant_cast<QDBusVariant>(v).variant().toByteArray();
    if (v.userType() == QMetaType::QVariantList) {
        QByteArray out;
        for (const QVariant& item : v.toList())
            out.append(static_cast<char>(item.toUInt() & 0xFF));
        return out;
    }
    return v.toByteArray();
}

bool isArgument(const QVariant& v, const char* signature) {
    return v.isValid() && QString::fromLatin1(v.typeName()) == QLatin1String("QDBusArgument") &&
           v.value<QDBusArgument>().currentSignature() == QLatin1String(signature);
}

// 取广播里可能承载厂商 31 字节报文的字段，并标记是否见到已确认的 0xFF07 厂商 id。
QList<QByteArray> adPayloads(const QVariantMap& props, bool* vendorSeen) {
    QList<QByteArray> out;
    if (vendorSeen) *vendorSeen = false;
    const QVariant mfr = props.value(QStringLiteral("ManufacturerData"));
    auto addVendorCandidate = [&](ushort company, const QByteArray& b) {
        if (b.isEmpty()) return;
        out.append(b);
        // 现场 BWD1 把 0x07 放在 manufacturer id 的低字节（company=0xFF07）。
        // BlueZ 剥掉了这两个字节，这里仅为该厂商补回小端公司 id 拼接候选。
        if (company == static_cast<ushort>(kDoorVendorCompany)) {
            if (vendorSeen) *vendorSeen = true;
            QByteArray withCompany;
            withCompany.append(static_cast<char>(company & 0xFF));
            withCompany.append(static_cast<char>((company >> 8) & 0xFF));
            withCompany.append(b);
            out.append(withCompany);
        }
    };
    if (isArgument(mfr, "a{qv}")) {
        const QDBusArgument a = mfr.value<QDBusArgument>();
        a.beginMap();
        while (!a.atEnd()) {
            a.beginMapEntry();
            ushort company = 0;
            QVariant val;
            a >> company >> val;
            a.endMapEntry();
            addVendorCandidate(company, variantToBytes(val));
        }
        a.endMap();
    } else if (mfr.userType() == QMetaType::QVariantMap) {
        // 部分 Qt/BlueZ 组合把 a{qv} 直接展开成 QVariantMap（键为十进制 company id）。
        const QVariantMap direct = mfr.toMap();
        for (auto it = direct.constBegin(); it != direct.constEnd(); ++it)
            addVendorCandidate(static_cast<ushort>(it.key().toUShort()), variantToBytes(it.value()));
    }
    const QVariant svc = props.value(QStringLiteral("ServiceData"));
    if (isArgument(svc, "a{sv}")) {
        const QDBusArgument a = svc.value<QDBusArgument>();
        a.beginMap();
        while (!a.atEnd()) {
            a.beginMapEntry();
            QString uuid;
            QVariant val;
            a >> uuid >> val;
            a.endMapEntry();
            Q_UNUSED(uuid);
            const QByteArray b = variantToBytes(val);
            if (!b.isEmpty()) out.append(b);
        }
        a.endMap();
    }
    return out;
}

// BlueZ Device1 对象路径固定为 /org/bluez/hciX/dev_XX_XX_XX_XX_XX_XX，
// 推导 MAC 后即使对象被 BlueZ 重建、或还没被全量轮询收录，也能立即识别。
QString macFromPath(const QString& path) {
    const int i = path.lastIndexOf(QStringLiteral("/dev_"));
    if (i < 0) return QString();
    QString tail = path.mid(i + 5);
    tail.replace(QLatin1Char('_'), QLatin1Char(':'));
    return tail.toUpper();
}

QString byteHex(unsigned char v) {
    return QStringLiteral("%1").arg(static_cast<unsigned int>(v), 2, 16, QLatin1Char('0')).toUpper();
}

}  // namespace

DoorSensorWorker::DoorSensorWorker(const railhmi::core::DoorSensorConfig& cfg, QObject* parent)
    : QObject(parent), cfg_(cfg), bus_(QStringLiteral("railhmi-door-unconnected")) {}

DoorSensorWorker::~DoorSensorWorker() {
    if (advNotifier_) advNotifier_->setEnabled(false);
    if (advFd_ >= 0) {
        railhmi::disableBleScan(advFd_);
        railhmi::closeBleAdvSocket(advFd_);
        advFd_ = -1;
    }
    QDBusConnection::disconnectFromBus(QStringLiteral("railhmi-door-worker"));
}

bool DoorSensorWorker::isCandidate(const QString& mac, const QString& name) const {
    const QString key = mac.toUpper();
    for (const auto& d : cfg_.devices)
        if (QString::fromStdString(d.mac).toUpper() == key) return true;
    if (!cfg_.auto_discover || cfg_.name_filter.empty()) return false;
    return name.startsWith(QString::fromStdString(cfg_.name_filter), Qt::CaseInsensitive);
}

QVariantMap DoorSensorWorker::makeFrame(const QString& objectPath, const QVariantMap& props,
                                        bool forceFresh) const {
    bool vendorSeen = false;
    const QList<QByteArray> payloads = adPayloads(props, &vendorSeen);
    QVariantList bytes;
    for (const QByteArray& b : payloads) bytes.append(QVariant(b));

    const QString addr = props.value(QStringLiteral("Address")).toString().toUpper();
    QVariantMap f;
    f[QStringLiteral("path")] = objectPath;
    f[QStringLiteral("mac")] = addr.isEmpty() ? macFromPath(objectPath) : addr;
    f[QStringLiteral("name")] = props.value(QStringLiteral("Name")).toString();
    f[QStringLiteral("rssi")] = props.value(QStringLiteral("RSSI")).toInt();
    f[QStringLiteral("payloads")] = bytes;
    f[QStringLiteral("vendor")] = vendorSeen;
    f[QStringLiteral("forceFresh")] = forceFresh;
    return f;
}

// HCI 直读帧：构造与 BlueZ Device1 帧完全一致的形状，让 GUI 侧解析器复用。
QVariantMap DoorSensorWorker::makeRawFrame(const QString& mac, const QString& name, int rssi,
                                           const QByteArray& vendorValue) const {
    QVariantList payloads;
    payloads.append(QVariant(vendorValue));            // 原始厂商字段（07 FF 之后）
    QByteArray withCompany;
    withCompany.append(static_cast<char>(kDoorVendorCompany & 0xFF));         // 0x07
    withCompany.append(static_cast<char>((kDoorVendorCompany >> 8) & 0xFF)); // 0xFF
    withCompany.append(vendorValue);
    payloads.append(QVariant(withCompany));            // core 宽松解析所需的拼回帧

    QVariantMap f;
    f[QStringLiteral("path")] = QStringLiteral("hci:/") + mac;
    f[QStringLiteral("mac")] = mac;
    f[QStringLiteral("name")] = name;
    f[QStringLiteral("rssi")] = rssi;
    f[QStringLiteral("payloads")] = payloads;
    f[QStringLiteral("vendor")] = true;
    f[QStringLiteral("forceFresh")] = true;
    return f;
}

void DoorSensorWorker::init() {
    // 主路径优先：HCI 原始通道不依赖 BlueZ 的发现状态与 D-Bus 推送节流。
    startRawHci();
}

bool DoorSensorWorker::setupRawChannel(int fd, int* failRc) {
    const int paramsRc = railhmi::configureBleScan(fd);
    const int enableRc = railhmi::enableBleScanNoDup(fd);
    if (enableRc != 0) {
        if (failRc) *failRc = enableRc;
        emit info(QStringLiteral("warning: LE Set Scan Enable(filter_dup=0) failed (rc=%1)")
                      .arg(enableRc));
        railhmi::closeBleAdvSocket(fd);
        return false;
    }
    advFd_ = fd;
    rawMode_ = true;

    advNotifier_ = new QSocketNotifier(fd, QSocketNotifier::Read, this);
    connect(advNotifier_, &QSocketNotifier::activated, this, &DoorSensorWorker::onAdvData);

    // bluetoothd 约每 10s 重新下发 LE Set Scan Enable 并重新打开去重，
    // 2s 重发一次即可覆盖，且比 1s 温和，不干扰 BlueZ 的 mgmt 状态。
    rawScanTimer_ = new QTimer(this);
    rawScanTimer_->setInterval(2000);
    connect(rawScanTimer_, &QTimer::timeout, this, &DoorSensorWorker::refreshRawScan);
    rawScanTimer_->start();

    emit info(QStringLiteral("door-sensor raw HCI mode: LE Set Scan Parameters rc=%1, "
                             "filter_dup=0 enabled, 2s refresh (1s advertisements read directly)")
                  .arg(paramsRc));
    return true;
}

void DoorSensorWorker::startRawHci() {
    // 验证用：强制先走 BlueZ 兜底，用于验证「适配器晚就绪 -> 自动升级回 raw」的链路。
    // 默认关闭；生产不受影响。
    if (qEnvironmentVariableIsSet("RAILHMI_DOOR_FORCE_FALLBACK")) {
        emit info(QStringLiteral("door-sensor: RAILHMI_DOOR_FORCE_FALLBACK set, starting in BlueZ "
                                 "fallback (verification only)"));
        startBluezFallback();
        return;
    }
    const int fd = railhmi::openBleAdvSocket();
    if (fd < 0) {
        emit info(QStringLiteral("warning: raw HCI advertisement channel unavailable (rc=%1, "
                                 "need CAP_NET_ADMIN+CAP_NET_RAW); falling back to BlueZ discovery")
                      .arg(fd));
        startBluezFallback();
        return;
    }
    if (!setupRawChannel(fd)) {
        startBluezFallback();
        return;
    }
    clock_.start();
    emit ioMode(QStringLiteral("hci-raw"), QStringLiteral("hci0 (raw)"));
}

void DoorSensorWorker::tryUpgradeToRaw() {
    if (rawMode_) {
        if (rawRetryTimer_) rawRetryTimer_->stop();
        return;
    }
    // 裸 HCI 与 BlueZ discovery 互斥：先停发现，再开 raw；失败要把发现恢复回去，
    // 否则兜底路径也拿不到新设备（这正是开机抢跑时最容易踩的坑）。
    const QString adapter =
        adapterPath_.isEmpty() ? QStringLiteral("/org/bluez/hci0") : adapterPath_;
    if (bus_.isConnected()) {
        QDBusMessage stop = QDBusMessage::createMethodCall(
            QStringLiteral("org.bluez"), adapter, QStringLiteral("org.bluez.Adapter1"),
            QStringLiteral("StopDiscovery"));
        bus_.call(stop, QDBus::Block, 1200);
    }
    const int fd = railhmi::openBleAdvSocket();
    int failRc = fd;
    const bool ok = fd >= 0 && setupRawChannel(fd, &failRc);
    if (!ok) {
        if (fd >= 0) railhmi::closeBleAdvSocket(fd);
        if (!rawRetryLogged_) {
            rawRetryLogged_ = true;
            emit info(QStringLiteral("door-sensor: raw HCI still unavailable (open=%1, scanEnable=%2); "
                                     "keeping BlueZ fallback and retrying every 5s")
                          .arg(fd).arg(failRc));
        }
        if (bus_.isConnected() && filterApplied_) applyDiscovery(adapter);
        return;
    }
    if (rawRetryTimer_) rawRetryTimer_->stop();
    stopBluezSide();
    clock_.start();
    emit ioMode(QStringLiteral("hci-raw"), QStringLiteral("hci0 (raw)"));
    emit info(QStringLiteral("door-sensor: upgraded to raw HCI (adapter became ready after boot)"));
}

void DoorSensorWorker::stopBluezSide() {
    if (timer_) {
        timer_->stop();
        timer_->deleteLater();
        timer_ = nullptr;
    }
    if (bus_.isConnected()) {
        // 裸 HCI 与 BlueZ discovery 互斥：切回 raw 前先停掉发现，避免两边互相覆盖扫描参数。
        const QString path =
            adapterPath_.isEmpty() ? QStringLiteral("/org/bluez/hci0") : adapterPath_;
        QDBusMessage stop = QDBusMessage::createMethodCall(
            QStringLiteral("org.bluez"), path, QStringLiteral("org.bluez.Adapter1"),
            QStringLiteral("StopDiscovery"));
        bus_.call(stop, QDBus::Block, 1500);
    }
    if (subscribed_) {
        bus_.disconnect(QStringLiteral("org.bluez"), QString(),
                        QStringLiteral("org.freedesktop.DBus.Properties"),
                        QStringLiteral("PropertiesChanged"), this,
                        SLOT(onPropertiesChanged(QString,QVariantMap,QStringList,QDBusMessage)));
        subscribed_ = false;
    }
}

void DoorSensorWorker::refreshRawScan() {
    if (!rawMode_ || advFd_ < 0) return;
    railhmi::enableBleScanNoDup(advFd_);
}

void DoorSensorWorker::onAdvData() {
    if (advFd_ < 0) return;
    unsigned char buf[4096];
    for (;;) {
        const int n = railhmi::readBleAdvPacket(advFd_, buf, static_cast<int>(sizeof(buf)));
        if (n <= 0) break;
        int off = 0;
        while (off + 3 <= n) {
            if (buf[off] != kHciEventPacket) break;             // 过滤器只允许事件包
            const int plen = buf[off + 2];
            const int total = 3 + plen;
            if (off + total > n) break;
            if (buf[off + 1] == kLeMetaEvent && total >= 5 && buf[off + 3] == kLeAdvReport)
                parseAdvReports(buf + off + 4, total - 4);
            off += total;
        }
    }
}

void DoorSensorWorker::parseAdvReports(const unsigned char* data, int len) {
    if (len < 1) return;
    const int count = data[0];
    int off = 1;
    for (int i = 0; i < count && off + 10 <= len; ++i) {
        Q_UNUSED(data[off]);                              // event type: ADV_IND / SCAN_RSP
        QString mac;
        for (int k = 5; k >= 0; --k) {                   // 广播地址是小端序
            if (!mac.isEmpty()) mac += QLatin1Char(':');
            mac += byteHex(data[off + 2 + k]);
        }
        const int dlen = data[off + 8];
        if (off + 10 + dlen > len) break;
        const unsigned char* ad = data + off + 9;
        const int rssi = static_cast<int>(static_cast<signed char>(data[off + 9 + dlen]));
        off += 10 + dlen;

        QString name;
        QByteArray vendorValue;
        int q = 0;
        while (q + 1 < dlen) {
            const int fieldLen = ad[q];
            if (fieldLen == 0 || q + 1 + fieldLen > dlen) break;
            const int type = ad[q + 1];
            const unsigned char* value = ad + q + 2;
            const int valueLen = fieldLen - 1;
            if (type == kAdTypeNameShort || type == kAdTypeNameFull)
                name = QString::fromLatin1(reinterpret_cast<const char*>(value), valueLen);
            else if (type == kAdTypeManufacturer && valueLen >= 4) {
                const int company = value[0] | (value[1] << 8);
                if (company == static_cast<int>(kDoorVendorCompany))
                    vendorValue = QByteArray(reinterpret_cast<const char*>(value + 2), valueLen - 2);
            }
            q += 1 + fieldLen;
        }
        if (vendorValue.isEmpty() || !isCandidate(mac, name)) continue;
        if (!rawFrameLogged_) {
            rawFrameLogged_ = true;
            emit info(QStringLiteral("raw HCI advertisement received mac=%1 name=%2 rssi=%3dBm "
                                     "vendorPayload=%4")
                          .arg(mac, name).arg(rssi)
                          .arg(QString::fromLatin1(vendorValue.toHex())));
        }
        emit pushedFrame(makeRawFrame(mac, name, rssi, vendorValue));
    }
}

void DoorSensorWorker::startBluezFallback() {
    // 私有 system-bus 连接必须在**本工作线程**创建，才能把阻塞调用与信号投递
    // 全部留在线程内；GUI 线程从此不再有任何 D-Bus 等待。
    bus_ = QDBusConnection::connectToBus(QDBusConnection::SystemBus,
                                         QStringLiteral("railhmi-door-worker"));
    if (!bus_.isConnected()) {
        emit sourceError(QStringLiteral("NOT_READY"),
                         QStringLiteral("system DBus not connected in door worker"));
        return;
    }

    // 正确的接口名是 org.freedesktop.DBus.Properties（成员才是 PropertiesChanged）。
    subscribed_ = bus_.connect(
        QStringLiteral("org.bluez"), QString(), QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("PropertiesChanged"), QStringList(), QStringLiteral("sa{sv}as"), this,
        SLOT(onPropertiesChanged(QString,QVariantMap,QStringList,QDBusMessage)));
    emit info(subscribed_
                  ? QStringLiteral("PropertiesChanged subscribed on org.freedesktop.DBus.Properties (BlueZ fallback)")
                  : QStringLiteral("warning: PropertiesChanged subscription unavailable; relying on poll"));

    clock_.start();
    timer_ = new QTimer(this);
    timer_->setInterval(std::min(10000, std::max(1000, cfg_.poll_interval_ms)));
    connect(timer_, &QTimer::timeout, this, &DoorSensorWorker::poll);
    poll();                        // 立即取一次，界面启动即给出真值
    timer_->start();
    emit ioMode(QStringLiteral("bluez-fallback"), adapterPath_);

    // 开机时 hci0 可能比应用晚就绪（hciattach ~10s）：保留 BlueZ 兜底，同时每 5s
    // 重试 HCI 原始通道；一旦适配器就绪就自动切回 1~2s 的快路径，不必人工重启。
    if (!rawRetryTimer_) {
        rawRetryTimer_ = new QTimer(this);
        rawRetryTimer_->setInterval(5000);
        connect(rawRetryTimer_, &QTimer::timeout, this, &DoorSensorWorker::tryUpgradeToRaw);
    }
    rawRetryTimer_->start();
}

void DoorSensorWorker::onPropertiesChanged(const QString& interfaceName,
                                           const QVariantMap& changed,
                                           const QStringList& invalidated,
                                           const QDBusMessage& message) {
    Q_UNUSED(invalidated);
    if (interfaceName != QLatin1String("org.bluez.Device1")) return;
    const QString path = message.path();
    const QString mac = changed.value(QStringLiteral("Address")).toString().toUpper().isEmpty()
                            ? macFromPath(path)
                            : changed.value(QStringLiteral("Address")).toString().toUpper();
    const QString name = changed.value(QStringLiteral("Name")).toString();
    if (mac.isEmpty()) return;
    // QDBusArgument 是浅拷贝共享读游标，同一份 props 只能解包一次；
    // makeFrame 内部解包后会顺带给出 vendor 判定。
    const QVariantMap frame = makeFrame(path, changed, true);
    if (!frame.value(QStringLiteral("vendor")).toBool() && !isCandidate(mac, name)) return;
    emit pushedFrame(frame);
}

void DoorSensorWorker::applyDiscovery(const QString& adapterPath) {
    // 降级路径：BlueZ 默认去重重复广播、并清理长时间未上报的临时 Device 对象。
    // Transport=le + DuplicateData=true 让 BlueZ 不合并相同数据的上报。
    // 注意：这里绝不能同时下发裸 HCI 命令，两者会互相破坏发现状态。
    if (!filterApplied_) {
        QDBusMessage fm = QDBusMessage::createMethodCall(
            QStringLiteral("org.bluez"), adapterPath, QStringLiteral("org.bluez.Adapter1"),
            QStringLiteral("SetDiscoveryFilter"));
        QVariantMap filter;
        filter.insert(QStringLiteral("Transport"), QStringLiteral("le"));
        filter.insert(QStringLiteral("DuplicateData"), true);
        fm << filter;
        const QDBusMessage fr = bus_.call(fm, QDBus::Block, 3000);
        const QString ferr = fr.errorName();
        if (ferr.isEmpty()) filterApplied_ = true;
        if (!filterLogged_) {
            emit info(ferr.isEmpty()
                          ? QStringLiteral("SetDiscoveryFilter OK (Transport=le, DuplicateData=true) on ") + adapterPath
                          : QStringLiteral("SetDiscoveryFilter unavailable: ") + ferr +
                                QStringLiteral(" (will retry, continuing with default discovery)"));
            filterLogged_ = true;
        }
    }

    QDBusMessage m = QDBusMessage::createMethodCall(
        QStringLiteral("org.bluez"), adapterPath, QStringLiteral("org.bluez.Adapter1"),
        QStringLiteral("StartDiscovery"));
    const QDBusMessage r = bus_.call(m, QDBus::Block, 3000);
    const QString err = r.errorName();
    if (err.isEmpty() || err.endsWith(QStringLiteral("InProgress"))) {
        if (!discoveryLogged_) {
            emit info(QStringLiteral("StartDiscovery OK on ") + adapterPath +
                      (err.isEmpty() ? QString() : QStringLiteral(" (already scanning)")));
            discoveryLogged_ = true;
        }
    } else if (!discoveryLogged_) {
        emit info(QStringLiteral("StartDiscovery failed: ") + err);
        discoveryLogged_ = true;
    }
}

void DoorSensorWorker::parseManagedObjects(const QDBusMessage& reply) {
    if (reply.type() == QDBusMessage::ErrorMessage) {
        emit sourceError(QStringLiteral("SOURCE_ERROR"), reply.errorName());
        return;
    }
    if (reply.arguments().isEmpty() || reply.signature() != QLatin1String("a{oa{sa{sv}}}")) {
        emit sourceError(QStringLiteral("UNSUPPORTED"),
                         QStringLiteral("unexpected reply signature ") + reply.signature());
        return;
    }

    const QDBusArgument top = reply.arguments().at(0).value<QDBusArgument>();
    QString firstAdapter;
    int deviceCount = 0, matchCount = 0;
    QVariantList frames;
    top.beginMap();
    while (!top.atEnd()) {
        top.beginMapEntry();
        QDBusObjectPath path;
        top >> path;
        const QString objectPath = path.path();
        QVariantMap deviceProps;
        bool isDevice = false, isAdapter = false;
        top.beginMap();                                  // a{s a{sv}}
        while (!top.atEnd()) {
            top.beginMapEntry();
            QString iface;
            top >> iface;
            top.beginMap();                              // a{sv}
            QVariantMap p;
            while (!top.atEnd()) {
                top.beginMapEntry();
                QString key;
                QVariant val;
                top >> key >> val;
                top.endMapEntry();
                p.insert(key, val);
            }
            top.endMap();
            top.endMapEntry();
            if (iface == QLatin1String("org.bluez.Device1")) { isDevice = true; deviceProps = p; }
            if (iface == QLatin1String("org.bluez.Adapter1")) isAdapter = true;
        }
        top.endMap();
        top.endMapEntry();

        if (isAdapter && (firstAdapter.isEmpty() || objectPath == adapterPath_)) firstAdapter = objectPath;
        if (!isDevice) continue;
        ++deviceCount;
        const QString mac = deviceProps.value(QStringLiteral("Address")).toString().toUpper();
        const QString name = deviceProps.value(QStringLiteral("Name")).toString();
        if (mac.isEmpty() || !isCandidate(mac, name)) continue;
        ++matchCount;
        frames.append(makeFrame(objectPath, deviceProps, false));
    }
    top.endMap();

    adapterPath_ = cfg_.adapter.empty() ? firstAdapter
                                        : QStringLiteral("/org/bluez/") + QString::fromStdString(cfg_.adapter);
    emit scanResult(frames, adapterPath_, deviceCount, matchCount);

    // StartDiscovery 刷新按墙上时间（约 60s），不按轮询次数；1s 轮询下更不能写成「每 N 次」。
    const long long now = clock_.elapsed();
    if (!rawMode_ && !adapterPath_.isEmpty() && (now - lastDiscoveryMs_ >= 60000)) {
        lastDiscoveryMs_ = now;
        applyDiscovery(adapterPath_);
    }
}

void DoorSensorWorker::poll() {
    if (!bus_.isConnected()) return;
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.bluez"), QStringLiteral("/"),
        QStringLiteral("org.freedesktop.DBus.ObjectManager"), QStringLiteral("GetManagedObjects"));
    const QDBusMessage reply = bus_.call(msg, QDBus::Block, 3000);
    parseManagedObjects(reply);
}
