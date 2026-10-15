# railhmi 依赖 / 构建环境核验原始报告

- 探针: `app/tools/env_probe.py` (只读, 标准库); 编号对齐 `DESKTOP_APP_DESIGN` §3.4
- 生成时间: 2026-10-15 16:26:48
- 执行环境: hostname=`DESKTOP-476JGQ4` system=`Windows 10` machine=`AMD64` python=`3.13.9`

> 环境判定: 若 machine 含 `aarch64`/`arm64` 且存在 `/sys/kernel/debug/rknpu`，则本报告为目标板 (RK3588) 结果；否则为**主机**结果，不能作为目标板结论。

## E-00 执行环境与工具链（扩展项）

- 目的: 确认 ABI/架构与编译工具链

```
$ uname -a
# exit=0
MINGW64_NT-10.0-19045 DESKTOP-476JGQ4 3.6.7-fb42d713.x86_64 2026-03-29 11:44 UTC x86_64 Msys
```

```
$ python3 --version
# exit=0
Python 3.13.9
```

```
$ gcc --version
(command not found: gcc)
```

```
$ g++ --version
(command not found: g++)
```

```
$ cmake --version
(command not found: cmake)
```

```
$ pkg-config --version
(command not found: pkg-config)
```

```
$ dpkg --print-architecture
(command not found: dpkg)
```

## E-01 Qt 版本与平台插件

- 目的: 确认 Qt5.15 与 xcb/eglfs 平台插件

```
$ qmake -v
# exit=0
QMake version 3.1
Using Qt version 5.15.2 in D:/Users/123/anaconda3/Library/lib
```

```
$ qmake6 -v
(command not found: qmake6)
```

```
$ dpkg -l | grep -i qt5 (filtered)
# exit=0
(no output)
```

- Qt 平台插件目录: `(not found)`

## E-02 GStreamer Qt sink (qmlglsink)

- 目的: 确认 gstreamer1.0-qt5 与 qmlglsink

```
$ gst-inspect-1.0 --version
(command not found: gst-inspect-1.0)
```

```
$ gst-inspect-1.0 qmlglsink
(command not found: gst-inspect-1.0)
```

```
$ dpkg -l | grep gstreamer1.0-qt (filtered)
# exit=0
(no output)
```

## E-03 GL/EGL (Mali)

- 目的: 确认 EGL/GLES 驱动与 renderer

```
$ eglinfo
(command not found: eglinfo)
```

```
$ glxinfo -B
(command not found: glxinfo)
```

```
$ ls /proc/driver/mali
# exit=0
ls: cannot access '/proc/driver/mali': No such file or directory
```

## E-04 MPP 解码与 GL 元素

- 目的: 确认 mppvideodec/glupload/glcolorconvert/rgavideoconvert/glimagesink 存在

```
$ gst-inspect-1.0 mppvideodec
(command not found: gst-inspect-1.0)
```

```
$ gst-inspect-1.0 glupload
(command not found: gst-inspect-1.0)
```

```
$ gst-inspect-1.0 glcolorconvert
(command not found: gst-inspect-1.0)
```

```
$ gst-inspect-1.0 rgavideoconvert
(command not found: gst-inspect-1.0)
```

```
$ gst-inspect-1.0 rtspsrc
(command not found: gst-inspect-1.0)
```

```
$ gst-inspect-1.0 h265parse
(command not found: gst-inspect-1.0)
```

```
$ gst-inspect-1.0 h264parse
(command not found: gst-inspect-1.0)
```

```
$ gst-inspect-1.0 appsink
(command not found: gst-inspect-1.0)
```

```
$ gst-inspect-1.0 glimagesink
(command not found: gst-inspect-1.0)
```

## E-05 RGA (librga)

- 目的: 确认 RGA 库与转换元素

```
$ gst-inspect-1.0 rgavideoconvert
(command not found: gst-inspect-1.0)
```

```
$ ldconfig -p | grep -i rga (filtered)
# exit=0
(no output)
```

- librga: (not found)

## E-06 NPU 运行时 (librknnrt)

- 目的: 确认 2.3.2 与 rknpu 驱动版本可读

```
$ cat /sys/kernel/debug/rknpu/version
# exit=1
cat: /sys/kernel/debug/rknpu/version: No such file or directory
```

```
$ cat /sys/kernel/debug/rknpu/load
# exit=1
cat: /sys/kernel/debug/rknpu/load: No such file or directory
```

```
$ ldconfig -p | grep -i rknnrt (filtered)
# exit=0
(no output)
```

- librknnrt: (not found)

## E-07 SQLite

- 目的: 确认 sqlite3 与库

```
$ sqlite3 --version
# exit=0
3.50.6 2025-09-22 18:50:24 9cc554372371e07bf01d8070a64d461ac85b91be6eef0e642ed129a0c7fa8105 (32-bit)
```

```
$ python3 -c 'import sqlite3; print(sqlite3.sqlite_version)'
# exit=0
3.51.0
```

- libsqlite3: (not found)

## E-08 显示会话与图形目标

- 目的: 确认 X11 DISPLAY/XAUTHORITY 与 systemd 默认目标

```
$ printenv DISPLAY
# exit=0
needs-to-be-defined
```

```
$ printenv XAUTHORITY
# exit=0
(XAUTHORITY unset)
```

```
$ ls /tmp/.X11-unix
# exit=0
ls: cannot access '/tmp/.X11-unix': No such file or directory
```

```
$ systemctl get-default
(command not found: systemctl)
```

```
$ loginctl list-sessions
(command not found: loginctl)
```

## E-09 磁盘容量

- 目的: 确认根分区与运行目录可用空间

```
$ df -h /
# exit=0
Filesystem            Size  Used Avail Use% Mounted on
C:/Program Files/Git  118G  115G  3.4G  98% /
```

```
$ df -h /home/admin
# exit=1
df: /home/admin: No such file or directory
```

```
$ du -sh /home/admin (may be slow)
# exit=0
(no output)
```

- `/`: total=488.3 GiB used=36.2 GiB free=452.1 GiB (7% used)
- `/home/admin`: (unavailable)

## E-10 相机可达性

- 状态: **未执行**（默认关闭；网络探测需项目授权，`--probe-cameras` 开启）。

## E-11 NVR 接口

- 状态: **未执行**（需授权）。注: 本项仅能测端口可达性，NVR 回放/导出接口能力需厂商文档/现场核验，属 E2E/现场档。

## E-12 温度/调频/UDP 基线（扩展项）

- 目的: 确认 governor 与 UDP 缓冲配置（现有平台事实）

```
$ cat scaling_governor
# exit=1
cat: /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor: No such file or directory
```

```
$ systemctl is-enabled cpu-performance.service
(command not found: systemctl)
```

```
$ cat /etc/sysctl.d/99-udp-buffers.conf
# exit=1
cat: /etc/sysctl.d/99-udp-buffers.conf: No such file or directory
```

```
$ sysctl net.core.rmem_max
(command not found: sysctl)
```

## E-13 UI 渲染前提（扩展项, D-04）

- 目的: 字体（CJK/等宽）、触控输入、显示分辨率、QML 控件模块

```
$ fc-list | grep -iE 'Noto Sans CJK|Noto Sans Mono' (filtered)
# exit=0
(no output)
```

```
$ fc-list | wc -l
# exit=0
514
```

```
$ xinput list
(command not found: xinput)
```

```
$ libinput list-devices (needs root)
(command not found: libinput)
```

```
$ xrandr
(command not found: xrandr)
```

```
$ dpkg -l | grep qml-module-qtquick (filtered)
# exit=0
(no output)
```

```
$ ls QtQuick Controls 2 qml dir
# exit=0
ls: cannot access '/usr/lib/aarch64-linux-gnu/qt5/qml/QtQuick/Controls.2': No such file or directory
```

- Qt QML 目录: `(not found)`
- QtQuick Controls 目录: `(not found)`

---

## 结论口径

- 本文件只陈述**实际执行的命令与原始输出**；命令不存在/超时/未执行均如实记录。
- 主机结果仅证明主机环境；目标板结论必须以 machine=aarch64 且 rknpu 可读的同一脚本输出为准。
- 元素/库“存在”不等于“协商/运行成功”，运行时协商属 ITEM-002 渲染 spike。
