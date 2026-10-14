# 铁轨异物检测项目 - 完整流程文档

> 本文档面向「不熟悉整个流程」的读者，从项目目标、数据集、训练、转换、部署到运行 demo 一步步讲清楚。
> 训练脚本细节请看 `README.md`，本文档是「全局地图」。

---

## 1. 项目最终目标

通过 RTSP 摄像头实时检测铁轨及轨道上的异物（人、树枝、碎石等），运行在 RK3588 上，发现异物即报警。

**架构（两阶段管道，未来阶段 2b 实现）：**

```
RTSP 摄像头 ─┐
             ├─→ rail seg (YOLOv8-seg)  ──→ 铁轨 ROI mask
             └─→ yolov5 detect (COCO)   ──→ 人/车/物体 bbox
                                              ↓
                              检测框中心 in rail mask = 报警
```

为什么要两阶段？因为只靠 yolov5 detect（COCO 80 类）会把画面里所有人/车都报出来，包括不在铁轨上的。加上 rail seg 限定「只看铁轨上」的异物，误报率会大幅下降。

---

## 2. 硬件与账号

| 角色 | 地址 | 账号 | 说明 |
|------|------|------|------|
| RK3588 板子 | `192.168.0.103` | admin / admin | SSH 22 端口；demo 在这跑 |
| IP 摄像头 | `192.168.0.162` | admin / 123456 | RTSP URL: `rtsp://admin:123456@192.168.0.162:554/h264/ch1/main/av_stream` |
| 云训练机 | `36.129.34.186:6001` | root / mnxw6VY5Bn$NUrYR | Tesla T4 16GB, Ubuntu 22.04, Python 3.12 |
| 本地 PC | E:\work\ | - | Windows + Python 3.13（不能转 RKNN，只能训练/导出） |

**SSH 访问 RK3588（本地 PC 上）：**
```bash
# 用 plink (PuTTY)
plink -ssh -pw admin admin@192.168.0.103

# 或用 Python paramiko（推荐，方便脚本化）
python -c "import paramiko; s=paramiko.SSHClient(); s.set_missing_host_key_policy(paramiko.AutoAddPolicy()); s.connect('192.168.0.103', username='admin', password='admin')"
```

---

## 3. 阶段进展

| 阶段 | 内容 | 状态 |
|------|------|------|
| 0 | RTSP+GStreamer+MPP+RGA 摄像头接入验证 | 已完成 2026-08-03 |
| 1 | yolov5 demo 接入 RTSP+RGA 管道（COCO 80 类检测） | 已完成 2026-08-03 |
| 2a | rail seg 模型训练 + RKNN 部署验证 | 已完成 2026-08-05 |
| 2b | 两阶段集成（rail seg + yolov5 交集报警） | 未来 |
| 3 | 自定义训练树枝/碎石等非 COCO 类别 | 未来 |

---

## 4. 数据集

- **位置**：`E:\work\railway.v4i.yolov8\`（Roboflow 导出）
- **类别**：1 类 `rail`，polygon segmentation 格式
- **规模**：5310 train / 109 valid / 100 test
- **坑**：Roboflow 导出混入了 358 个 bbox 格式标签（class + 4 数字，覆盖整张图，疑似背景/噪声）。seg 训练会报 `len(segments) != len(boxes)`，已删除（train 349 / valid 2 / test 7）。
- **重要**：图里有 PS 合成的异物（车、人、兔子等），对 rail seg 训练是免费遮挡增强，但**不能用来训异物检测**，因为标签只有 rail。

---

## 5. 训练流程（云机器上）

### 5.1 训练参数（最终采用）
- 模型：`yolov8s-seg`（small 版本，平衡精度和速度）
- batch=16, epochs=200, AdamW, cos_lr, patience=50
- copy_paste=0.3（copy-paste 增强）, close_mosaic=10, amp=False
- 训练 9.78 小时跑完，epoch 157 早停（best at epoch 107）

### 5.2 训练结果
- Box mAP50 = 0.965
- Box mAP50-95 = 0.920
- Mask mAP50 = 0.957
- **Mask mAP50-95 = 0.877**（最关键的指标）

### 5.3 训练脚本
全部在 `E:\work\rail_seg_train\`：
- `train.py` - 训练入口
- `eval.py` - 评估脚本（在 test 集上跑 mAP）
- `data.yaml` - 数据集配置（**需要改 path** 指向云机器上的数据集路径）

### 5.4 训练命令
```bash
# 在云机器上
cd /root/rail_seg_train
python train.py  # 后台跑，log 重定向到 train.log
tail -f train.log  # 看进度
```

权重产出在 `/root/rail_seg_train/runs/segment/rail-3/weights/best.pt`（路径嵌套是前两次失败重试留下的）。

---

## 6. 模型转换流程

### 6.1 关键坑：ONNX 输出格式

标准 ultralytics 的 `model.export(format='onnx')` 导出 **2 个 tensor**（合并输出 + proto）。

但 rknn_model_zoo 的 `examples/yolov8_seg/cpp/postprocess.cc` 期望 **13 个 tensor**：
```
3 个 stride × [box(64), cls(1), sum(1), mc(32)] = 12 个
+ proto(32, 160, 160) = 1 个
合计 13 个
```

这是 airockchip fork 的格式，标准 ultralytics 没有。**装 fork 会和 ultralytics 8.4.115 冲突**（fork 是 8.2.82，import 会报错）。

### 6.2 解决方案：monkey-patch

用 `E:\work\rail_seg_train\export_rknn_format_onnx.py`，monkey-patch `Detect.forward` 和 `Segment.forward`，复刻 airockchip fork 的 13-tensor 输出格式。不装 fork，直接在标准 ultralytics 上改。

```python
# 关键逻辑（已在 export_rknn_format_onnx.py 里实现）
head.export = True
head.format = 'rknn'
head.forward = segment_forward_rknn.__get__(head, type(head))
torch.onnx.export(base_model, dummy, output_path, ...)
```

### 6.3 RKNN 转换

```bash
# 在云机器（Python 3.12 兼容 rknn-toolkit2，本地 3.13 不兼容）
cd /root/rail_seg_train
python convert_rknn.py  # 输出 exports/yolov8s-seg.rknn
```

- int8 量化（w8a8）
- 大小 13.3 MB
- 转换耗时约 5 分钟（13 输出比 2 输出慢）

### 6.4 下载到本地
```bash
# 用 paramiko sftp.get 从云机器下载
python -c "
import paramiko
s = paramiko.SSHClient()
s.set_missing_host_key_policy(paramiko.AutoAddPolicy())
s.connect('36.129.34.186', port=6001, username='root', password='mnxw6VY5Bn\$NUrYR')
s.open_sftp().get('/root/rail_seg_train/exports/yolov8s-seg.rknn', r'E:\work\rail_seg_train\exports\yolov8s-seg.rknn')
"
```

### 6.5 推到 RK3588
```bash
# 上传到 install 目录
python -c "
import paramiko
s = paramiko.SSHClient()
s.set_missing_host_key_policy(paramiko.AutoAddPolicy())
s.connect('192.168.0.103', username='admin', password='admin')
s.open_sftp().put(r'E:\work\rail_seg_train\exports\yolov8s-seg.rknn', '/home/admin/rknn_model_zoo/install/rk3588_linux_aarch64/rknn_yolov8_seg_demo/model/yolov8s-seg.rknn')
"
```

---

## 7. demo 代码与编译

### 7.1 修改的文件（在 `E:\work\rknn_model_zoo\examples\yolov8_seg\`）

| 文件 | 修改内容 |
|------|----------|
| `cpp/main.cc` | 完全重写，支持 image / video / RTSP 三种输入 |
| `cpp/postprocess.h` | `OBJ_CLASS_NUM` 从 80 改成 1（rail 单类） |
| `model/rail_labels.txt` | 新建，内容就一行 `rail` |

### 7.2 main.cc 关键改动

- 加 `is_image_file()` 按扩展名判断输入类型
- 加 `is_rtsp_url()` 判断 RTSP
- 抽出 `process_frame()` 函数：BGR -> RGB -> letterbox -> RKNN 推理 -> 画掩码 -> 画 bbox
- 三种输入路径：
  - 图片：`cv::imread()` + `waitKey(0)`（按键退出）
  - 视频：`cv::VideoCapture(input, cv::CAP_FFMPEG)` + `waitKey(1)`
  - RTSP：`cv::VideoCapture(pipeline, cv::CAP_GSTREAMER)` + `waitKey(1)`
- RTSP pipeline 用 RGA 硬件颜色转换：`rtspsrc ! rtph265depay ! h265parse ! mppvideodec ! rgaconvert ! video/x-raw,format=BGR,width=1280,height=720 ! appsink`

### 7.3 在 RK3588 上编译

```bash
ssh admin@192.168.0.103
cd ~/rknn_model_zoo
bash build-linux.sh -t rk3588 -a aarch64 -d yolov8_seg
# 产物在 install/rk3588_linux_aarch64/rknn_yolov8_seg_demo/
```

**坑**：build-linux.sh 在 `~/rknn_model_zoo/` 根目录，不在 examples 下。

---

## 8. 在 RK3588 上跑 demo

### 8.1 准备工作
```bash
# 必须有显示器（或 VNC）连着 RK3588，因为要 imshow
# 必须设 DISPLAY 环境变量
export DISPLAY=:0
```

### 8.2 三种运行方式

```bash
cd ~/rknn_model_zoo/install/rk3588_linux_aarch64/rknn_yolov8_seg_demo

# 方式 1：静态图（必须 BMP，不能 JPG！见第 9 节坑）
./rknn_yolov8_seg_demo model/yolov8s-seg.rknn /tmp/test_rail.bmp
# 任意键退出

# 方式 2：视频文件
./rknn_yolov8_seg_demo model/yolov8s-seg.rknn /path/to/video.mp4
# 按 q 退出

# 方式 3：RTSP 实时流
./rknn_yolov8_seg_demo model/yolov8s-seg.rknn "rtsp://admin:123456@192.168.0.162:554/h264/ch1/main/av_stream"
# 按 q 退出
```

### 8.3 后台跑（推荐用 SSH 时）
```bash
# 用 timeout 避免 imshow 阻塞导致 SSH hang
DISPLAY=:0 timeout 30 ./rknn_yolov8_seg_demo model/yolov8s-seg.rknn /tmp/test_rail.bmp 2>&1 | tail -20
```

### 8.4 期望输出
```
load lable ./model/coco_80_labels_list.txt
model input num: 1, output num: 13   ← 关键，必须是 13
input tensors: [1, 640, 640, 3] INT8
output tensors: 13 个，dims = [64/1/1/32, 80/40/20] + proto[32, 160, 160]
...
Opening image: /tmp/test_rail.bmp
Image size: 640x640. Press any key in window to exit.
scale=1.000000 dst_box=(0 0 639 639) ...
rga_api version 1.10.1_[0]
-- matmul_by_cpu_uint8 use: 5.3 ms
-- resize_by_opencv_uint8 use: 1.5 ms
-- crop_mask_uint8 use: 1.7 ms
-- seg_reverse use: 0.2 ms
rail @ (337 376 573 640) 0.860    ← 检测结果
rail @ (0 442 225 638) 0.389
```

显示器上会看到原图 + 半透明掩码（红色）+ 绿色 bbox + `rail 86.0%` 标签。

---

## 9. 已知坑（重要）

### 9.1 demo 读不了 JPEG/PNG

**现象**：`cv::imread` 返回空 Mat，但 BMP 正常。Python `cv2.imread` 同图同库却 OK。

**根因**：demo 二进制间接链接了 `libgdal.so.30`（OpenCV videoio/highgui 的 transitive dep），libgdal 又链了 libjpeg.so.8。多份 JPEG 实例让 OpenCV imgcodecs 的 codec 注册乱掉。最小测试程序（只链 imgcodecs）不受影响。

**规避**：静态图测试用 BMP。
```bash
# RK3588 上把 JPG 转 BMP
python3 -c "import cv2; cv2.imwrite('/tmp/x.bmp', cv2.imread('/tmp/x.jpg'))"
```

**不受影响**：RTSP 流（GStreamer 解码）和视频文件（FFmpeg 解码）都不走 imgcodecs，正常。

详见 memory：`opencv_jpeg_vs_libgdal_conflict.md`。

### 9.2 build-linux.sh 会清空 install 目录

每次 `bash build-linux.sh` 后，install 目录里手动放的文件（比如 test_rail.jpg）会被删。**测试图放 `/tmp/`，别放 install 目录**。

### 9.3 librknnrt.so 的 RPATH 问题

demo 的 RPATH 是 `$ORIGIN/../lib`，解析到 `install/rk3588_linux_aarch64/lib/`（不存在），所以加载的是系统 `/lib/librknnrt.so`。两个文件 md5 相同，不影响功能，但理解 ldd 输出时要知道这点。

### 9.4 训练时 Roboflow 数据集的 bbox 标签

Roboflow 导出 `.v4i.yolov8` 时会混入 bbox 格式标签（class + 4 个数字，覆盖整张图）。seg 训练会报 `len(segments) != len(boxes)`。**训练前先删掉这些 bbox 文件**。

### 9.5 RKNN 转换的 Python 版本

`rknn-toolkit2` 不支持 Python 3.13（本地 PC 是 3.13）。**转换必须在云机器（Python 3.12）上跑**。

### 9.6 旧进程占 NPU

之前跑的 demo 如果没正常退出，会一直占着 NPU。新 demo 跑起来会变慢或失败。**跑前先 `ps aux | grep rknn_yolov` 检查，有就 kill**。

---

## 10. 文件位置速查

### 10.1 本地 PC (E:\)

| 路径 | 内容 |
|------|------|
| `E:\work\rail_seg_train\` | 训练脚本（train/eval/export/convert） |
| `E:\work\rail_seg_train\README.md` | 训练脚本说明 |
| `E:\work\rail_seg_train\PROJECT_GUIDE.md` | **本文档** |
| `E:\work\rail_seg_train\export_rknn_format_onnx.py` | 13-tensor ONNX 导出脚本 |
| `E:\work\rail_seg_train\exports\yolov8s-seg.rknn` | 最终 RKNN 模型（13.3 MB） |
| `E:\work\railway.v4i.yolov8\` | 数据集 |
| `E:\work\rknn_model_zoo\examples\yolov8_seg\cpp\main.cc` | demo 源码 |
| `E:\work\rknn_model_zoo\examples\yolov8_seg\cpp\postprocess.h` | 后处理头文件（OBJ_CLASS_NUM=1） |
| `E:\work\rknn_model_zoo\examples\yolov8_seg\model\rail_labels.txt` | 标签文件 |

### 10.2 云机器 (/root/)

| 路径 | 内容 |
|------|------|
| `/root/railway.v4i.yolov8/` | 解压后的数据集 |
| `/root/rail_seg_train/` | 训练脚本（同本地） |
| `/root/rail_seg_train/runs/segment/rail-3/weights/best.pt` | 训练产出权重 |
| `/root/rail_seg_train/exports/yolov8s-seg.rknn` | RKNN 模型（云机器上转换产出） |

### 10.3 RK3588 (/home/admin/)

| 路径 | 内容 |
|------|------|
| `~/rknn_model_zoo/` | rknn_model_zoo 仓库 |
| `~/rknn_model_zoo/build-linux.sh` | 编译脚本 |
| `~/rknn_model_zoo/examples/yolov8_seg/cpp/main.cc` | demo 源码（已改） |
| `~/rknn_model_zoo/install/rk3588_linux_aarch64/rknn_yolov8_seg_demo/rknn_yolov8_seg_demo` | demo 可执行文件 |
| `~/rknn_model_zoo/install/rk3588_linux_aarch64/rknn_yolov8_seg_demo/model/yolov8s-seg.rknn` | RKNN 模型 |
| `~/rknn_model_zoo/install/rk3588_linux_aarch64/rknn_yolov8_seg_demo/lib/` | librknnrt.so, librga.so |
| `/tmp/test_rail.bmp` | 测试图（BMP 格式，已验证） |

---

## 11. 验证测试（当前状态）

**已通过**：用 `/tmp/test_rail.bmp` 测试，模型检测到两条铁轨：
- rail @ (337, 376, 573, 640) 置信度 0.860（右下区域）
- rail @ (0, 442, 225, 638) 置信度 0.389（左下区域）

推理耗时 ~8.5 ms/帧（单帧理论 100+ FPS）。

**待做**：
- 用户提供有铁轨场景的视频文件，跑视频 demo
- 或把摄像头对准铁轨，跑 RTSP 实时 demo

---

## 12. 下一步（阶段 2b）

两阶段集成：

```
RTSP 帧 ──┬─→ rail seg demo ──→ rail mask
          └─→ yolov5 detect demo ──→ bbox 列表
                                       ↓
                          Python/C++ 后处理脚本
                                       ↓
                    检测框中心 in rail mask → 报警
```

可能的实现方式：
- 方案 A：两个 RKNN 模型同时跑（一个进程里加载两个模型），共享输入帧
- 方案 B：两个独立进程，通过共享内存/管道传递结果
- 方案 C：单进程跑 rail seg，bbox 检测交给现有的 yolov5 demo（IPC 通信）

推荐方案 A，性能最好，但代码改动大。

---

## 13. 常用命令速查

```bash
# SSH 到 RK3588
plink -ssh -pw admin admin@192.168.0.103

# SSH 到云机器
plink -ssh -P 6001 -pw "mnxw6VY5Bn\$NUrYR" root@36.129.34.186

# RK3588 上编译 demo
cd ~/rknn_model_zoo && bash build-linux.sh -t rk3588 -a aarch64 -d yolov8_seg

# RK3588 上跑静态图（BMP）
cd ~/rknn_model_zoo/install/rk3588_linux_aarch64/rknn_yolov8_seg_demo
DISPLAY=:0 ./rknn_yolov8_seg_demo model/yolov8s-seg.rknn /tmp/test_rail.bmp

# RK3588 上跑 RTSP
DISPLAY=:0 ./rknn_yolov8_seg_demo model/yolov8s-seg.rknn "rtsp://admin:123456@192.168.0.162:554/h264/ch1/main/av_stream"

# 检查 NPU 占用
ps aux | grep rknn_yolov | grep -v grep

# 杀进程
kill <PID>
```
