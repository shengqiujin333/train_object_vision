#include "door_sensor_runtime.h"

#include "door_sensor_worker.h"

#include <QThread>
#include <QTimer>
#include <QVariant>
#include <QtGlobal>
#include <algorithm>
#include <vector>

#include "railhmi/platform/log.h"

namespace {

using railhmi::core::DoorFrame;
using railhmi::core::DoorSensorState;
using railhmi::core::DoorState;
using railhmi::core::StatusLevel;

void logLine(const QString& s) {
    railhmi::platform::Logger::instance().info("door-sensor: " + s.toStdString());
}

std::vector<std::uint8_t> toBytes(const QByteArray& b) {
    std::vector<std::uint8_t> out;
    out.reserve(static_cast<std::size_t>(b.size()));
    for (char c : b) out.push_back(static_cast<std::uint8_t>(static_cast<unsigned char>(c)));
    return out;
}

QByteArray hexDump(const QList<QByteArray>& payloads) {
    QByteArray h;
    for (const QByteArray& b : payloads) h.append(b.toHex());
    return h;
}

// adPayloads 只为 company=0xFF07 生成拼接候选，其特征是 company 小端 07 FF 开头。
// 只有这类已确认的厂商帧才允许启用 core 的受控宽松 Key 定位。
bool isVendorReassembledFrame(const QByteArray& b) {
    return b.size() >= 2 &&
           static_cast<unsigned char>(b.at(0)) ==
               static_cast<unsigned char>(railhmi::core::kDoorMarker) &&
           static_cast<unsigned char>(b.at(1)) == 0xFF;
}

QString levelToken(StatusLevel l) {
    return QString::fromLatin1(railhmi::core::to_string(l));
}

}  // namespace

QString door_reason_label(const std::string& token) {
    using namespace railhmi::core;
    // 空 reason 表示「已解析到合法厂商帧」，不要写成「正常」——门本身可能未关严，
    // 「正常」会被误读成门状态正常。这里只陈述核验结果。
    if (token.empty()) return QString::fromUtf8("已核验");
    if (token == kDoorReasonUnconfigured) return QString::fromUtf8("未配置");
    if (token == kDoorReasonNotReady) return QString::fromUtf8("蓝牙未就绪");
    if (token == kDoorReasonSourceError) return QString::fromUtf8("BlueZ 读取失败");
    if (token == kDoorReasonUnsupported) return QString::fromUtf8("广播格式不符");
    if (token == kDoorReasonUnverified) return QString::fromUtf8("未核验");
    if (token == kDoorReasonStale) return QString::fromUtf8("广播超时");
    return QString::fromStdString(token);
}

DoorSensorRuntime::DoorSensorRuntime(const railhmi::core::DoorSensorConfig& cfg, QObject* parent)
    : QObject(parent), cfg_(cfg) {
    clock_.start();
    seedRegistered();
    // 1s 只重算「N 秒前」/超时，不触碰 D-Bus；门状态变化由 worker 事件推送即时到达。
    ageTimer_ = new QTimer(this);
    ageTimer_->setInterval(1000);
    connect(ageTimer_, &QTimer::timeout, this, &DoorSensorRuntime::refreshAges);
    ageTimer_->start();
}

DoorSensorRuntime::~DoorSensorRuntime() {
    if (thread_) {
        thread_->quit();
        thread_->wait(5000);
    }
}

const railhmi::core::DoorSensorDeviceConfig* DoorSensorRuntime::registeredFor(
    const QString& mac) const {
    const QString key = mac.toUpper();
    for (const auto& d : cfg_.devices)
        if (QString::fromStdString(d.mac).toUpper() == key) return &d;
    return nullptr;
}

bool DoorSensorRuntime::isCandidate(const QString& mac, const QString& name) const {
    if (registeredFor(mac)) return true;
    if (!cfg_.auto_discover || cfg_.name_filter.empty()) return false;
    // 使用说明：nRF Connect 搜到名称 T1 表示设备进入工作状态；这里把广播名作为
    // 发现闸门，避免把周围任意 BLE 设备（耳机/电脑/手机）误当成门磁。
    const QString filter = QString::fromStdString(cfg_.name_filter);
    return name.startsWith(filter, Qt::CaseInsensitive);
}

void DoorSensorRuntime::seedRegistered() {
    for (const auto& d : cfg_.devices) {
        Entry e;
        e.state.id = d.id;
        e.state.name = d.name.empty() ? d.id : d.name;
        e.state.mac = d.mac;
        e.state.source = "registered";
        e.state.verified = true;          // 显式登记 MAC 为权威来源
        e.state.door = DoorState::Unknown;
        e.state.reason = railhmi::core::kDoorReasonUnverified;  // 尚未收到可核验广播
        entries_[QString::fromStdString(d.mac).toUpper()] = e;
    }
}

void DoorSensorRuntime::start() {
    if (!cfg_.enabled) {
        source_ = "disabled";
        logLine(QStringLiteral("disabled by config (app.door_sensor.enabled=false) -> 未接入"));
        rebuildSnapshots();
        return;
    }
    source_ = "bluez";
    logLine(QStringLiteral("enabled adapter=%1 poll=%2ms stale=%3ms filter=%4 registered=%5 (I/O on dedicated worker thread)")
                .arg(cfg_.adapter.empty() ? QStringLiteral("auto") : QString::fromStdString(cfg_.adapter))
                .arg(std::min(10000, std::max(1000, cfg_.poll_interval_ms))).arg(cfg_.stale_ms)
                .arg(QString::fromStdString(cfg_.name_filter))
                .arg(cfg_.devices.size()));

    injectSimulationFrame();
    if (!qgetenv("RAILHMI_DOOR_SIM_FOLLOWUP").isEmpty()) {
        int delay = qgetenv("RAILHMI_DOOR_SIM_FOLLOWUP_MS").toInt();
        if (delay <= 0) delay = 4000;
        QTimer::singleShot(delay, this, &DoorSensorRuntime::injectFollowupFrame);
    }

    // 独立 BLE I/O 线程：所有阻塞式 BlueZ 调用与信号订阅都在 worker 内完成，
    // GUI 线程只接收纯 QVariant 帧。Qt 自动用 QueuedConnection 跨线程投递。
    thread_ = new QThread(this);
    thread_->setObjectName(QStringLiteral("railhmi-door-io"));
    worker_ = new DoorSensorWorker(cfg_);
    worker_->moveToThread(thread_);
    connect(thread_, &QThread::started, worker_, &DoorSensorWorker::init);
    connect(worker_, &DoorSensorWorker::scanResult,
            this, &DoorSensorRuntime::onScanResult, Qt::QueuedConnection);
    connect(worker_, &DoorSensorWorker::pushedFrame,
            this, &DoorSensorRuntime::onPushedFrame, Qt::QueuedConnection);
    connect(worker_, &DoorSensorWorker::sourceError,
            this, &DoorSensorRuntime::onSourceError, Qt::QueuedConnection);
    connect(worker_, &DoorSensorWorker::info,
            this, &DoorSensorRuntime::onWorkerInfo, Qt::QueuedConnection);
    connect(worker_, &DoorSensorWorker::ioMode,
            this, &DoorSensorRuntime::onIoMode, Qt::QueuedConnection);
    connect(thread_, &QThread::finished, worker_, &QObject::deleteLater);
    thread_->start();
}

void DoorSensorRuntime::onWorkerInfo(const QString& text) {
    logLine(text);
}

void DoorSensorRuntime::onIoMode(const QString& mode, const QString& adapter) {
    ioMode_ = mode;
    if (source_ != "error") source_ = mode;
    adapterPath_ = adapter;
    logLine(QStringLiteral("I/O mode=%1 adapter=%2").arg(mode, adapter));
    rebuildSnapshots();
}

void DoorSensorRuntime::onSourceError(const QString& reason, const QString& detail) {
    setSourceError(reason, detail);
    rebuildSnapshots();
}

void DoorSensorRuntime::onScanResult(const QVariantList& frames, const QString& adapterPath,
                                     int deviceCount, int matchCount) {
    ++pollCount_;
    adapterPath_ = adapterPath;
    scanDevices_ = deviceCount;
    scanMatched_ = matchCount;
    if (source_ == "error") { source_ = "bluez"; lastError_.clear(); }
    const long long now = nowMs();
    for (const QVariant& v : frames) {
        const QVariantMap f = v.toMap();
        const QString mac = f.value(QStringLiteral("mac")).toString().toUpper();
        const QString name = f.value(QStringLiteral("name")).toString();
        if (mac.isEmpty()) continue;
        auto it = entries_.find(mac);
        if (it == entries_.end()) {
            Entry e;
            e.state.id = "AUTO-" + mac.toStdString();
            e.state.name = name.toStdString();
            e.state.mac = mac.toStdString();
            e.state.source = registeredFor(mac) ? "registered" : "discovered";
            e.state.verified = (registeredFor(mac) != nullptr);
            e.state.door = DoorState::Unknown;
            e.state.reason = railhmi::core::kDoorReasonUnverified;
            it = entries_.emplace(mac, e).first;
        }
        applyFrame(it->second, f, now);
    }
    if (qEnvironmentVariableIsSet("RAILHMI_DOOR_DEBUG"))
        logLine(QStringLiteral("scan objects(devices=%1 matched=%2) adapter=%3 poll#%4")
                    .arg(deviceCount).arg(matchCount).arg(adapterPath).arg(pollCount_));
    rebuildSnapshots();
}

void DoorSensorRuntime::onPushedFrame(const QVariantMap& frame) {
    // 事件驱动主路径：HCI 直读每收到一个 T1 广播（实测 1~2s 一帧）即刻推送，
    // 不等 BlueZ 缓存更新、不等下一次全量轮询。开门 -> 顶栏/报警的延迟
    // 只剩「传感器把新状态写进广播」的时间。
    const QString mac = frame.value(QStringLiteral("mac")).toString().toUpper();
    const QString name = frame.value(QStringLiteral("name")).toString();
    if (mac.isEmpty()) return;
    ++pushEvents_;
    lastPushMs_ = nowMs();
    auto it = entries_.find(mac);
    if (it == entries_.end()) {
        if (!isCandidate(mac, name) && !frame.value(QStringLiteral("vendor")).toBool()) return;
        Entry e;
        e.state.id = "AUTO-" + mac.toStdString();
        e.state.name = name.toStdString();
        e.state.mac = mac.toStdString();
        e.state.source = "discovered";
        e.state.door = DoorState::Unknown;
        e.state.reason = railhmi::core::kDoorReasonUnverified;
        it = entries_.emplace(mac, e).first;
    }
    applyFrame(it->second, frame, lastPushMs_);
    // HCI 直读下 1~2s 就有一帧，避免日志被刷爆：前 3 次全打，之后每 20 次打一行；
    // RAILHMI_DOOR_DEBUG=1 时逐帧打印，便于板端测量端到端延迟。
    const bool verbose = qEnvironmentVariableIsSet("RAILHMI_DOOR_DEBUG");
    if (verbose || pushEvents_ <= 3 || pushEvents_ % 20 == 0)
        logLine(QStringLiteral("EVENT push#%1 mac=%2 (arrived immediately, no poll wait)")
                    .arg(pushEvents_).arg(mac));
    rebuildSnapshots();
}

void DoorSensorRuntime::refreshAges() {
    if (entries_.empty()) return;
    rebuildSnapshots();
}

void DoorSensorRuntime::injectSimulationFrame() {
    const QByteArray hex = qgetenv("RAILHMI_DOOR_SIM_FRAME");
    if (hex.isEmpty()) return;
    const QByteArray bytes = QByteArray::fromHex(hex);
    if (bytes.isEmpty()) {
        logLine(QStringLiteral("warning: RAILHMI_DOOR_SIM_FRAME is not valid hex, ignored"));
        return;
    }
    logLine(QStringLiteral("SYNTHETIC test frame injected (origin=simulation, NOT a real device): ") +
            QString::fromLatin1(bytes.toHex()));
    const QString mac = QStringLiteral("SIM:00:00:00:00:01");
    Entry& e = entries_[mac];
    e.state.id = QStringLiteral("SIM-T1").toStdString();
    e.state.name = "T1 (仿真)";
    e.state.mac = mac.toStdString();
    e.state.source = "simulation";
    e.state.door = DoorState::Unknown;
    QList<QByteArray> payloads;
    payloads.append(bytes);
    applyPayloads(e, payloads, -55, QStringLiteral("T1"), mac, nowMs(), bytes.toHex());
}

// 回归用：厂商协议的门磁 Key 0x08 是「状态变化立即上报」，电池 Key 0x63 约 5 分钟
// 一次，BlueZ 也会单独推 RSSI 更新。因此真实运行中一定会出现不带门磁 Key 的后续帧。
// 这里显式注入这类帧，验证已核验设备的门状态不会被抹成未知、也不会被打回未核验。
void DoorSensorRuntime::injectFollowupFrame() {
    const QByteArray spec = qgetenv("RAILHMI_DOOR_SIM_FOLLOWUP");
    if (spec.isEmpty()) return;
    const QString mac = QStringLiteral("SIM:00:00:00:00:01");
    auto it = entries_.find(mac);
    if (it == entries_.end()) return;             // 没有首帧就没有可回归的已核验状态
    Entry& e = it->second;
    QList<QByteArray> payloads;
    QByteArray fp;
    if (spec.trimmed().toUpper() == "RSSI_ONLY") {
        logLine(QStringLiteral("SYNTHETIC follow-up: RSSI-only frame, no manufacturer payload"));
        fp = "RSSI_ONLY";
    } else {
        const QByteArray bytes = QByteArray::fromHex(spec);
        if (bytes.isEmpty()) {
            logLine(QStringLiteral("warning: RAILHMI_DOOR_SIM_FOLLOWUP is not valid hex, ignored"));
            return;
        }
        logLine(QStringLiteral("SYNTHETIC follow-up frame injected: ") +
                QString::fromLatin1(bytes.toHex()));
        payloads.append(bytes);
        fp = bytes.toHex();
    }
    applyPayloads(e, payloads, -57, QString(), mac, nowMs(), fp);
    rebuildSnapshots();
}

void DoorSensorRuntime::setSourceError(const QString& reason, const QString& detail) {
    source_ = "error";
    lastError_ = reason + ": " + detail;
    logLine(QStringLiteral("ERROR ") + lastError_);
    for (auto& kv : entries_) {
        if (kv.second.state.source == "simulation") continue;   // 显式仿真输入不受 BlueZ 故障影响
        kv.second.state.reason = reason.toStdString();
    }
}

void DoorSensorRuntime::applyFrame(Entry& e, const QVariantMap& frame, long long now) {
    const QString mac = frame.value(QStringLiteral("mac")).toString().toUpper();
    const QString name = frame.value(QStringLiteral("name")).toString();
    const int rssi = frame.value(QStringLiteral("rssi")).toInt();
    const bool forceFresh = frame.value(QStringLiteral("forceFresh")).toBool();
    QList<QByteArray> payloads;
    const QVariantList bytes = frame.value(QStringLiteral("payloads")).toList();
    for (const QVariant& v : bytes) payloads.append(v.toByteArray());
    QByteArray fp;
    fp.append(name.toUtf8());
    fp.append('|');
    fp.append(QString::number(rssi).toUtf8());
    fp.append('|');
    fp.append(hexDump(payloads));
    applyPayloads(e, payloads, rssi, name, mac, now, fp);
    if (forceFresh) e.state.last_seen_ms = now;
}

void DoorSensorRuntime::applyPayloads(Entry& e, const QList<QByteArray>& payloads, int rssi,
                                      const QString& name, const QString& mac, long long now,
                                      const QByteArray& fingerprint) {
    const bool changed = (fingerprint != e.fingerprint);
    if (changed) e.fingerprint = fingerprint;
    if (changed || e.state.last_seen_ms == 0) e.state.last_seen_ms = now;

    e.state.mac = mac.toStdString();
    if (!name.isEmpty()) e.state.name = name.toStdString();
    e.state.rssi = rssi;

    railhmi::core::DoorFrameFormat fmt;
    fmt.payload_offset = cfg_.payload_offset;
    bool valid = false;
    QByteArray decodedHex;
    for (const QByteArray& b : payloads) {
        const std::vector<std::uint8_t> bytes = toBytes(b);
        DoorFrame f = railhmi::core::parse_door_frame(bytes, fmt);
        if (!f.valid && isVendorReassembledFrame(b)) {
            // 现场 BWD1 固件的 <Key,Value> 报文前有 FF FF FF 06 00 头部，
            // 严格按 PDF 的 byte5 布局解析会失败。仅对已由 company id 确认、
            // 且拼回 0x07 帧首的厂商候选启用有限窗口内的宽松 Key 定位。
            railhmi::core::DoorFrameFormat relaxed = fmt;
            relaxed.vendor_tolerant = true;
            f = railhmi::core::parse_door_frame(bytes, relaxed);
        }
        if (!f.valid) continue;
        valid = true;
        decodedHex = b.toHex();
        e.state.device_id = f.device_id;
        if (f.has_door) e.state.door = f.door;
        if (f.has_battery) { e.state.has_battery = true; e.state.battery_pct = f.battery_pct; }
        break;
    }
    if (valid) {
        // 合法厂商帧本身即身份核验（或 MAC 已登记）。只覆盖本帧实际携带的字段，
        // 门状态/电量沿用上一次有效广播，避免电池帧或纯 RSSI 帧把状态抹成未知。
        e.ever_verified = true;
        e.state.verified = true;
        e.state.reason.clear();
    } else if (!e.ever_verified) {
        e.state.reason = railhmi::core::kDoorReasonUnverified;
    }
    // 验收用：帧内容变化时打印实际解码的广播字节，证明数据来自真实设备而非仿真。
    if (valid && qEnvironmentVariableIsSet("RAILHMI_DOOR_DEBUG") &&
        decodedHex != e.lastFrameHex) {
        logLine(QStringLiteral("decoded vendor frame mac=%1 hex=%2 door=%3 battery=%4")
                    .arg(mac, QString::fromLatin1(decodedHex))
                    .arg(QString::fromLatin1(railhmi::core::door_state_token(e.state.door)))
                    .arg(e.state.battery_pct));
    }
    e.lastFrameHex = decodedHex;
    // ever_verified 但本帧无有效载荷时：保留 reason（通常为空）与上一次门状态，
    // 新鲜度仍由 last_seen_ms / PropertiesChanged 判定，超时会由 rebuildSnapshots 置 STALE。

    const bool fresh = e.state.last_seen_ms > 0 &&
        (now - e.state.last_seen_ms) <= static_cast<long long>(cfg_.stale_ms);
    e.state.online = fresh;
    if (!fresh) e.state.reason = railhmi::core::kDoorReasonStale;
    else if (e.ever_verified && e.state.reason == railhmi::core::kDoorReasonStale)
        e.state.reason.clear();   // 又收到广播，超时标记失效；避免「0 秒前 · 广播超时」自相矛盾
}

void DoorSensorRuntime::rebuildSnapshots() {
    const long long now = nowMs();
    std::vector<DoorSensorState> states;
    QVariantList rows;
    int doorUnknown = 0;
    for (auto& kv : entries_) {
        Entry& e = kv.second;
        const bool fresh = e.state.last_seen_ms > 0 &&
            (now - e.state.last_seen_ms) <= static_cast<long long>(cfg_.stale_ms);
        e.state.online = fresh;
        if (!fresh && e.state.reason != railhmi::core::kDoorReasonSourceError)
            e.state.reason = railhmi::core::kDoorReasonStale;
        states.push_back(e.state);
        // 现场 BWD1 常态广播只带电池 Key 0x63；门磁 Key 0x08 按文档「状态变化
        // 立即上报」。统计「在线但门态未知」的数量，供维护页明示这不是接入故障。
        if (fresh && e.state.door == DoorState::Unknown) ++doorUnknown;

        const bool lowBattery = e.state.has_battery && e.state.battery_pct >= 0 &&
                                e.state.battery_pct <= cfg_.battery_low_pct;
        StatusLevel level = StatusLevel::Unavailable;
        if (fresh && e.state.verified && e.state.reason.empty()) {
            if (e.state.door == DoorState::Ajar) level = StatusLevel::Alarm;
            else if (lowBattery) level = StatusLevel::Warning;
            else if (e.state.door == DoorState::Closed) level = StatusLevel::Ok;
        }
        QVariantMap m;
        m["id"] = QString::fromStdString(e.state.id);
        m["mac"] = QString::fromStdString(e.state.mac);
        m["name"] = QString::fromUtf8(e.state.name.c_str());
        m["deviceId"] = QString::number(e.state.device_id, 16).toUpper().rightJustified(8, QLatin1Char('0'));
        m["door"] = QString::fromLatin1(railhmi::core::door_state_token(e.state.door));
        m["doorLabel"] = QString::fromUtf8(railhmi::core::door_state_label(e.state.door));
        m["batteryPct"] = e.state.battery_pct;
        m["batteryText"] = e.state.has_battery ? QString::number(e.state.battery_pct) + "%"
                                               : QString::fromUtf8("—");
        m["rssi"] = e.state.rssi;
        m["lastSeenText"] = e.state.last_seen_ms == 0 ? QString::fromUtf8("从未")
            : QString::number((now - e.state.last_seen_ms) / 1000) + QString::fromUtf8(" 秒前");
        m["online"] = fresh;
        m["verified"] = e.state.verified;
        m["reason"] = QString::fromStdString(e.state.reason);
        m["reasonLabel"] = door_reason_label(e.state.reason);
        m["source"] = QString::fromStdString(e.state.source);
        m["lowBattery"] = lowBattery;
        m["level"] = levelToken(level);
        rows.append(m);

        // 由「非未关严」跃迁到「未关严」时产生一条 L2 报警（不重复、不删除历史事件）。
        const bool ajarNow = fresh && e.state.verified && e.state.reason.empty() &&
                             e.state.door == DoorState::Ajar;
        if (ajarNow && !e.was_ajar) {
            const QString desc = QString::fromUtf8("车门未关严（") +
                                 QString::fromStdString(e.state.mac) + QStringLiteral("）");
            emit doorAjar(QString::fromStdString(e.state.id), desc);
        }
        e.was_ajar = ajarNow;
    }

    const railhmi::core::DoorFleetSummary sum = railhmi::core::summarize_door_sensors(
        states, now, cfg_.stale_ms, cfg_.battery_low_pct);
    QVariantMap s;
    s["enabled"] = cfg_.enabled;
    s["level"] = levelToken(sum.level);
    s["value"] = QString::fromUtf8(sum.value.c_str());
    s["alarmText"] = QString::fromUtf8(sum.alarm_text.c_str());
    s["total"] = sum.total;
    s["online"] = sum.online;
    s["ajar"] = sum.ajar;
    s["unknown"] = sum.unknown;
    s["doorUnknown"] = doorUnknown;      // 已接入但尚未收到门磁状态帧的设备数
    s["lowBattery"] = sum.low_battery;
    s["source"] = source_;
    s["adapter"] = adapterPath_;
    s["lastError"] = lastError_;
    s["nameFilter"] = QString::fromStdString(cfg_.name_filter);
    s["staleMs"] = cfg_.stale_ms;
    // 现场验收用：证明门磁已进入「事件驱动 + 独立 I/O 线程 + 1s 高频读取」。
    s["pollReads"] = pollCount_;
    s["pushEvents"] = pushEvents_;
    s["lastPushAgeMs"] = lastPushMs_ < 0 ? -1 : now - lastPushMs_;
    s["eventDriven"] = cfg_.enabled;
    s["ioThread"] = QStringLiteral("railhmi-door-io");
    s["reason"] = cfg_.enabled ? QString::fromUtf8(source_ == "error" ? railhmi::core::kDoorReasonSourceError
                                                   : (sum.total == 0 ? railhmi::core::kDoorReasonUnverified : ""))
                               : QString::fromUtf8(railhmi::core::kDoorReasonUnconfigured);
    s["reasonLabel"] = door_reason_label(s["reason"].toString().toStdString());

    const bool differ = (rows != devices_) || (s != summary_);
    // RAILHMI_DOOR_DEBUG=1 时在**快照发生变化**时打印真值（含空表），便于板端验收，
    // 正常轮询不刷屏。
    if (qEnvironmentVariableIsSet("RAILHMI_DOOR_DEBUG") && differ)
        logLine(QStringLiteral("snapshot level=%1 value='%2' total=%3 online=%4 ajar=%5 unknown=%6 "
                               "lowBattery=%7 source=%8 reason=%9 pushes=%10 polls=%11")
                    .arg(s["level"].toString(), s["value"].toString())
                    .arg(sum.total).arg(sum.online).arg(sum.ajar).arg(sum.unknown)
                    .arg(sum.low_battery).arg(source_, s["reason"].toString())
                    .arg(pushEvents_).arg(pollCount_));

    devices_ = rows;
    summary_ = s;
    if (differ) emit changed();
}
