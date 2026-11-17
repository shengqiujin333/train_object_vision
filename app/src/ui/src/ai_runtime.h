#pragma once

// railhmi UI: 视觉推理接入（2026-11-17 现场需求）。
//
// 职责：
//   * 用 media::VisionAiWorker + RknnVisionDetector 在独立线程跑真实 RKNN 推理；
//   * 帧只来自取流层（VideoRuntime::latestLease），不造帧、不复制像素；
//   * 车前方（front）/受电弓（pantograph）/轮轨（wheel_rail）当前**共用同一个检测模型**
//     （配置 app.ai.reuse_detect_for_duties），将来换成专用模型只需改配置；
//     front 另需分割模型做「轨道掩码 ∩ 人/物」两阶段判定；
//   * 结果以 QVariant 列表提供给 P5（框/置信度/耗时/模型名/可用性）；
//   * 报警判定委托 core::AiAlarmTracker（多帧确认 + 解除保持），跃迁通过信号上报，
//     P1 三画面据此闪红框、报警中心追加事件；
//   * 模型可在 P5 现场替换（selectModel），替换时重建检测器与工作线程。
//
// 语义边界：模型名/路径全部来自配置，代码不写死；模型没加载成功、线程没起来或
// 帧没到，一律如实报「未接入/不可用」，绝不显示上一轮的框或伪造报警。

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <memory>

#include "railhmi/core/ai_alarm.h"
#include "railhmi/core/config.h"

class VideoRuntime;

class AiRuntime : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList channels READ channels NOTIFY channelsChanged)
    Q_PROPERTY(QVariantList catalog READ catalog NOTIFY channelsChanged)
    Q_PROPERTY(QVariantMap alarms READ alarms NOTIFY alarmsChanged)
    Q_PROPERTY(bool aiReady READ aiReady NOTIFY statusChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
    // 当前选中的模型（P5 显示与切换用）：检测模型三路共用；分割模型仅车前方使用。
    Q_PROPERTY(QString detectModel READ detectModel NOTIFY modelSelectionChanged)
    Q_PROPERTY(QString segmentModel READ segmentModel NOTIFY modelSelectionChanged)
    Q_PROPERTY(QString modelsDir READ modelsDir NOTIFY modelSelectionChanged)
public:
    AiRuntime(const railhmi::core::AppSettings& settings, const QString& configDir,
              VideoRuntime* video, QObject* parent = nullptr);
    ~AiRuntime() override;

    void start();
    void stop();

    QVariantList channels() const;
    QVariantList catalog() const;
    QVariantMap alarms() const;
    bool aiReady() const { return ready_; }
    QString statusText() const { return status_text_; }
    QString detectModel() const;
    QString segmentModel() const;
    QString modelsDir() const;

    // QML：某路是否处于报警（红框闪烁判据）。
    Q_INVOKABLE bool isAlarming(const QString& cameraId) const;
    // QML：该路当前实际执行的模型（含「临时复用」标注）。
    Q_INVOKABLE QString modelFor(const QString& cameraId) const;
    // QML：替换模型。kind = "detect"（检测，三路共用）|"segment"（分割，仅车前方）。
    Q_INVOKABLE bool selectModel(const QString& kind, const QString& modelName);

signals:
    void channelsChanged();
    void alarmsChanged();
    void statusChanged();
    void alarmRaised(const QString& cameraId, int level, const QString& type,
                     const QString& description);
    void alarmCleared(const QString& cameraId);
    void modelChanged(const QString& kind, const QString& modelName, bool ok, const QString& detail);
    void modelSelectionChanged();

private:
    void tick();
    void rebuild();
    void publish(const QVariantList& channels, const QVariantMap& alarms);

    struct Impl;
    std::unique_ptr<Impl> impl_;
    QVariantList channels_;
    QVariantMap alarms_;
    bool ready_ = false;
    QString status_text_ = QStringLiteral("未接入");
};
