# 历史录像接入验证

- `nvr-build.log`：RK3588 编译输出。
- `nvr-test.log`：模拟 IVR 设备及历史解码测试，37 项通过，包含会话、冷启动查询、空响应重试及模板验证。
- `nvr-video-regression.log`：原有实时视频和页面导航回归，21 项通过。
- `nvr-history.png`：模拟设备返回录像数据后的页面截图，**不是现场 NVR 历史录像**。
- `nvr-real-device-probe.log`：同一 C++ 客户端访问现场设备，取得 10 个通道和真实录像时间段。
- `nvr-real-playback.log`：使用程序实际历史解码器的现场回放检查结果。
- `nvr-six-channel-playback.log`：六路摄像头的真实 NVR 录像查询、实际解码及播放时间推进检查，全部退出 0。
- `auth-validation.txt`：用户确认凭据后，厂家网页格式和独立 curl 客户端的实机认证核对结果。
- `artifact.txt`：目标程序运行状态、执行文件格式、SHA256 和动态库依赖检查。
- `nvr-startup.log`：目标板桌面启动日志，六路实时视频均进入 `live`，没有 QML 启动错误；不替代用户画面验收。

接口与配置说明见 [NVR_HISTORY_INTEGRATION.md](../../NVR_HISTORY_INTEGRATION.md)。
