#pragma once

#include <QObject>
#include <QDate>
#include <QJsonObject>
#include <QMap>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QQueue>
#include <QTimeZone>
#include <QUrl>
#include <QVariantList>
#include <functional>

struct NvrSettings {
    bool enabled = false;
    QUrl address;
    QString apiPrefix = "/digest";
    QString username = "admin";
    QString credentialEnv = "TRAIN_NVR_CREDENTIAL";
    QTimeZone timeZone = QTimeZone("Asia/Shanghai");
    bool localEpoch = true; // API documents timestamps as local time.
    int timeoutMs = 8000;
    int responseChannelBase = 0; // Request Channels are 1-based; ChannelNo is 0-based.
    QMap<QString, int> channels;
};

bool loadNvrSettings(const QString& file, NvrSettings* out, QString* error);

// Asynchronous IVR client. Only read/query operations are exposed.
class NvrClient : public QObject {
    Q_OBJECT
public:
    explicit NvrClient(NvrSettings settings, QObject* parent = nullptr);
    void setCredentials(const QString& username, const QString& password);
    void fetchChannels();
    void queryRecords(const QString& camera, const QDate& date, int channel);
    void requestPlayback(const QString& camera, int channel, qint64 startMs, qint64 endMs);
    const NvrSettings& settings() const { return settings_; }
    static qint64 dayStart(const QDate& day, const NvrSettings& settings);
    static qint64 deviceSeconds(qint64 utcMs, const NvrSettings& settings);
    static qint64 utcMilliseconds(qint64 seconds, const NvrSettings& settings);
    static bool parseRecords(const QJsonObject& response, int channel, const QString& camera,
                             qint64 begin, qint64 end, const NvrSettings& settings,
                             QVariantList* records, QString* error);
signals:
    void channelsReady(const QVariantList& channels);
    void recordsReady(const QString& camera, const QString& date, const QVariantList& records,
                      qint64 begin, qint64 end);
    void playbackReady(const QString& camera, const QUrl& url, qint64 begin, qint64 end);
    void failed(const QString& operation, const QString& reason);
private:
    using Callback = std::function<void(const QJsonObject&)>;
    struct Request { QString operation, endpoint; QJsonObject body; Callback callback; int attempts = 0; quint64 revision = 0; };
    void enqueue(Request request);
    void processNext();
    void send(Request request);
    QString scrub(QString message) const;
    QByteArray authorization(const QUrl& url);
    NvrSettings settings_;
    QNetworkAccessManager network_;
    QQueue<Request> requests_;
    bool active_ = false;
    QString password_;
    QMap<QString, QString> challenge_;
    quint32 nonceCount_ = 0;
    quint64 queryRevision_ = 0;
    quint64 playbackRevision_ = 0;
    quint64 credentialRevision_ = 0;
    QPointer<QNetworkReply> activeReply_;
};
