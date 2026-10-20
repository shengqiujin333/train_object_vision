# 全功能入口与六画面默认首页

按用户最新要求：取消页面角色隐藏，全部六个页面可见；启动直接进入全摄像头六画面。
默认运行角色改为管理员，使设备操作、配置和导出入口完整显示。
页面访问规则、权限视图和 AI 调试页的独立角色拦截已同步修改。
操作二次确认仍按原有流程执行。尚未接入的录像、NVR 回放和推理仍显示其真实不可用状态。

## 目标板验证

- `192.168.0.103` aarch64 本机构建成功。
- 核心测试：1452 PASS、0 FAIL。
- Qt 集成测试：21 PASS、0 FAIL；包含实际应用外壳默认六画面、六个导航入口，
  以及逐一进入实时监控、报警中心、历史录像、全摄像头、AI 调试、设备维护。
  测试特意以旧版受限的 driver 角色验证所有页面均可进入，无 QML 运行错误。
- AI 探针 PASS：driver_debug_visible=true。
- QML/Qt 合法性、UI 令牌、AI 接线、权限接线及打包检查通过。
- 新版已直接启动在板端 `DISPLAY=:0`。启动 PID 838935，六路现场相机均达到 live/presentable=true。

程序：`/home/admin/rk3588_handover_live_20261020/app/build-target/railhmi`

启动：`DISPLAY=:0 QT_QUICK_BACKEND=software /home/admin/rk3588_handover_live_20261020/run-railhmi.sh`

SHA-256：`ec2173d558f8ef6620a9109cf8b1b00d25ee7a79ae0f5efe90eb397fb54c347f`

`all-functions.png` 是应用外壳在测试源下的截图；现场画面的视觉验收由用户完成。
构建、核心测试、Qt 测试及现场启动日志同目录归档。
