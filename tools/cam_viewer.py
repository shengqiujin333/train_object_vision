# cam_viewer.py — Windows demo viewer for the vendor remote session.
# Shows BOTH cameras side by side (visible .65 + thermal .64) with the same
# diagnostics as the RK3588 demo (empty-frame skip, green artifact, stream
# gap, FPS), plus a live log panel and a big "mark stutter" button.
# Log is mirrored to cam_viewer_log.txt next to this script.
#
# usage:  python cam_viewer.py
# needs:  pip install opencv-python   (already installed)
# NOTE: the camera-switch cable must be plugged into THIS PC.
import os
import queue
import socket
import threading
import time
import tkinter as tk
from tkinter import scrolledtext

import cv2
from PIL import Image, ImageTk

# single-instance guard: kernel named mutex, atomic, race-free
import ctypes
_k32 = ctypes.WinDLL('kernel32', use_last_error=True)
_mutex = _k32.CreateMutexW(None, True, 'cam_viewer_instance')
if ctypes.get_last_error() == 183:  # ERROR_ALREADY_EXISTS
    raise SystemExit('cam_viewer already running')

os.environ['OPENCV_FFMPEG_CAPTURE_OPTIONS'] = (
    'rtsp_transport;udp|buffer_size;4194304|stimeout;5000000|max_delay;500000')

CAMS = {
    'visible': {
        'url': 'rtsp://admin:ipc12345@192.168.1.65:554/cam/realmonitor?channel=1&subtype=0',
        'title': 'visible .65 (Dahua main)',
        'panel': (640, 360),
    },
    'thermal': {
        'url': 'rtsp://admin:ipc12345@192.168.1.64/cam/realmonitor?channel=1&subtype=0',
        'title': 'thermal .64 (main H.265)',
        'panel': (576, 461),
    },
}
LOG_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        'cam_viewer_log.txt')
GAP_MS = 800
IDLE_REOPEN_POLLS = 20
STALL_REOPEN_S = 3.0
EMPTY_Y = 10.0

logq = queue.Queue()
logfile = open(LOG_FILE, 'a', buffering=1)


def log(msg):
    line = time.strftime('%H:%M:%S') + ' ' + msg
    logq.put(line)
    logfile.write(line + '\n')
    print(line, flush=True)


class Cam:
    def __init__(self, name, cfg):
        self.name = name
        self.url = cfg['url']
        self.title = cfg['title']
        self.panel = cfg['panel']
        self.lock = threading.Lock()
        self.frame = None
        self.fps = 0.0
        self.frames = 0
        self.skips = 0
        self.gaps = 0
        self.state = 'connecting'
        self.wh = ''
        self._fps_n = 0
        self._fps_t = time.time()
        self._last_ok = 0.0

    def publish(self, frame):
        now = time.time()
        with self.lock:
            self.frame = frame
        self._fps_n += 1
        if now - self._fps_t >= 5.0:
            self.fps = self._fps_n / (now - self._fps_t)
            log('%s: FPS: %.1f' % (self.name, self.fps))
            self._fps_n = 0
            self._fps_t = now
        if self._last_ok and (now - self._last_ok) * 1000 > GAP_MS:
            self.gaps += 1
            log('%s: stream gap %d ms' % (self.name, int((now - self._last_ok) * 1000)))
        self._last_ok = now
        self.frames += 1

    def reopen(self):
        self.state = 'reconnecting'
        log('%s: opening %s' % (self.name, self.url))
        wh = cam_panel_wh.get(self.name, (1280, 720))
        cap = FfmpegCapture(self.url, w=wh[0], h=wh[1])
        if not cap.isOpened():
            log('%s: open FAILED, retry in 2s' % self.name)
            cap.release()
            time.sleep(2)
            return None
        return cap


cam_panel_wh = {
    'thermal': (1280, 1024),
    'visible': (1280, 720),
}


class FfmpegCapture:
    """Standalone-FFmpeg subprocess capture: bypasses OpenCV's bundled
    libavformat (Lavf61) which cannot pull frames from the reflashed .64,
    while gyan ffmpeg 9.0 (Lavf63) streams it fine."""

    FFMPEG = (r'C:\Users\123\AppData\Local\Microsoft\WinGet\Packages'
              r'\Gyan.FFmpeg_Microsoft.Winget.Source_8wekyb3d8bbwe'
              r'\ffmpeg-9.0-full_build\bin\ffmpeg.exe')

    def __init__(self, url, w=0, h=0, buf_size=4194304):
        import subprocess as sp
        import numpy as np
        self.np = np
        self.w = w
        self.h = h
        self._frame_bytes = w * h * 3
        self._first_read = None
        self.proc = sp.Popen(
            [self.FFMPEG, '-nostdin', '-hide_banner', '-loglevel', 'error',
             '-nostats', '-rtsp_transport', 'udp', '-buffer_size',
             str(buf_size),
             '-i', url, '-f', 'rawvideo', '-pix_fmt', 'bgr24', '-'],
            stdout=sp.PIPE, stderr=sp.DEVNULL, stdin=sp.DEVNULL,
            creationflags=0x08000000)

    def isOpened(self):
        return self.proc is not None and self.proc.poll() is None

    def read(self):
        # first call: read header to detect 1280x1024/1280x720; bus size
        # needs to match incoming frame. We discover size by sniffing once.
        import subprocess as sp
        if self.proc is None:
            return False, None
        raw = self.proc.stdout.read(self._frame_bytes)
        if raw is None or len(raw) < self._frame_bytes:
            return False, None
        frame = self.np.frombuffer(raw, dtype=self.np.uint8).reshape(
            (self.h, self.w, 3))
        return True, frame.copy()

    def release(self):
        try:
            if self.proc:
                self.proc.kill()
        except Exception:
            pass
        self.proc = None


def cam_thread(cam):
    fail = 0
    while True:
        cam._last_ok = 0.0
        cap = cam.reopen()
        if cap is None:
            continue
        first = True
        while True:
            ok, frame = cap.read()
            if not ok:
                fail += 1
                if fail >= IDLE_REOPEN_POLLS:
                    log('%s: no frames for %d polls, reconnecting' % (cam.name, fail))
                    cam.state = 'no frames, reconnecting'
                    break
                continue
            fail = 0
            now = time.time()
            if cam._last_ok and now - cam._last_ok > STALL_REOPEN_S:
                log('%s: stalled %.1fs, reconnecting' % (cam.name, now - cam._last_ok))
                cam.state = 'stalled, reconnecting'
                break
            if first:
                cam.wh = '%dx%d' % (frame.shape[1], frame.shape[0])
                cam.state = 'streaming'
                log('%s: opened, first frame %s' % (cam.name, cam.wh))
                first = False
            if cam.name == 'thermal':
                ymean = float(cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY).mean())
                if ymean < EMPTY_Y:
                    cam.skips += 1
                    log('%s: skipped empty frame #%d (Y mean %.2f)'
                        % (cam.name, cam.skips, ymean))
                    continue
                b, g, r = [float(x) for x in cv2.mean(frame)[:3]]
                if g > 50 and b < 0.2 * g and r < 0.2 * g:
                    cam.skips += 1
                    log('%s: skipped green artifact frame #%d (BGR %.1f/%.1f/%.1f)'
                        % (cam.name, cam.skips, b, g, r))
                    continue
            cam.publish(frame)
        cap.release()


def overlay(cam):
    with cam.lock:
        frame = None if cam.frame is None else cam.frame.copy()
    if frame is None:
        return None
    t = cam.title
    info = 'FPS %.1f  frames %d  skips %d  gaps %d  %s  [%s]' % (
        cam.fps, cam.frames, cam.skips, cam.gaps, cam.wh, cam.state)
    cv2.putText(frame, t, (10, 28), cv2.FONT_HERSHEY_SIMPLEX, 0.8,
                (0, 255, 255), 2, cv2.LINE_AA)
    cv2.putText(frame, info, (10, frame.shape[0] - 12),
                cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 255, 0), 2, cv2.LINE_AA)
    return frame


class App:
    def __init__(self, root):
        self.root = root
        root.title('camera viewer — vendor demo')
        self.imgs = {}
        self.labels = {}
        self.stats = {}
        row = tk.Frame(root)
        row.pack(padx=6, pady=6)
        for name, cam in cams.items():
            cell = tk.Frame(row)
            cell.pack(side='left', padx=6)
            self.labels[name] = tk.Label(cell, bg='black',
                                         width=cam.panel[0], height=cam.panel[1] // 8)
            self.labels[name].pack()
            self.stats[name] = tk.Label(cell, text=name + ': starting', anchor='w')
            self.stats[name].pack(fill='x')
        bar = tk.Frame(root)
        bar.pack(fill='x', padx=6)
        tk.Button(bar, text='标记卡顿 (marker)', bg='#ff9090', font=('sans', 12, 'bold'),
                  command=self.marker).pack(side='left', padx=4, pady=4)
        tk.Label(bar, text='  看到卡顿/异常时点一下，日志里会写入时间标记').pack(side='left')
        self.logbox = scrolledtext.ScrolledText(root, height=16, font=('Consolas', 9))
        self.logbox.pack(fill='both', expand=True, padx=6, pady=6)
        log('viewer started; log file: %s' % LOG_FILE)
        root.after(33, self.tick)
        root.after(200, self.drain)

    def tick(self):
        try:
            for name, cam in cams.items():
                frame = overlay(cam)
                if frame is None:
                    continue
                frame = cv2.resize(frame, cam.panel)
                rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
                ph = ImageTk.PhotoImage(Image.fromarray(rgb))
                self.imgs[name] = ph
                self.labels[name].config(image=ph, width=cam.panel[0],
                                         height=cam.panel[1])
                self.stats[name].config(
                    text='%s: %s  FPS %.1f  frames %d  skips %d  gaps %d'
                         % (name, cam.state, cam.fps, cam.frames, cam.skips,
                            cam.gaps))
        except Exception as e:
            log('tick error: %r' % e)
        finally:
            self.root.after(33, self.tick)

    def drain(self):
        try:
            while True:
                line = logq.get_nowait()
                self.logbox.insert('end', line + '\n')
                self.logbox.see('end')
                if int(self.logbox.index('end-1c').split('.')[0]) > 3000:
                    self.logbox.delete('1.0', '1000.0')
        except queue.Empty:
            pass
        self.root.after(200, self.drain)

    def marker(self):
        log('================ USER MARKER (看到卡顿) ================')


cams = {name: Cam(name, cfg) for name, cfg in CAMS.items()}
for cam in cams.values():
    threading.Thread(target=cam_thread, args=(cam,), daemon=True).start()

root = tk.Tk()
App(root)
root.mainloop()
