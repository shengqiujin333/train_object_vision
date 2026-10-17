#include "railhmi/core/timebase.h"

#include <chrono>

namespace railhmi {
namespace core {

void TimeBase::calibrate(long long wall_ms, long long mono_us) {
    ref_wall_ms_ = wall_ms;
    ref_mono_us_ = mono_us;
    calibrated_ = true;
}

long long TimeBase::to_wall_ms(long long mono_us) const {
    if (!calibrated_) return mono_us / 1000;
    return ref_wall_ms_ + (mono_us - ref_mono_us_) / 1000;
}

bool TimeBase::try_to_wall_ms(long long mono_us, long long* wall_ms) const {
    if (!calibrated_) return false;
    if (wall_ms) *wall_ms = ref_wall_ms_ + (mono_us - ref_mono_us_) / 1000;
    return true;
}

long long TimeBase::to_mono_us(long long wall_ms) const {
    if (!calibrated_) return wall_ms * 1000;
    return ref_mono_us_ + (wall_ms - ref_wall_ms_) * 1000;
}

long long TimeBase::system_now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

long long TimeBase::steady_now_us() {
    using namespace std::chrono;
    return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}

}  // namespace core
}  // namespace railhmi
