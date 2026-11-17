#include "vehicle_state.h"

#include <string>

#include "railhmi/platform/log.h"

VehicleStateRuntime::VehicleStateRuntime(QObject* parent) : QObject(parent) {}

void VehicleStateRuntime::cycle() {
    mode_ = (mode_ + 1) % 3;
    railhmi::platform::Logger::instance().info(
        "vehicle-state: manual selection mode=" + std::to_string(mode_));
    emit changed();
}

void VehicleStateRuntime::reset() {
    if (mode_ == 0) return;
    mode_ = 0;
    emit changed();
}
