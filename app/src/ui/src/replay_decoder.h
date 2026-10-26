#pragma once

#include <QImage>
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <memory>

// One history playback session, separate from all live camera sessions.
class ReplayDecoder : public QObject {
    Q_OBJECT
public:
    explicit ReplayDecoder(QObject* parent = nullptr);
    ~ReplayDecoder() override;
    void start(const QUrl& url, const QString& username, const QString& password,
               qint64 beginMs, qint64 endMs, bool legacyControl = false, bool tcpTransport = false);
    void stop();
    void setPaused(bool paused);
    void setRate(double rate);
signals:
    void frameReady(const QImage& image, qint64 positionMs);
    void stateChanged(const QString& state, const QString& detail);
private:
    void poll();
    struct Impl;
    std::unique_ptr<Impl> impl_;
    QTimer timer_;
    quint64 lastFrame_ = 0;
    QString lastState_, lastDetail_;
};
