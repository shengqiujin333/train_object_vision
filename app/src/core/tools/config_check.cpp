// railhmi 启动配置校验入口（ITEM-004）
// 加载并校验 config/*.yaml；非法配置逐条打印**具体字段路径**并非零退出。
// 不依赖 Qt/GStreamer，可在目标板/主机最小工具链运行（与应用启动同一校验代码）。

#include <cstdio>
#include <string>

#include "railhmi/core/config.h"

int main(int argc, char** argv) {
    std::string dir = (argc > 1) ? argv[1] : "config";
    railhmi::core::ConfigLoadResult r = railhmi::core::load_config_dir(dir);
    if (r.ok) {
        std::printf("config OK: %zu cameras, %zu roi, %zu integrations (dir=%s)\n",
                    r.settings.cameras.size(), r.settings.roi.size(),
                    r.settings.app.integrations.size(), dir.c_str());
        std::printf("  mode=%s render_path=%s recording_source=%s\n",
                    railhmi::core::to_string(r.settings.app.mode),
                    railhmi::core::to_string(r.settings.app.render_path),
                    railhmi::core::to_string(r.settings.app.recording_source));
        return 0;
    }
    std::printf("config INVALID (%zu error(s)) dir=%s:\n", r.errors.size(), dir.c_str());
    for (const auto& e : r.errors) {
        std::printf("  %s\n", e.str().c_str());  // "path: message" —— 含具体字段名
    }
    return 1;
}
