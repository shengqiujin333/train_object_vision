#include "nvr_client.h"
#include "replay_model.h"
#include "replay_decoder.h"
#include "video_runtime.h"
#include <QGuiApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QQmlContext>
#include <QQuickView>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <qqml.h>
#include <cstdio>
#include <functional>

static int failures = 0;
static void check(bool condition, const char* name) {
    std::printf("%s %s\n", condition ? "PASS" : "FAIL", name);
    if (!condition) ++failures;
}
static bool waitFor(const std::function<bool()>& condition, int limit = 5000) {
    QElapsedTimer timer; timer.start();
    while (timer.elapsed() < limit) {
        QCoreApplication::processEvents();
        if (condition()) return true;
        QThread::msleep(10);
    }
    return false;
}
static QByteArray md5(QByteArray input) { return QCryptographicHash::hash(input, QCryptographicHash::Md5).toHex(); }

class MockNvr : public QTcpServer {
public:
    int mode = 0, count = 0, recordRequests = 0;
    bool digestValid = true;
    QJsonObject lastQuery, lastPlayback;
    MockNvr() {
        listen(QHostAddress::LocalHost, 0);
        connect(this, &QTcpServer::newConnection, this, [this]() {
            while (hasPendingConnections()) {
                auto* socket = nextPendingConnection();
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
                    QByteArray data = socket->property("request").toByteArray() + socket->readAll();
                    socket->setProperty("request", data);
                    const int split = data.indexOf("\r\n\r\n");
                    if (split < 0 || socket->property("handled").toBool()) return;
                    const auto head = data.left(split);
                    const auto match = QRegularExpression("Content-Length: (\\d+)", QRegularExpression::CaseInsensitiveOption)
                        .match(QString::fromLatin1(head));
                    const int length = match.captured(1).toInt();
                    if (data.size() < split + 4 + length) return;
                    socket->setProperty("handled", true); ++count;
                    const QByteArray path = head.split(' ')[1];
                    const auto body = QJsonDocument::fromJson(data.mid(split + 4, length)).object();
                    QByteArray auth;
                    for (const auto& line : head.split('\n')) if (line.toLower().startsWith("authorization:")) auth = line.mid(14).trimmed();
                    if (auth.isEmpty() || mode == 1) {
                        respond(socket, 401, {}, "WWW-Authenticate: X-Digest realm=\"fixture\", nonce=\"abc\", qop=\"auth\", opaque=\"def\", algorithm=MD5\r\n"); return;
                    }
                    QMap<QByteArray, QByteArray> fields;
                    auto matches = QRegularExpression("([A-Za-z]+)=(?:\"([^\"]*)\"|([^,\\s]+))").globalMatch(QString::fromLatin1(auth));
                    while (matches.hasNext()) {
                        auto m = matches.next(); fields[m.captured(1).toLatin1()] =
                            (m.capturedStart(2) >= 0 ? m.captured(2) : m.captured(3)).toLatin1();
                    }
                    const auto expected = md5(md5("admin:fixture:fixture-password") + ":abc:" + fields["nc"] + ':' +
                        fields["cnonce"] + ":auth:" + md5("POST:" + path));
                    if (!auth.startsWith("Digest ") || fields["response"] != expected || fields["uri"] != path) {
                        digestValid = false; respond(socket, 401, {}); return;
                    }
                    if (mode == 4) return; // Deliberately no response, for timeout coverage.
                    if (mode == 2) { respond(socket, 200, "<html>not JSON</html>"); return; }
                    if (mode == 3) { respond(socket, 200, "{\"Result\":-1,\"ErrorString\":\"denied\"}"); return; }
                    QJsonObject result{{"Result", 0}, {"Data", QJsonObject{}}};
                    if (path.endsWith("frmGetPlaybackInfo"))
                        result["Data"] = QJsonObject{{"Channels", QJsonArray{QJsonArray{"front", 1}, QJsonArray{"left", 0}}}};
                    if (path.endsWith("frmVideoRecordsQuery")) {
                        lastQuery = body; ++recordRequests;
                        const auto data = body["Data"].toObject();
                        const auto begin = data["BeginDateTime"].toDouble();
                        const int channel = data["Channels"].toArray()[0].toInt();
                        result["Data"] = QJsonObject{{"SearchResults", QJsonArray{QJsonObject{
                            {"ChannelNo", channel - 1}, {"Records", mode == 5 ? QJsonArray{} : QJsonArray{QJsonArray{1, begin + 100, begin + 200}}}}}}};
                        if (mode == 6 && recordRequests == 1) {
                            QPointer<QTcpSocket> safe(socket);
                            QTimer::singleShot(200, this, [safe, result]() {
                                if (safe) respond(safe, 200, QJsonDocument(result).toJson(QJsonDocument::Compact));
                            }); return;
                        }
                    }
                    if (path.endsWith("frmGetRtspUrl")) {
                        lastPlayback = body;
                        result["Data"] = QJsonObject{{"RtspUrl", "rtsp://127.0.0.1:554/recording?ch=1"}};
                    }
                    respond(socket, 200, QJsonDocument(result).toJson(QJsonDocument::Compact));
                });
            }
        });
    }
    static void respond(QTcpSocket* socket, int code, const QByteArray& body, const QByteArray& extra = {}) {
        socket->write("HTTP/1.1 " + QByteArray::number(code) + (code == 200 ? " OK\r\n" : " Unauthorized\r\n") +
            "Content-Type: application/json\r\nConnection: close\r\n" + extra + "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body);
        socket->disconnectFromHost();
    }
    NvrSettings settings() const {
        NvrSettings result; result.enabled = true;
        result.address = QUrl("http://127.0.0.1:" + QString::number(serverPort()));
        result.credentialEnv = "RAILHMI_TEST_NVR_PASSWORD"; result.timeoutMs = 600;
        return result;
    }
};

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    if (argc >= 3 && QString::fromLocal8Bit(argv[1]) == "--probe") {
        NvrSettings settings; QString error;
        if (!loadNvrSettings(QString::fromLocal8Bit(argv[2]), &settings, &error)) {
            std::printf("NVR_PROBE_CONFIG_ERROR %s\n", error.toUtf8().constData()); return 2;
        }
        NvrClient client(settings); bool done = false; int result = 3;
        QObject::connect(&client, &NvrClient::failed, &app, [&](const QString& operation, const QString& message) {
            std::printf("NVR_PROBE_FAILURE operation=%s reason=%s\n", operation.toUtf8().constData(), message.toUtf8().constData()); done = true;
        });
        QObject::connect(&client, &NvrClient::channelsReady, &app, [&](const QVariantList& channels) {
            std::printf("NVR_PROBE_CONNECTED channels=%d\n", channels.size());
            if (argc >= 5) client.queryRecords("probe", QDate::fromString(QString::fromLocal8Bit(argv[4]), Qt::ISODate), QString::fromLocal8Bit(argv[3]).toInt());
            else { result = 0; done = true; }
        });
        QObject::connect(&client, &NvrClient::recordsReady, &app,
            [&](const QString&, const QString&, const QVariantList& records, qint64, qint64) {
                std::printf("NVR_PROBE_RECORDS count=%d\n", records.size()); result = 0; done = true;
            });
        client.fetchChannels();
        if (!waitFor([&]() { return done; }, settings.timeoutMs * 4 + 1000)) std::printf("NVR_PROBE_DEADLINE\n");
        return result;
    }
    qputenv("RAILHMI_TEST_NVR_PASSWORD", "fixture-password");
    qmlRegisterUncreatableType<VideoChannel>("Railhmi.Video", 1, 0, "VideoChannel", "Runtime");
    qmlRegisterType<VideoFrameItem>("Railhmi.Video", 1, 0, "VideoFrame");
    const QDate day(2026, 10, 26);
    NvrSettings settings; settings.timeZone = QTimeZone("Asia/Shanghai");
    const auto begin = NvrClient::dayStart(day, settings);
    check(QDateTime::fromMSecsSinceEpoch(begin, Qt::UTC).toString(Qt::ISODate).startsWith("2026-10-25T16:00:00"), "local_day_boundaries_are_not_UTC_midnight");
    check(NvrClient::utcMilliseconds(NvrClient::deviceSeconds(begin, settings), settings) == begin, "local_epoch_roundtrip");
    settings.localEpoch = false;
    check(NvrClient::deviceSeconds(begin, settings) == begin / 1000 && NvrClient::utcMilliseconds(begin / 1000, settings) == begin,
          "unix_epoch_mode");
    settings.localEpoch = true;
    const double start = NvrClient::deviceSeconds(begin, settings);
    const auto response = QJsonObject{{"Result", 0}, {"Data", QJsonObject{{"SearchResults", QJsonArray{QJsonObject{
        {"ChannelNo", 0}, {"Records", QJsonArray{QJsonArray{1, start + 20, start + 50}, QJsonArray{2, start + 5, start + 10}}}}}}}}};
    QVariantList records; QString problem;
    check(NvrClient::parseRecords(response, 1, "CAM-01", begin, begin + 86400000, settings, &records, &problem) &&
          records.size() == 2 && records[0].toMap()["t0"].toLongLong() == begin + 5000, "parse_sort_and_translate_zero_based_response_channel");
    check(NvrClient::parseRecords(response, 2, "CAM-02", begin, begin + 86400000, settings, &records, &problem) && records.isEmpty(),
          "never_assign_another_channels_recordings");
    check(!NvrClient::parseRecords({}, 1, "CAM-01", begin, begin + 86400000, settings, &records, &problem), "malformed_records_are_not_empty_success");
    {
        MockNvr server; NvrClient client(server.settings());
        QVariantList channels, found; QString error; QUrl playback;
        QObject::connect(&client, &NvrClient::channelsReady, &app, [&](const QVariantList& value) { channels = value; });
        QObject::connect(&client, &NvrClient::recordsReady, &app, [&](const QString&, const QString&, const QVariantList& value, qint64, qint64) { found = value; });
        QObject::connect(&client, &NvrClient::playbackReady, &app, [&](const QString&, const QUrl& url, qint64, qint64) { playback = url; });
        QObject::connect(&client, &NvrClient::failed, &app, [&](const QString&, const QString& text) { error = text; });
        client.fetchChannels();
        check(waitFor([&]() { return channels.size() == 2 || !error.isEmpty(); }) && channels.size() == 2 &&
              server.digestValid && server.count == 3 && !channels[1].toMap()["allowed"].toBool(), "X_Digest_login_and_channel_permissions");
        client.queryRecords("CAM-01", day, 1);
        check(waitFor([&]() { return !found.isEmpty() || !error.isEmpty(); }) && found.size() == 1 &&
              found[0].toMap()["t0"].toLongLong() == begin + 100000, "asynchronous_real_HTTP_record_query");
        check(server.lastQuery["Type"].toInt() == 0 && server.lastQuery["Data"].toObject()["QueryCondition"].toInt(-1) == 0 &&
              server.lastQuery["Data"].toObject()["EndDateTime"].toDouble() == start + 86399, "vendor_query_payload_and_inclusive_day_end");
        client.requestPlayback("CAM-01", 1, begin + 100000, begin + 200000);
        check(waitFor([&]() { return playback.isValid() || !error.isEmpty(); }) && playback.scheme() == "rtsp" &&
              server.lastPlayback["Type"].toInt() == 13, "playback_address_is_requested_from_device");
    }
    for (int mode : {1, 2, 3, 4}) {
        MockNvr server; server.mode = mode; NvrClient client(server.settings()); QString error; int successes = 0;
        QObject::connect(&client, &NvrClient::failed, &app, [&](const QString&, const QString& text) { error = text; });
        QObject::connect(&client, &NvrClient::recordsReady, &app, [&](const QString&, const QString&, const QVariantList&, qint64, qint64) { ++successes; });
        client.queryRecords("CAM-01", day, 1);
        check(waitFor([&]() { return !error.isEmpty(); }) && successes == 0 && !error.contains("fixture-password"),
              mode == 1 ? "auth_failure_is_bounded_and_redacted" : mode == 2 ? "HTML_response_is_rejected" :
              mode == 3 ? "nonzero_device_Result_is_failure" : "network_timeout_finishes_without_false_success");
        if (mode == 1) check(server.count == 2, "no_unbounded_password_retries");
    }
    {
        MockNvr server; server.mode = 5; NvrClient client(server.settings()); bool delivered = false;
        QObject::connect(&client, &NvrClient::recordsReady, &app, [&](const QString&, const QString&, const QVariantList& list, qint64, qint64) { delivered = list.isEmpty(); });
        client.queryRecords("CAM-01", day, 1);
        check(waitFor([&]() { return delivered; }), "successful_empty_recordings_are_distinct_from_auth_failure");
    }
    {
        MockNvr server; NvrClient client(server.settings()); QString error; bool connected = false;
        QObject::connect(&client, &NvrClient::failed, &app, [&](const QString&, const QString& text) { error = text; });
        QObject::connect(&client, &NvrClient::channelsReady, &app, [&](const QVariantList&) { connected = true; });
        client.setCredentials("admin", "incorrect-fixture"); client.fetchChannels();
        check(waitFor([&]() { return !error.isEmpty(); }) && !connected, "incorrect_interactive_credentials_do_not_connect");
        client.setCredentials("admin", "fixture-password"); error.clear(); client.fetchChannels();
        check(waitFor([&]() { return connected; }) && error.isEmpty(), "corrected_interactive_credentials_reset_digest_session");
    }
    {
        MockNvr server; server.mode = 6; NvrClient client(server.settings()); QString delivered; int replies = 0;
        QObject::connect(&client, &NvrClient::recordsReady, &app, [&](const QString& camera, const QString&, const QVariantList&, qint64, qint64) { delivered = camera; ++replies; });
        client.queryRecords("old", day, 1);
        waitFor([&]() { return server.recordRequests == 1; });
        client.queryRecords("new", day, 2);
        check(waitFor([&]() { return delivered == "new"; }) && replies == 1, "late_previous_query_cannot_overwrite_current_camera");
    }
    {
        QTemporaryDir dir; QFile file(dir.path() + "/nvr.yaml"); file.open(QIODevice::WriteOnly);
        MockNvr server;
        file.write(("nvr:\n  enabled: true\n  address: http://127.0.0.1:" + QString::number(server.serverPort()) +
            "\n  username: admin\n  credential_env: RAILHMI_TEST_NVR_PASSWORD\n  channels:\n    - camera: CAM-01\n      channel: 1\n").toUtf8()); file.close();
        RailhmiReplayModel model; model.configureNvr(file.fileName());
        model.loginNvr("admin", "fixture-password");
        check(waitFor([&]() { return model.nvrState()["connected"].toBool(); }), "page_login_loads_actual_channel_list");
        QQuickView view; view.setResizeMode(QQuickView::SizeRootObjectToView);
        view.rootContext()->setContextProperty("railhmiReplay", &model);
        view.rootContext()->setContextProperty("railhmiCameras", QVariantList{QVariantMap{{"id", "CAM-01"}, {"name", "front"}}});
        view.rootContext()->setContextProperty("railhmiPermissions", static_cast<QObject*>(nullptr));
        view.setSource(QUrl("qrc:/qml/pages/P3Replay.qml")); view.resize(1280, 720); view.show();
        check(waitFor([&]() { return model.nvrState()["count"].toInt() > 0; }), "P3_queries_API_and_receives_real_timeline_data");
        check(view.status() == QQuickView::Ready && model.sourceStatus()["source"] == "nvr" && model.sourceStatus()["usable"].toBool(),
              "P3_NVR_mode_loads_and_reports_source");
        const auto timeline = model.timeline("CAM-01", QDate::currentDate().toString(Qt::ISODate));
        check(timeline["segments"].toList().size() == 1 && timeline["keyframes"].toList().isEmpty(), "no_fabricated_thumbnails_or_sample_timeline");
        if (argc > 1) view.grabWindow().save(QString::fromLocal8Bit(argv[1]) + "/nvr-history.png");
    }
    {
        ReplayDecoder decoder; QImage frame; qint64 position = -1; QString state;
        QObject::connect(&decoder, &ReplayDecoder::frameReady, &app, [&](const QImage& image, qint64 time) { frame = image; position = time; });
        QObject::connect(&decoder, &ReplayDecoder::stateChanged, &app, [&](const QString& value, const QString&) { state = value; });
        decoder.start(QUrl("test://smpte"), {}, {}, begin, begin + 10000);
        check(waitFor([&]() { return !frame.isNull(); }) && frame.width() == 640 && position >= begin, "history_decoder_produces_owned_real_pixels");
        decoder.setPaused(true);
        check(waitFor([&]() { return state == "paused"; }), "history_pause_is_a_media_operation");
        decoder.setPaused(false);
        check(waitFor([&]() { return state == "playing" && position > begin; }), "history_resume_advances_decoded_position");
        decoder.stop(); check(frame.isNull(), "history_stop_clears_frame");
    }
    std::printf("NVR_TEST failures=%d\n", failures);
    return failures ? 1 : 0;
}
