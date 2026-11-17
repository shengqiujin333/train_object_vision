#pragma once

// 车辆状态（静止/运动）运行时。
// 现场没有车速/GPS/轴速传感器数据源，按契约不得臆测车辆在动还是静止；
// 因此顶栏该项是**人工置位**：点击在 未接入 -> 静止 -> 运动 间循环，
// 界面同时标注「人工置位」，不把人工输入伪装成设备真值。
// 真值映射在 core::vehicle_state_top_status，UI 只负责切换与通知。

#include <QObject>

class VehicleStateRuntime : public QObject {
    Q_OBJECT
    Q_PROPERTY(int mode READ mode NOTIFY changed)
public:
    explicit VehicleStateRuntime(QObject* parent = nullptr);

    // 0=未接入, 1=静止, 2=运动
    int mode() const { return mode_; }

    Q_INVOKABLE void cycle();
    Q_INVOKABLE void reset();

signals:
    void changed();

private:
    int mode_ = 0;
};
