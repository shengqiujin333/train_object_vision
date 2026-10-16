# railhmi 板端构建资源/支持范围探针（只读）

- host: `192.168.0.103`  user: `admin`  port: 22
- host_key_fingerprint: `SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU`
- generated: 20261017-release-r1

## os
```
PRETTY_NAME="Ubuntu 22.04.5 LTS"
NAME="Ubuntu"
VERSION_ID="22.04"
VERSION="22.04.5 LTS (Jammy Jellyfish)"
Linux admin 6.1.141 #128 SMP Thu Jun 18 09:56:06 CST 2026 aarch64 aarch64 aarch64 GNU/Linux
```

## arch
```
aarch64
8
64
```

## toolchain
```
gcc (Ubuntu 11.4.0-1ubuntu1~22.04.3) 11.4.0
cmake version 3.22.1
```

## qt_cmake
```
/usr/lib/aarch64-linux-gnu/cmake/Qt5Core
```

## qt_quick_cfg
```

```

## qt_pkgs
```
dpkg-query: no packages found matching qtdeclarative5-dev
dpkg-query: no packages found matching qml-module-qtquick2
dpkg-query: no packages found matching qml-module-qtquick-controls2
dpkg-query: no packages found matching libqt5svg5-dev
qtbase5-dev 5.15.3+dfsg-2ubuntu0.2 install ok installed
```

## gst
```
gst-inspect-1.0 version 1.20.3
1.20.3
```

## gst_plugins
```
1394:  dv1394src: Firewire (1394) DV video source
1394:  hdv1394src: Firewire (1394) HDV video source
a52dec:  a52dec: ATSC A/52 audio decoder
aasink:  aasink: ASCII art video sink
aasink:  aatv: aaTV effect
```

## gst_elems
```
rtspsrc=yes
mppvideodec=yes
glupload=yes
appsink=yes
```

## rga
```
2.1.0
/usr/include/rga/im2d.h
```

## rknn
```
/usr/lib/librknnrt.so
```

## sqlite
```
libsqlite3.so.0 (libc6,AArch64) => /lib/aarch64-linux-gnu/libsqlite3.so.0
```

## display
```
X0
X1001
active
```

## systemd
```
systemd 249 (249.11-0ubuntu4.16)
railhmi.service: not-installed
```

## cpu_service
```
enabled
active
```

## apt_reach
```
apt-reachable
```

## apt_policy
```
qtdeclarative5-dev:
  Installed: (none)
  Candidate: 5.15.3+dfsg-1
  Version table:
     5.15.3+dfsg-1 900
        900 http://wiki.t-firefly.com/firefly-ubuntu-repo jammy/main arm64 Packages
```

## sudo
```
sudo-nopass-ok
```

## disk
```
Filesystem      Size  Used Avail Use% Mounted on
/dev/root        57G   24G   31G  44% /
/dev/root        57G   24G   31G  44% /
```

## models_dir
```
bus.jpg
coco_80_labels_list.txt
rail_labels.txt
yolov5s.rknn
yolov8s-seg.rknn
```

## repo_dir
```
/opt/railhmi: absent
```
