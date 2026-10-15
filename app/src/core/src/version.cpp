#include "railhmi/core/version.h"

#ifndef RAILHMI_BUILD_MODE_STR
#define RAILHMI_BUILD_MODE_STR "Unknown"
#endif

namespace railhmi {
namespace core {

const char* version_string() { return "0.1.0"; }

const char* build_mode() { return RAILHMI_BUILD_MODE_STR; }

bool simulation_enabled() {
#ifdef RAILHMI_SIMULATION
    return true;
#else
    return false;
#endif
}

std::string build_description() {
    std::string s = "railhmi ";
    s += version_string();
    s += " [mode=";
    s += build_mode();
    s += simulation_enabled() ? ", simulation=on]" : ", simulation=off]";
    return s;
}

}  // namespace core
}  // namespace railhmi
