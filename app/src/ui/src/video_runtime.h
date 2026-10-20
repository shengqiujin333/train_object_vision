#pragma once

#include <QImage>
#include <QMutex>
#include <QObject>
#include <QQuickPaintedItem>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <memory>
#include <vector>

#include "railhmi/core/camera_registry.h"
#include "railhmi/core/config.h"

// Stable objects keep QML delegates alive while status and frames change.
class VideoChannel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap info READ info NOTIFY infoChanged)
public:
    explicit VideoChannel(QObject* parent = nullptr) : QObject(parent) {}
    QVariantMap info() const { return info_; }
    QImage image() const;
    void setInfo(const QVariantMap& info);
    void setImage(const QImage& image);
signals:
    void infoChanged();
    void frameChanged();
private:
    QVariantMap info_;
    mutable QMutex mutex_;
    QImage image_;
};

// Qt owns the uploaded image; neither QML nor the render thread sees Gst buffers.
class VideoFrameItem : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(VideoChannel* channel READ channel WRITE setChannel NOTIFY channelChanged)
public:
    explicit VideoFrameItem(QQuickItem* parent = nullptr);
    VideoChannel* channel() const { return channel_; }
    void setChannel(VideoChannel* channel);
    void paint(QPainter* painter) override;
signals:
    void channelChanged();
private:
    VideoChannel* channel_ = nullptr;
    QMetaObject::Connection frameConnection_;
};

class VideoRuntime : public QObject {
    Q_OBJECT
public:
    explicit VideoRuntime(const railhmi::core::AppSettings& settings, QObject* parent = nullptr);
    ~VideoRuntime() override;
    QVariantList channels() const;
    void start();
    void stop();
    // Used by the integration test; production pages cannot inject faults.
    void freezeForTest(std::size_t index, bool freeze);
private:
    void refresh();
    struct Impl;
    std::unique_ptr<Impl> impl_;
    QTimer timer_;
};
