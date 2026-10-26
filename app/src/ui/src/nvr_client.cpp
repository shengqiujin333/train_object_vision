#include "nvr_client.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include "railhmi/core/yaml.h"

static bool validPlaybackUrl(const QUrl& url, const NvrSettings& settings) {
    return url.isValid() && (url.scheme() == "rtsp" || url.scheme() == "rtsps") &&
        !url.host().isEmpty() && url.host().compare(settings.address.host(), Qt::CaseInsensitive) == 0 &&
        url.userInfo().isEmpty() && !url.hasFragment();
}

bool loadNvrSettings(const QString& file, NvrSettings* out, QString* error) {
    QFile input(file);
    if (!input.exists()) { *out = NvrSettings{}; return true; }
    if (!input.open(QIODevice::ReadOnly)) { *error = "无法读取 nvr.yaml"; return false; }
    railhmi::core::yaml::Node root;
    std::string problem;
    if (!railhmi::core::yaml::parse(input.readAll().toStdString(), &root, &problem)) {
        *error = QString::fromStdString(problem); return false;
    }
    const auto* node = root.get("nvr");
    if (!node || !node->is_map()) { *error = "nvr.yaml 缺少 nvr 配置"; return false; }
    NvrSettings result;
    auto scalar = [&](const char* key, const QString& fallback) {
        const auto* value = node->get(key);
        return value && value->is_scalar() ? QString::fromStdString(value->scalar) : fallback;
    };
    result.enabled = scalar("enabled", "false") == "true";
    result.address = QUrl(scalar("address", ""));
    result.apiPrefix = scalar("api_prefix", result.apiPrefix);
    result.username = scalar("username", result.username);
    result.credentialEnv = scalar("credential_env", result.credentialEnv);
    result.timeZone = QTimeZone(scalar("timezone", "Asia/Shanghai").toUtf8());
    const QString mode = scalar("timestamp_mode", "local");
    result.localEpoch = mode == "local";
    bool timeoutOk = false, baseOk = false;
    result.timeoutMs = scalar("timeout_ms", "8000").toInt(&timeoutOk);
    result.responseChannelBase = scalar("response_channel_base", "0").toInt(&baseOk);
    result.playbackUrlTemplate = scalar("playback_url_template", "");
    const auto playbackTransport = scalar("playback_transport", "udp");
    const auto legacyControl = scalar("playback_legacy_control", "false");
    result.playbackTcp = playbackTransport == "tcp";
    result.playbackLegacyControl = legacyControl == "true";
    if ((playbackTransport != "udp" && playbackTransport != "tcp") ||
        (legacyControl != "true" && legacyControl != "false")) {
        *error = "NVR 回放传输方式或兼容设置无效"; return false;
    }
    if (result.enabled && (!result.address.isValid() || result.address.host().isEmpty() ||
        (result.address.scheme() != "http" && result.address.scheme() != "https") ||
        !result.address.userInfo().isEmpty() || result.address.hasQuery() || result.address.hasFragment() ||
        (result.address.path() != "" && result.address.path() != "/") ||
        !result.apiPrefix.startsWith('/') || result.apiPrefix.contains("..") ||
        result.apiPrefix.contains('?') || result.apiPrefix.contains('#') ||
        !result.timeZone.isValid() || (mode != "local" && mode != "unix") ||
        !timeoutOk || result.timeoutMs < 500 || result.timeoutMs > 60000 || !baseOk ||
        (result.responseChannelBase != 0 && result.responseChannelBase != 1))) {
        *error = "nvr.yaml 地址、时区、时间格式、通道基数或超时设置无效"; return false;
    }
    if (!result.playbackUrlTemplate.isEmpty()) {
        QString sample = result.playbackUrlTemplate;
        const bool complete = sample.contains("{channel}") && sample.contains("{start}") && sample.contains("{stop}");
        sample.replace("{channel}", "1").replace("{start}", "20260101000000").replace("{stop}", "20260101000100");
        if (!complete || sample.contains('{') || sample.contains('}') || !validPlaybackUrl(QUrl(sample), result)) {
            *error = "NVR 回放地址模板必须包含通道和起止时间，并指向同一录像机且不含凭据"; return false;
        }
    }
    if (const auto* mappings = node->get("channels")) {
        if (!mappings->is_seq()) { *error = "nvr.channels 必须是列表"; return false; }
        for (const auto& entry : mappings->seq) {
            const auto* camera = entry.get("camera"); const auto* channel = entry.get("channel");
            long long number = 0;
            if (!camera || !camera->is_scalar() || camera->scalar.empty() || !channel ||
                !railhmi::core::yaml::to_int(*channel, &number) || number < 1 || number > 256 ||
                result.channels.contains(QString::fromStdString(camera->scalar))) {
                *error = "nvr.channels 摄像头或通道号无效/重复"; return false;
            }
            result.channels.insert(QString::fromStdString(camera->scalar), static_cast<int>(number));
        }
    }
    *out = result; return true;
}

NvrClient::NvrClient(NvrSettings settings, QObject* parent)
    : QObject(parent), settings_(std::move(settings)) {
    password_ = QString::fromUtf8(qgetenv(settings_.credentialEnv.toUtf8().constData()));
}

void NvrClient::setCredentials(const QString& username, const QString& password) {
    ++credentialRevision_; requests_.clear(); challenge_.clear(); sessionHeaders_.clear(); nonceCount_ = 0;
    settings_.username = username; password_ = password;
    if (active_ && activeReply_) activeReply_->abort();
}

qint64 NvrClient::dayStart(const QDate& day, const NvrSettings& settings) {
    return QDateTime(day, QTime(0, 0), settings.timeZone).toMSecsSinceEpoch();
}
qint64 NvrClient::deviceSeconds(qint64 utcMs, const NvrSettings& settings) {
    const auto time = QDateTime::fromMSecsSinceEpoch(utcMs, settings.timeZone);
    return utcMs / 1000 + (settings.localEpoch ? time.offsetFromUtc() : 0);
}
qint64 NvrClient::utcMilliseconds(qint64 seconds, const NvrSettings& settings) {
    if (!settings.localEpoch) return seconds * 1000;
    const auto fields = QDateTime::fromSecsSinceEpoch(seconds, Qt::UTC);
    return QDateTime(fields.date(), fields.time(), settings.timeZone).toMSecsSinceEpoch();
}

QString NvrClient::scrub(QString message) const {
    if (!password_.isEmpty()) message.replace(password_, "***");
    return message.left(400);
}

static QByteArray md5(const QByteArray& bytes) {
    return QCryptographicHash::hash(bytes, QCryptographicHash::Md5).toHex();
}
static QByteArray quoted(QByteArray value) {
    value.replace("\\", "\\\\"); value.replace("\"", "\\\"");
    return '"' + value + '"';
}
QByteArray NvrClient::authorization(const QUrl& url) {
    if (challenge_.isEmpty()) return {};
    const QByteArray user = settings_.username.toUtf8(), realm = challenge_["realm"].toUtf8();
    const QByteArray nonce = challenge_["nonce"].toUtf8();
    const QByteArray cnonce = QUuid::createUuid().toByteArray(QUuid::Id128);
    const QByteArray nc = QByteArray::number(++nonceCount_, 16).rightJustified(8, '0');
    const bool ivrDigest = challenge_.value("_scheme") == "X-Digest";
    // IVR's web client signs the absolute URL; standard Digest signs the request target.
    QByteArray uri = ivrDigest ? url.toEncoded(QUrl::RemoveUserInfo | QUrl::RemoveFragment)
                               : url.path(QUrl::FullyEncoded).toUtf8();
    if (!ivrDigest && url.hasQuery()) uri += '?' + url.query(QUrl::FullyEncoded).toUtf8();
    QByteArray first = md5(user + ':' + realm + ':' + password_.toUtf8());
    const QByteArray algorithm = challenge_.value("algorithm", "MD5").toUtf8();
    if (algorithm.toLower() == "md5-sess") first = md5(first + ':' + nonce + ':' + cnonce);
    const bool qop = !challenge_.value("qop").isEmpty();
    const QByteArray response = md5(first + ':' + nonce + ':' +
        (qop ? nc + ':' + cnonce + ":auth:" : QByteArray()) + md5("POST:" + uri));
    QByteArray result = (ivrDigest ? QByteArray("X-Digest") : QByteArray("Digest")) +
        " username=" + quoted(user) + ", realm=" + quoted(realm) +
        ", nonce=" + quoted(nonce) + ", uri=" + quoted(uri) + ", response=" + quoted(response) +
        ", algorithm=" + algorithm;
    if (qop) result += ", qop=auth, nc=" + nc + ", cnonce=" + quoted(cnonce);
    else if (algorithm.toLower() == "md5-sess") result += ", cnonce=" + quoted(cnonce);
    if (challenge_.contains("opaque")) result += ", opaque=" + quoted(challenge_["opaque"].toUtf8());
    return result;
}

void NvrClient::enqueue(Request request) {
    if (!settings_.enabled) { emit failed(request.operation, "录像机接口未启用"); return; }
    if (password_.isEmpty()) {
        emit failed(request.operation, "缺少录像机密码环境变量：" + settings_.credentialEnv); return;
    }
    if (request.operation == "records" || request.operation == "playback") {
        for (int i = requests_.size() - 1; i >= 0; --i)
            if (requests_[i].operation == request.operation) requests_.removeAt(i);
    }
    requests_.enqueue(std::move(request));
    processNext();
}
void NvrClient::processNext() {
    if (active_ || requests_.isEmpty()) return;
    active_ = true; send(requests_.dequeue());
}
void NvrClient::send(Request request) {
    QUrl url = settings_.address;
    QString prefix = settings_.apiPrefix;
    while (prefix.endsWith('/')) prefix.chop(1);
    url.setPath(prefix + request.endpoint);
    QNetworkRequest net(url);
    net.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    net.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    for (auto it = sessionHeaders_.cbegin(); it != sessionHeaders_.cend(); ++it)
        net.setRawHeader(it.key(), it.value());
    const auto auth = sessionHeaders_.isEmpty() ? authorization(url) : QByteArray();
    if (!auth.isEmpty()) net.setRawHeader("Authorization", auth);
    auto* reply = network_.post(net, QJsonDocument(request.body).toJson(QJsonDocument::Compact));
    activeReply_ = reply;
    const auto credentialRevision = credentialRevision_;
    auto* timeout = new QTimer(reply); timeout->setSingleShot(true);
    connect(timeout, &QTimer::timeout, reply, [reply]() { reply->setProperty("timedOut", true); reply->abort(); });
    timeout->start(settings_.timeoutMs);
    connect(reply, &QNetworkReply::readyRead, reply, [reply]() {
        if (reply->bytesAvailable() > 8 * 1024 * 1024) { reply->setProperty("tooLarge", true); reply->abort(); }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, request, timeout, credentialRevision]() mutable {
        timeout->stop();
        if (credentialRevision != credentialRevision_) {
            reply->deleteLater(); active_ = false;
            QTimer::singleShot(0, this, &NvrClient::processNext); return;
        }
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray challenge = reply->rawHeader("WWW-Authenticate");
        const QByteArray bytes = reply->isReadable() ? reply->readAll() : QByteArray();
        QString problem;
        if (reply->property("timedOut").toBool()) problem = "录像机请求超时";
        else if (reply->property("tooLarge").toBool()) problem = "录像机响应超过大小限制";
        else if (status == 401 && request.attempts < 1 &&
                 (challenge.startsWith("Digest ") || challenge.startsWith("X-Digest "))) {
            QMap<QString, QString> fields;
            fields["_scheme"] = challenge.startsWith("X-Digest ") ? "X-Digest" : "Digest";
            const QRegularExpression field("([A-Za-z]+)=(?:\"([^\"]*)\"|([^,\\s]+))");
            auto matches = field.globalMatch(QString::fromLatin1(challenge));
            while (matches.hasNext()) {
                const auto match = matches.next();
                fields[match.captured(1)] = match.capturedStart(2) >= 0 ? match.captured(2) : match.captured(3);
            }
            const QString algorithm = fields.value("algorithm", "MD5").toLower();
            if (fields.contains("realm") && !fields.value("nonce").isEmpty() &&
                (algorithm == "md5" || algorithm == "md5-sess") &&
                (!fields.contains("qop") || fields["qop"].split(',').contains("auth"))) {
                sessionHeaders_.clear(); challenge_ = fields; nonceCount_ = 0; ++request.attempts;
                reply->deleteLater(); send(std::move(request)); return;
            }
            problem = "录像机认证协议不受支持";
        } else if (status == 401 || status == 403) problem = "录像机认证失败，请核对账号、密码及回放权限";
        else if (status < 200 || status >= 300) problem = "录像机 HTTP 错误：" + QString::number(status);
        else if (reply->error() != QNetworkReply::NoError) problem = "录像机网络连接失败";
        QJsonParseError parseError;
        const auto doc = QJsonDocument::fromJson(bytes, &parseError);
        if (problem.isEmpty() && (parseError.error != QJsonParseError::NoError || !doc.isObject()))
            problem = "录像机返回的不是有效 JSON";
        const auto object = doc.object();
        // This IVR occasionally returns HTTP 200 with {} for an authenticated read.
        // Retry that specific incomplete response once; never interpret it as an empty recording list.
        if (problem.isEmpty() && object.isEmpty() &&
            (request.operation == "channels" || request.operation == "records")) {
            if (request.emptyRetries++ == 0) {
                reply->deleteLater();
                QTimer::singleShot(150, this, [this, request, credentialRevision]() mutable {
                    if (credentialRevision != credentialRevision_ ||
                        (request.operation == "records" && request.revision != queryRevision_)) {
                        active_ = false; processNext(); return;
                    }
                    send(std::move(request));
                });
                return;
            }
            problem = "录像机返回了空响应，重试后仍未取得有效数据";
        }
        const auto result = object.contains("Result") ? object.value("Result") : object.value("result");
        if (qEnvironmentVariableIsSet("RAILHMI_NVR_TRACE") && !result.isDouble())
            qWarning() << "NVR response schema" << request.operation << object.keys()
                       << "resultType" << static_cast<int>(result.type()) << "bytes" << bytes.size();
        if (problem.isEmpty() && (!result.isDouble() || result.toDouble() != 0))
            problem = "录像机查询失败（返回码 " +
                (result.isDouble() ? QString::number(result.toDouble()) : QString("无效")) +
                "）：" + object.value("ErrorString").toString("设备未提供错误说明");
        // IVR requires all three login/session hashes on subsequent calls and may rotate them.
        // Only accept a complete set from a successful JSON response of this device.
        const auto sHash = reply->rawHeader("S-HASH"), xHash = reply->rawHeader("X-HASH"),
                   cHash = reply->rawHeader("C-HASH");
        if (problem.isEmpty() && !sHash.isEmpty() && !xHash.isEmpty() && !cHash.isEmpty())
            sessionHeaders_ = {{"S-HASH", sHash}, {"X-HASH", xHash}, {"C-HASH", cHash}};
        reply->deleteLater(); active_ = false;
        if ((request.operation == "records" && request.revision != queryRevision_) ||
            (request.operation == "playback" && request.revision != playbackRevision_)) {
            QTimer::singleShot(0, this, &NvrClient::processNext); return;
        }
        if (!problem.isEmpty()) emit failed(request.operation, scrub(problem));
        else request.callback(object);
        QTimer::singleShot(0, this, &NvrClient::processNext);
    });
}

void NvrClient::fetchChannels() {
    // An explicit reconnect must not keep sending an expired device session.
    sessionHeaders_.clear(); challenge_.clear(); nonceCount_ = 0;
    enqueue({"login", "/frmUserLogin", {{"Type", 0}, {"Ch", 0}, {"Data", QJsonObject{}}},
        [this](const QJsonObject&) {
            enqueue({"channels", "/frmGetPlaybackInfo", {{"Type", 0}, {"Ch", 0}, {"Data", QJsonObject{}}},
                [this](const QJsonObject& response) {
                    const auto value = response["Data"].toObject()["Channels"];
                    if (!value.isArray()) { emit failed("channels", "录像机通道列表格式错误"); return; }
                    QVariantList channels;
                    const auto array = value.toArray();
                    for (int i = 0; i < array.size() && i < 256; ++i) {
                        const auto row = array[i].toArray();
                        if (row.size() < 2 || !row[0].isString() || !row[1].isDouble()) continue;
                        channels.append(QVariantMap{{"channel", i + 1}, {"name", row[0].toString()},
                                                     {"allowed", row[1].toInt() == 1}});
                    }
                    emit channelsReady(channels);
                }});
        }});
}

bool NvrClient::parseRecords(const QJsonObject& response, int channel, const QString& camera,
                             qint64 begin, qint64 end, const NvrSettings& settings,
                             QVariantList* records, QString* error) {
    const auto results = response["Data"].toObject()["SearchResults"];
    if (!results.isArray()) { *error = "录像机录像列表格式错误"; return false; }
    records->clear();
    for (const auto& result : results.toArray()) {
        const auto object = result.toObject();
        if (!object["ChannelNo"].isDouble()) { *error = "录像通道号格式错误"; return false; }
        if (object["ChannelNo"].toInt() != channel - 1 + settings.responseChannelBase) continue;
        if (!object["Records"].isArray()) { *error = "录像时间段格式错误"; return false; }
        for (const auto& item : object["Records"].toArray()) {
            const auto row = item.toArray();
            if (row.size() < 3 || !row[0].isDouble() || !row[1].isDouble() || !row[2].isDouble() ||
                !std::isfinite(row[1].toDouble()) || !std::isfinite(row[2].toDouble()) ||
                row[1].toDouble() < 0 || row[2].toDouble() > 32503680000.0 ||
                row[2].toDouble() <= row[1].toDouble()) { *error = "录像时间段内容无效"; return false; }
            const qint64 start = std::max(begin, utcMilliseconds(static_cast<qint64>(row[1].toDouble()), settings));
            const qint64 stop = std::min(end, utcMilliseconds(static_cast<qint64>(row[2].toDouble()), settings));
            if (start >= stop) continue;
            records->append(QVariantMap{{"cameraId", camera}, {"channel", channel}, {"t0", start}, {"t1", stop},
                {"kind", "continuous"}, {"level", 0}, {"recordType", row[0].toInt()}, {"label", "NVR 录像"}});
            if (records->size() > 50000) { *error = "录像时间段数量超过限制"; return false; }
        }
    }
    std::sort(records->begin(), records->end(), [](const QVariant& a, const QVariant& b) {
        return a.toMap()["t0"].toLongLong() < b.toMap()["t0"].toLongLong();
    });
    return true;
}

void NvrClient::queryRecords(const QString& camera, const QDate& date, int channel) {
    const quint64 revision = ++queryRevision_;
    if (!date.isValid() || channel < 1 || channel > 256) { emit failed("records", "日期或 NVR 通道号无效"); return; }
    const qint64 begin = dayStart(date, settings_), end = dayStart(date.addDays(1), settings_);
    const QJsonObject data{{"Channels", QJsonArray{channel}}, {"QueryType", 2147483647},
        {"BeginDateTime", deviceSeconds(begin, settings_)}, {"EndDateTime", deviceSeconds(end, settings_) - 1},
        {"QueryCondition", 0}};
    enqueue({"records", "/frmVideoRecordsQuery", {{"Type", 0}, {"Ch", channel}, {"Data", data}},
        [this, camera, date, channel, begin, end, revision](const QJsonObject& response) {
            if (revision != queryRevision_) return;
            QVariantList records; QString error;
            if (!parseRecords(response, channel, camera, begin, end, settings_, &records, &error)) {
                emit failed("records", error); return;
            }
            emit recordsReady(camera, date.toString(Qt::ISODate), records, begin, end);
        }, 0, revision});
}

void NvrClient::requestPlayback(const QString& camera, int channel, qint64 startMs, qint64 endMs) {
    const quint64 revision = ++playbackRevision_;
    const auto start = QDateTime::fromMSecsSinceEpoch(startMs, settings_.timeZone);
    const auto end = QDateTime::fromMSecsSinceEpoch(endMs, settings_.timeZone);
    if (channel < 1 || channel > 256 || !start.isValid() || endMs <= startMs) {
        emit failed("playback", "回放时间或通道无效"); return;
    }
    if (!settings_.playbackUrlTemplate.isEmpty()) {
        QString location = settings_.playbackUrlTemplate;
        location.replace("{channel}", QString::number(channel))
            .replace("{start}", start.toString("yyyyMMddHHmmss"))
            .replace("{stop}", end.toString("yyyyMMddHHmmss"));
        const QUrl url(location);
        if (!validPlaybackUrl(url, settings_)) { emit failed("playback", "NVR 回放地址模板无效"); return; }
        QTimer::singleShot(0, this, [this, camera, url, startMs, endMs, revision]() {
            if (revision == playbackRevision_) emit playbackReady(camera, url, startMs, endMs);
        });
        return;
    }
    const QJsonObject data{{"Channel", channel}, {"StreamType", 0}, {"StartTime", start.toString("yyyyMMddHHmmss")},
        {"StopTime", end.toString("yyyyMMddHHmmss")}, {"TimeMode", "Local"}};
    enqueue({"playback", "/frmGetRtspUrl", {{"Type", 13}, {"Ch", channel}, {"Data", data}},
        [this, camera, startMs, endMs, revision](const QJsonObject& response) {
            if (revision != playbackRevision_) return;
            const QUrl url(response["Data"].toObject()["RtspUrl"].toString());
            if (!validPlaybackUrl(url, settings_)) {
                emit failed("playback", "设备未返回可用的本机 RTSP 回放地址"); return;
            }
            emit playbackReady(camera, url, startMs, endMs);
        }, 0, revision});
}
