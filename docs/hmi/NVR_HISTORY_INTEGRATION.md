# IVR 历史录像接入

接口来源：用户提供的 [常规 IVR Web API](https://www.eolink.com/share/index?shareCode=1da60b6a050be3c4674d10be2a6976c0)。
本轮实现于 2026-10-26，目标为 RK3588 上的 Qt/QML 应用。

## 查询约定

- `POST /frmUserLogin`：`{"Type":0,"Ch":0,"Data":{}}`，读取登录结果。
- `POST /frmGetPlaybackInfo`：读取 `Data.Channels`，数组元素为 `[名称, 回放权限]`。
- `POST /frmVideoRecordsQuery`：按通道和起止时间查询。
  `Data` 包含 `Channels`、`QueryType`、`BeginDateTime`、`EndDateTime`、`QueryCondition:0`。
  解析 `Data.SearchResults[].ChannelNo` 和 `Records[][录像类型, 开始秒, 结束秒]`。
- `Result:0` 才表示查询成功。空列表、HTTP 401/403、超时、非 JSON、设备非零返回码分别处理。

现场 `192.168.1.4` 的普通 `/frm...` 路径不能直接使用；实际认证入口已探测到 `/digest/`，
设备返回 `WWW-Authenticate: X-Digest ... algorithm=MD5`。
客户端支持该挑战格式及标准 Digest/MD5、MD5-sess，单个请求至多重试一次，禁止自动猜密码。
与设备 20260422 版网页配套的[厂家公开播放工具包](https://ie-activex-nvr2x.oss-cn-hangzhou.aliyuncs.com/IvrPoePlugin3.0/IvrPoePluginSetup-3.0.6.4-N20260422-P20260313.exe)
中的静态资源核对后，X-Digest 保留同名认证头，
并使用完整 URL 计算摘要；标准 Digest 使用请求路径。两种行为分别测试。
现场还要求后续请求携带登录返回的 `S-HASH`、`X-HASH`、`C-HASH`。客户端保存完整会话头并随响应更新，
切换账号清空旧会话；没有会话时，页面先登录、取得通道权限，再执行录像查询。
实机偶发 HTTP 200 / `{}` 不完整响应；通道和录像只读查询对此延迟 150ms 重试一次，仍不完整则报错，
不会把空对象解释成“当天没有录像”。显式重新连接会清理旧会话。
不自动跟随跳转或将凭据发送到不同主机；错误信息脱敏。密码可由页面输入或环境变量提供。

## 配置与操作

`app/config/nvr.yaml` 为可选配置；不存在时沿用原有回放模型。

```yaml
nvr:
  enabled: true
  address: "http://192.168.1.4"
  api_prefix: "/digest"
  username: nvradmin
  credential_env: TRAIN_NVR_CREDENTIAL
  timezone: Asia/Shanghai
  timestamp_mode: local
  response_channel_base: 1
  timeout_ms: 8000
  playback_transport: tcp
  playback_legacy_control: true
  playback_url_template: "rtsp://192.168.1.4:554/recording?ch={channel}&stream=0&start={start}&stop={stop}&rec_inquiry_type=0"
# 六路映射见 app/config/nvr.yaml
```

`timestamp_mode: local` 按文档“本地时间”处理秒值；`unix` 则使用标准 UTC epoch 秒。
现场查询响应与设备本地日历时间对应；两种模式均有测试。
查询按配置时区的当天零点至次日零点构建，避免将北京时间日期错查为 UTC 日期。
请求通道号为 1 起，分享文档示例 `ChannelNo` 为 0 起；现场实测为 1 起，已设置 `response_channel_base:1`。

进入“历史录像”，使用板端私有环境凭据自动连接、查询；也可在页面填写其他有回放权限的账号密码并登录。
右侧显示设备实际返回的通道及权限。选择摄像头、日期并查询，时间轴只显示本次真实响应。
已依据 `/frmGetPreviewInfo` 的摄像头 IP 和真实录像查询核对六路映射：

| 程序摄像头 | IP | NVR 通道 |
| --- | --- | --- |
| CAM-01 车头前方 | 192.168.1.65 | 1 |
| CAM-02 车辆左侧 | 192.168.1.160 | 8 |
| CAM-03 车辆右侧 | 192.168.1.162 | 4 |
| CAM-04 车顶受电弓 | 192.168.1.163 | 5 |
| CAM-05 车底轮轨 | 192.168.1.165 | 6 |
| CAM-06 司机室车门 | 192.168.1.167 | 7 |

NVR 的第 3 通道也登记了前方 IP，但本次未返回录像；前方使用有实际录像的第 1 通道。
页面密码提交后输入框清空，不保存到配置或 Git。部署环境也可以从本机私有凭据文件导出变量。
现场启动脚本已可选加载部署目录下的 `nvr-credentials.env`（权限 `0600`），
其中保存用户确认的 `TRAIN_NVR_CREDENTIAL`；该文件不在仓库内。

## 回放范围

现场 `/frmGetRtspUrl Type:13` 返回 `get rtsp type error !`；`Type:3` 返回厂商私有 `recording_comb` 地址。
实机核对同一设备的标准 `/recording` 路径可返回 H.265 SDP，因此使用已显式配置的标准回放模板。
模板按通道及本地起止时间替换，只允许同一录像机的 RTSP/RTSPS 地址，不允许内嵌凭据。
未配置模板的设备继续请求 `Type:13`，不自动猜测不同设备的路径。

本机 GStreamer 1.20 对 SDP 相对控制路径的处理与旧 IVR 不兼容；开启 `playback_legacy_control` 后，
仅将该设备的 `ctype=video/audio` 控制属性改为完整回放 URL 后追加的绝对地址，再交由 rtspsrc 取流。
该选项仅影响独立历史回放会话；现场使用 TCP 传输。

解码会话独立于六路实时取流，收到解码帧后才显示播放中；暂停作用于媒体管线。
播放游标使用实际解码 PTS 的相对推进量加本次请求起点，未验证为设备绝对拍摄时钟，不宣称多路精确同步。
当前开放单路、1 倍速及按时间重新请求回放；三联同步、其他倍速和 NVR 导出尚未验证，控件禁用。
离开页面会停止回放。没有制造缩略图或示例时间段来替代真实响应。

## 验证与实机状态

- RK3588 目标构建成功。
- 新增 Qt/TCP 模拟设备测试 37 项通过：包括 X-Digest、标准 Digest、会话头传递和轮换、切换账号清理、
  冷启动自动登录、空响应重试与次数限制、时间和通道基数、模板及同主机限制、超时/错误、过期请求隔离、页面查询和实际解码像素。
- 原有视频/六页导航集成回归 21 项通过。
- 用户提供 `nvradmin` 账号后，实机登录和通道接口均为 HTTP 200 / Result 0；共 10 通道，回放权限均为 1。
- 2026-10-26 的录像查询成功；六路实际使用的 NVR 通道 1、8、4、5、6、7 均返回录像时间段。
- 程序实际 C++ 解码器已收到前方历史 H.265 图像（1280×720），连续解码帧及 PTS 推进检查通过。
  六路分别查询、解码及 PTS 推进检查均退出 0，详见 `nvr-six-channel-playback.log`。
- 实际画面内容及相机朝向仍由用户验收；不将模拟截图当成现场录像，也不宣称精确同步或所有倍速已验证。

```sh
cd app
cmake -S . -B build-target -DRAILHMI_BUILD_TARGET=ON -DRAILHMI_BUILD_UI_TESTS=ON
cmake --build build-target --target railhmi railhmi-nvr-test -j4
QT_QUICK_BACKEND=software xvfb-run -a -s '-screen 0 1280x720x24' \
  ./build-target/src/ui/railhmi-nvr-test /tmp
# 读取目标私有环境变量后，验证实际设备（不输出密码）：
QT_QPA_PLATFORM=offscreen ./build-target/src/ui/railhmi-nvr-test --probe config/nvr.yaml
# 实机查询及解码检查（源凭据仍从私有环境加载，可选最后一项保存首帧）：
QT_QPA_PLATFORM=offscreen ./build-target/src/ui/railhmi-nvr-test \
  --probe config/nvr.yaml 1 2026-10-26 --playback /tmp/nvr-real-frame.png
```

日志与截图见 `docs/hmi/test_evidence/NVR-HISTORY-20261026/`；截图使用模拟设备响应，不是现场录像。
