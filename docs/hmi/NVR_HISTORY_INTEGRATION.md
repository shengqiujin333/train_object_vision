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
不自动跟随跳转或将凭据发送到不同主机；错误信息脱敏。密码可由页面输入或环境变量提供。

## 配置与操作

`app/config/nvr.yaml` 为可选配置；不存在时沿用原有回放模型。

```yaml
nvr:
  enabled: true
  address: "http://192.168.1.4"
  api_prefix: "/digest"
  username: admin
  credential_env: TRAIN_NVR_CREDENTIAL
  timezone: Asia/Shanghai
  timestamp_mode: local
  response_channel_base: 0
  timeout_ms: 8000
  channels: []
```

`timestamp_mode: local` 按文档“本地时间”处理秒值；`unix` 则使用标准 UTC epoch 秒。
实际设备的时间语义应在认证成功后核对；两种模式均有测试。
查询按配置时区的当天零点至次日零点构建，避免将北京时间日期错查为 UTC 日期。
请求通道号为 1 起，文档示例 `ChannelNo` 为 0 起；设备如使用 1 起响应，设置 `response_channel_base:1`。

进入“历史录像”，填写账号密码并点击“登录录像机”，右侧显示设备实际返回的通道及权限。
选择摄像头、核对其对应的 NVR 通道、选择日期并查询；时间轴只显示本次真实响应。
通道选择在本次进程内记住；固定映射可写为：

```yaml
  channels:
    - camera: CAM-01
      channel: 3
```

其中 `3` 只是格式示例，必须替换为现场核对的通道，不能当作已确认映射。
页面密码提交后输入框清空，不保存到配置或 Git。部署环境也可以从本机私有凭据文件导出变量。

## 回放范围

从录像时间轴选择有录像的时刻后，客户端请求设备 `/frmGetRtspUrl`（`Type:13`）返回标准 RTSP 回放地址，
请求包含通道、起止时间及 `TimeMode:Local`，不硬编码推测 RTSP 录像路径。
该地址接口未列于本次分享的“录像回放”目录，属于需要现场确认的设备能力；不支持时显示真实错误。
只有来自同一录像机主机的 RTSP/RTSPS 地址才可使用，收到地址不等于播放成功。

解码会话独立于六路实时取流，收到解码帧后才显示播放中；暂停作用于媒体管线。
播放游标使用实际解码 PTS 的相对推进量加本次请求起点，未验证为设备绝对拍摄时钟，不宣称多路精确同步。
当前开放单路、1 倍速及按时间重新请求回放；三联同步、其他倍速和 NVR 导出尚未验证，控件禁用。
离开页面会停止回放。没有制造缩略图或示例时间段来替代真实响应。

## 验证与实机状态

- RK3588 目标构建成功。
- 新增 Qt/TCP 模拟设备测试 27 项通过：包括 Digest 挑战及摘要校验、时间转换、通道基数、
  权限、成功空列表、认证失败、超时、非法响应、过期请求隔离、页面真实 HTTP 查询及实际解码像素。
- 原有视频/六页导航集成回归 21 项通过。
- 对现场录像机验证 `admin` 账号及用户授权的三个默认密码均未通过认证；编译后的同一 C++ 客户端
  实机探针也如实返回认证失败。尚未获得现场录像列表或验证现场 RTSP 历史画面，不能声称实机闭环完成。

```sh
cd app
cmake -S . -B build-target -DRAILHMI_BUILD_TARGET=ON -DRAILHMI_BUILD_UI_TESTS=ON
cmake --build build-target --target railhmi railhmi-nvr-test -j4
QT_QUICK_BACKEND=software xvfb-run -a -s '-screen 0 1280x720x24' \
  ./build-target/src/ui/railhmi-nvr-test /tmp
# 读取目标私有环境变量后，验证实际设备（不输出密码）：
QT_QPA_PLATFORM=offscreen ./build-target/src/ui/railhmi-nvr-test --probe config/nvr.yaml
```

日志与截图见 `docs/hmi/test_evidence/NVR-HISTORY-20261026/`；截图使用模拟设备响应，不是现场录像。
