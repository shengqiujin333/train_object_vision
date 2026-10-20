# 主程序实时视频接入与 RK3588 验证

日期：2026-10-20。目标：`admin@192.168.0.103`（aarch64）。

## 修改

- 主程序使用配置构建六路 CameraWorker，通过现有 DisplayRegistry 取拥有像素的帧快照。
- VideoChannel 为固定 QObject；状态更新不会销毁 P4 委托，切换页面不重建取流。
- VideoFrame 用 Qt 绘制独立 QImage，BGR→RGB 后保持宽高比；不向 QML 暴露 GstSample 地址。
- P1 按 left/primary/right 绑定实际状态、画面、FPS、最近帧、重连数；摄像头关键状态汇总随运行更新。
- 帧显示同时检查流状态、帧 generation 和成功转换帧的新鲜度。单帧 RGA 失败可保留期限内的成功帧，超时即清空。
- 凭据读取 cameras.yaml 指定的环境变量；缺失时对应路离线且明确提示；GL 未实现时明确不可用。
- 二进制输出到 build-target/railhmi，与发布/安装工具约定一致；systemd 支持本机私有凭据文件。

本轮仅接六路可见光，符合根目录“一些说明和要求.txt”与“功能需求.docx”的当前范围。

## 板端结果

1. `bash build.sh Release` 成功，`file` 确认为 ARM aarch64 ELF；动态库无 `not found`。
2. `railhmi-video-test`：13 项 PASS，`VIDEO_TEST failures=0`，exit=0。
   覆盖六路拥有像素的图像、P1 实际绘制像素/position 绑定、单路冻结清旧帧、其余路继续、恢复、
   P4 六槽位/委托稳定、切页持续出帧、停止清空、缺凭据明确离线。
3. 实际主程序使用现场 cameras.yaml 和私有凭据，在 Xvfb 上运行 35 秒：6/6 worker 启动，
   CAM-01..06 全部进入 `live`、`presentable=true`。最后一轮六路进入可展示后无闪空状态切换。
   `timeout` 返回 124 表示运行到预定时限；未出现 QML ReferenceError、TypeError 或属性赋值错误。
4. 主机源码检查：QML/Qt 合法性、UI 令牌、打包核对、`git diff --check` 全部通过。

日志内仍有板端既有 RGA 偶发转换失败；显示层按成功帧时间限期保留，不将过期画面当实时。
本轮为有界功能验证，未作长时间稳定性结论。PNG 是测试源截图，实际摄像头内容的视觉验收由用户完成。

## 执行文件与启动

源码目录：`/home/admin/rk3588_handover_live_20261020/app`

执行文件：`/home/admin/rk3588_handover_live_20261020/app/build-target/railhmi`

SHA-256：`37daa2a4b663ddf0c025f8873ad8592b640214568d025ac17d306e014862dcd9`

已在板上准备启动脚本（自动加载本机私有凭据）：

```sh
DISPLAY=:0 /home/admin/rk3588_handover_live_20261020/run-railhmi.sh
```

私有凭据仅在板端 `camera-credentials.env`，权限 0600，不进入 Git。
本轮在独立目录上传、编译和测试；未操作原有桌面实例或替换系统服务。

## 重现测试

```sh
cd /home/admin/rk3588_handover_live_20261020/app
cmake -S . -B build-target -DRAILHMI_BUILD_TARGET=ON -DRAILHMI_BUILD_UI_TESTS=ON
bash build.sh Release
QT_QUICK_BACKEND=software xvfb-run -a -s '-screen 0 1280x720x24' \
  ./build-target/src/ui/railhmi-video-test config ../evidence
```

证据：`build.log`、`video-test.log`、`live-camera-smoke.log`、`artifact.txt`、`video-P1.png`、`video-P4.png`。
