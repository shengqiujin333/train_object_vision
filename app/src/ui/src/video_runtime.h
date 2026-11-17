#pragma once

#include <QColor>
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
#ifdef RAILHMI_HAS_MEDIA
#include "railhmi/media/frame_lease.h"
#endif

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
    // 0 = Fit（保持比例、不裁剪，比例不符时留黑边）
    // 1 = Cover（保持比例铺满格子，居中裁掉超出部分）
    Q_PROPERTY(int fillMode READ fillMode WRITE setFillMode NOTIFY fillModeChanged)
    // 检测框叠加（2026-11-17 现场需求）：归一化坐标 [0,1]，与 QImage 同源，
    // 因此 Fit/Cover 两种铺法都能精确贴合画面（Cover 时由裁剪矩形裁掉框外部分）。
    // 每项：{x,y,w,h,confidence,label,inRoi}；inRoi=true 用 dangerBoxColor（轨道内目标）。
    Q_PROPERTY(QVariantList boxes READ boxes WRITE setBoxes NOTIFY boxesChanged)
    Q_PROPERTY(QColor boxColor READ boxColor WRITE setBoxColor NOTIFY boxStyleChanged)
    Q_PROPERTY(QColor dangerBoxColor READ dangerBoxColor WRITE setDangerBoxColor
               NOTIFY boxStyleChanged)
    Q_PROPERTY(bool showLabels READ showLabels WRITE setShowLabels NOTIFY boxStyleChanged)
public:
    explicit VideoFrameItem(QQuickItem* parent = nullptr);
    VideoChannel* channel() const { return channel_; }
    void setChannel(VideoChannel* channel);
    int fillMode() const { return fillMode_; }
    void setFillMode(int mode);
    QVariantList boxes() const { return boxes_; }
    void setBoxes(const QVariantList& boxes);
    QColor boxColor() const { return box_color_; }
    void setBoxColor(const QColor& c);
    QColor dangerBoxColor() const { return danger_box_color_; }
    void setDangerBoxColor(const QColor& c);
    bool showLabels() const { return show_labels_; }
    void setShowLabels(bool on);
    void paint(QPainter* painter) override;
signals:
    void channelChanged();
    void fillModeChanged();
    void boxesChanged();
    void boxStyleChanged();
private:
    void drawBoxes(QPainter* painter, const QRectF& target) const;
    VideoChannel* channel_ = nullptr;
    int fillMode_ = 0;
    QVariantList boxes_;
    QColor box_color_ = QColor(0, 229, 255);
    QColor danger_box_color_ = QColor(255, 60, 60);
    bool show_labels_ = true;
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
#ifdef RAILHMI_HAS_MEDIA
    // 推理侧取帧入口（2026-11-17）：返回该路最新 NV12 帧租约（共享所有权、零拷贝）。
    // 无帧/未启动时返回空租约（status()==NOT_READY），调用方不得当实时帧使用。
    railhmi::media::FrameLease latestLease(const std::string& camera_id) const;
#endif
private:
    void refresh();
    struct Impl;
    std::unique_ptr<Impl> impl_;
    QTimer timer_;
};
