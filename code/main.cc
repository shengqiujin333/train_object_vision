// Copyright (c) 2023 by Rockchip Electronics Co., Ltd. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

/*-------------------------------------------
        YOLOv8-seg (rail) + YOLOv5 (person) demo
  - Inputs:
      <rail_seg_model> <yolov5_model> <input> [thermal_rtsp_url]
      input can be image file / video file / rtsp:// URL
  - Rail seg: paints rail mask (semi-transparent)
  - YOLOv5: detects person bbox every frame
  - Alarm: person bbox center inside rail mask -> red + WARNING
  - thermal_rtsp_url (optional): display-only second window
  - Image: press any key to exit; Video/RTSP: press 'q' to quit
-------------------------------------------*/

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

#include <opencv2/opencv.hpp>
#include <opencv2/videoio.hpp>

// Native GStreamer + librga for the thermal grabber: OpenCV 4.5.4 pins its own
// BGR-only caps onto a user-supplied appsink, so an NV12 capsfilter upstream
// can never preroll (cap.open hangs forever, verified 2026-09-19). Pulling NV12
// through the C API and converting with librga keeps the whole path on
// hardware (RGA core) and bypasses both the buggy rgaconvert plugin and OpenCV.
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/video/video.h>
#include <im2d.h>
#include <RgaUtils.h>

#include "yolov8_seg.h"
#include "yolov5_person.h"
#include "yolov5_postprocess.h"
#include "image_utils.h"
#include "file_utils.h"
#include "image_drawing.h"
#include "common.h"

static unsigned char class_colors[][3] = {
    {255, 56, 56},   // 'FF3838'
    {255, 157, 151}, // 'FF9D97'
    {255, 112, 31},  // 'FF701F'
    {255, 178, 29},  // 'FFB21D'
    {207, 210, 49},  // 'CFD231'
    {72, 249, 10},   // '48F90A'
    {146, 204, 23},  // '92CC17'
    {61, 219, 134},  // '3DDB86'
    {26, 147, 52},   // '1A9334'
    {0, 212, 187},   // '00D4BB'
    {44, 153, 168},  // '2C99A8'
    {0, 194, 255},   // '00C2FF'
    {52, 69, 147},   // '344593'
    {100, 115, 255}, // '6473FF'
    {0, 24, 236},    // '0018EC'
    {132, 56, 255},  // '8438FF'
    {82, 0, 133},    // '520085'
    {203, 56, 255},  // 'CB38FF'
    {255, 149, 200}, // 'FF95C8'
    {255, 55, 199}   // 'FF37C7'
};

// Build a GStreamer pipeline for RTSP (HW decode via MPP + RGA color convert)
// - latency=500: 500ms jitter buffer; tolerates network jitter without
//   dropping frames. Higher = smoother but more lag.
//   NB: do NOT force protocols=tcp here. Some cameras (e.g. the thermal
//   at 192.168.1.64) reject TCP-interleaved RTSP and stall in SETUP.
//   UDP works for all tested cameras; rely on appsink drop=1 to absorb
//   the occasional lost packet.
static char *build_gstreamer_pipeline(const char *rtsp_url, int width, int height)
{
    static char pipeline[2048];
    snprintf(pipeline, sizeof(pipeline),
             "rtspsrc location=%s latency=500 timeout=5000000 udp-buffer-size=4194304 "
             "! rtph265depay ! h265parse ! mppvideodec "
             "! rgaconvert ! video/x-raw,format=BGR,width=%d,height=%d "
             "! appsink name=main_sink drop=1 max-buffers=1 sync=0",
             rtsp_url, width, height);
    return pipeline;
}

// Thermal camera pipeline. decodebin auto-negotiates H264/H265 (thermal
// main stream = H.265 1280x1024) and mppvideodec emits NV12 directly — no
// CPU conversion in the pipeline. Output BGR is produced by librga (RGA
// hardware) inside the grabber thread. rgaconvert is NOT used: it renders
// this stream green (UV-plane handling bug, verified against 8-bit Main
// profile SDP) and mis-negotiates 1x1 without pinned caps.
// timeout=5s: rtspsrc errors out when the camera stops sending data, so the
// grabber can detect the stall and reconnect instead of blocking forever.
static char *build_thermal_pipeline(const char *rtsp_url)
{
    static char pipeline[2048];
    snprintf(pipeline, sizeof(pipeline),
             "rtspsrc location=%s latency=500 timeout=5000000 udp-buffer-size=4194304 "
             "! decodebin ! video/x-raw,format=NV12 "
             "! appsink name=thermal_sink drop=1 max-buffers=1 sync=0",
             rtsp_url);
    return pipeline;
}

// ---------------------------------------------------------------------------
// Bad-state watchdog: the thermal camera's encoder can wedge into a state
// where it only emits near-zero filler frames (skip storm), or stops sending
// keyframes entirely (preroll never completes). Session rebuilds never recover
// it; a camera power cycle is the only known fix (verified twice). The camera
// supports ONVIF SystemReboot (verified 2026-09-21: endpoint
// http://<cam>/onvif/device_service, WS-UsernameToken digest auth), so the
// grabber can reboot it unattended and self-recover in ~90s.
#define ONVIF_USER "admin"
#define ONVIF_PASS "123456"
// Skip storm: sustained >5 filler frames/s for 30s. Night AGC dark-ramp
// episodes peak at ~0.4/s, the wedged-state storm ran at ~13/s.
#define STORM_WINDOW_MS 30000
#define STORM_SKIP_LIMIT 150
// Preroll wedge: reconnect loop retries every ~12s; ~4 min of futile retries
// means the camera needs a reboot (covers camera boot time after a manual
// power cycle, which takes ~7 retries).
#define PREROLL_FAIL_LIMIT 20
#define WATCHDOG_COOLDOWN_MS (10 * 60 * 1000)

// Minimal SHA-1 (FIPS 180-1) for the ONVIF WS-UsernameToken password digest.
typedef struct
{
    uint32_t h[5];
    uint64_t len;
    unsigned char buf[64];
    size_t buf_len;
} sha1_ctx;

static uint32_t sha1_rol(uint32_t v, int b) { return (v << b) | (v >> (32 - b)); }

static void sha1_block(sha1_ctx *c, const unsigned char *p)
{
    uint32_t w[80];
    for (int i = 0; i < 16; i++)
        w[i] = ((uint32_t)p[4 * i] << 24) | ((uint32_t)p[4 * i + 1] << 16) |
               ((uint32_t)p[4 * i + 2] << 8) | p[4 * i + 3];
    for (int i = 16; i < 80; i++)
        w[i] = sha1_rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = c->h[0], b = c->h[1], cc = c->h[2], d = c->h[3], e = c->h[4];
    for (int i = 0; i < 80; i++)
    {
        uint32_t f, k;
        if (i < 20) { f = (b & cc) | ((~b) & d); k = 0x5A827999; }
        else if (i < 40) { f = b ^ cc ^ d; k = 0x6ED9EBA1; }
        else if (i < 60) { f = (b & cc) | (b & d) | (cc & d); k = 0x8F1BBCDC; }
        else { f = b ^ cc ^ d; k = 0xCA62C1D6; }
        uint32_t t = sha1_rol(a, 5) + f + e + k + w[i];
        e = d; d = cc; cc = sha1_rol(b, 30); b = a; a = t;
    }
    c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d; c->h[4] += e;
}

static void sha1_init(sha1_ctx *c)
{
    c->h[0] = 0x67452301; c->h[1] = 0xEFCDAB89; c->h[2] = 0x98BADCFE;
    c->h[3] = 0x10325476; c->h[4] = 0xC3D2E1F0;
    c->len = 0; c->buf_len = 0;
}

static void sha1_update(sha1_ctx *c, const unsigned char *p, size_t n)
{
    c->len += n;
    while (n)
    {
        size_t take = 64 - c->buf_len;
        if (take > n) take = n;
        memcpy(c->buf + c->buf_len, p, take);
        c->buf_len += take; p += take; n -= take;
        if (c->buf_len == 64) { sha1_block(c, c->buf); c->buf_len = 0; }
    }
}

static void sha1_final(sha1_ctx *c, unsigned char out[20])
{
    uint64_t bits = c->len * 8;
    unsigned char pad = 0x80, z = 0, lenb[8];
    sha1_update(c, &pad, 1);
    while (c->buf_len != 56) sha1_update(c, &z, 1);
    for (int i = 0; i < 8; i++) lenb[i] = (unsigned char)(bits >> (56 - 8 * i));
    sha1_update(c, lenb, 8);
    for (int i = 0; i < 5; i++)
    {
        out[4 * i] = (unsigned char)(c->h[i] >> 24);
        out[4 * i + 1] = (unsigned char)(c->h[i] >> 16);
        out[4 * i + 2] = (unsigned char)(c->h[i] >> 8);
        out[4 * i + 3] = (unsigned char)(c->h[i]);
    }
}

static void b64_encode(const unsigned char *in, size_t n, char *out, size_t outsz)
{
    static const char tbl[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t i = 0, o = 0;
    for (; i + 3 <= n && o + 4 < outsz; i += 3)
    {
        uint32_t v = ((uint32_t)in[i] << 16) | ((uint32_t)in[i + 1] << 8) | in[i + 2];
        out[o++] = tbl[(v >> 18) & 63]; out[o++] = tbl[(v >> 12) & 63];
        out[o++] = tbl[(v >> 6) & 63]; out[o++] = tbl[v & 63];
    }
    if (n - i == 1 && o + 4 < outsz)
    {
        uint32_t v = (uint32_t)in[i] << 16;
        out[o++] = tbl[(v >> 18) & 63]; out[o++] = tbl[(v >> 12) & 63];
        out[o++] = '='; out[o++] = '=';
    }
    else if (n - i == 2 && o + 4 < outsz)
    {
        uint32_t v = ((uint32_t)in[i] << 16) | ((uint32_t)in[i + 1] << 8);
        out[o++] = tbl[(v >> 18) & 63]; out[o++] = tbl[(v >> 12) & 63];
        out[o++] = tbl[(v >> 6) & 63]; out[o++] = '=';
    }
    out[o] = 0;
}

// Extract host from rtsp://user:pass@host:port/...
static void onvif_host_from_rtsp(const char *url, char *host, size_t hostsz)
{
    host[0] = 0;
    const char *at = strchr(url, '@');
    if (!at) return;
    at++;
    const char *slash = strchr(at, '/');
    size_t n = slash ? (size_t)(slash - at) : strlen(at);
    const char *colon = (const char *)memchr(at, ':', n);
    if (colon) n = (size_t)(colon - at);
    if (n >= hostsz) n = hostsz - 1;
    memcpy(host, at, n);
    host[n] = 0;
}

// POST a SOAP body to http://host/onvif/device_service with WS-UsernameToken
// digest auth. Returns HTTP status, 0 on transport failure. Blocking ~10s max.
static int onvif_device_request(const char *host, const char *action, const char *soap_body)
{
    unsigned char nonce[16], mac[20];
    FILE *ur = fopen("/dev/urandom", "rb");
    if (ur) { if (fread(nonce, 1, sizeof(nonce), ur) != sizeof(nonce)) {} fclose(ur); }
    else for (size_t i = 0; i < sizeof(nonce); i++) nonce[i] = (unsigned char)rand();

    char created[40];
    time_t now = time(NULL);
    strftime(created, sizeof(created), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));

    sha1_ctx c;
    sha1_init(&c);
    sha1_update(&c, nonce, sizeof(nonce));
    sha1_update(&c, (const unsigned char *)created, strlen(created));
    sha1_update(&c, (const unsigned char *)ONVIF_PASS, strlen(ONVIF_PASS));
    sha1_final(&c, mac);

    char digest[32], nonce_b64[32];
    b64_encode(mac, 20, digest, sizeof(digest));
    b64_encode(nonce, 16, nonce_b64, sizeof(nonce_b64));

    char xml[2048];
    snprintf(xml, sizeof(xml),
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\">"
        "<s:Header>"
        "<Security s:mustUnderstand=\"1\" xmlns=\"http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-secext-1.0.xsd\">"
        "<UsernameToken><Username>%s</Username>"
        "<Password Type=\"http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-username-token-profile-1.0#PasswordDigest\">%s</Password>"
        "<Nonce>%s</Nonce><Created>%s</Created></UsernameToken>"
        "</Security></s:Header>"
        "<s:Body xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\">"
        "%s</s:Body></s:Envelope>",
        ONVIF_USER, digest, nonce_b64, created, soap_body);

    struct addrinfo hints, *res = nullptr;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, "80", &hints, &res) != 0 || !res)
    {
        printf("onvif: getaddrinfo %s failed\n", host);
        return 0;
    }
    int fd = socket(res->ai_family, res->ai_socktype, 0);
    if (fd < 0) { freeaddrinfo(res); return 0; }
    struct timeval tv = {10, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    int rc = connect(fd, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);
    if (rc != 0)
    {
        printf("onvif: connect %s:80 failed (camera down?)\n", host);
        close(fd);
        return 0;
    }

    char req[4096];
    int m = snprintf(req, sizeof(req),
        "POST /onvif/device_service HTTP/1.1\r\nHost: %s\r\n"
        "Content-Type: application/soap+xml; charset=utf-8; action=\"%s\"\r\n"
        "Content-Length: %zu\r\nConnection: close\r\n\r\n%s",
        host, action, strlen(xml), xml);
    size_t sent = 0;
    while (sent < (size_t)m)
    {
        ssize_t k = send(fd, req + sent, (size_t)m - sent, 0);
        if (k <= 0) { close(fd); return 0; }
        sent += (size_t)k;
    }
    char resp[2048];
    size_t got = 0;
    while (got < sizeof(resp) - 1)
    {
        ssize_t k = recv(fd, resp + got, sizeof(resp) - 1 - got, 0);
        if (k <= 0) break;
        got += (size_t)k;
    }
    close(fd);
    resp[got] = 0;
    int status = 0;
    if (got > 12 && strncmp(resp, "HTTP/1.", 7) == 0)
        status = atoi(resp + 9);
    return status;
}

static int onvif_system_reboot(const char *host)
{
    return onvif_device_request(
        host,
        "http://www.onvif.org/ver10/device/wsdl/SystemReboot",
        "<tds:SystemReboot xmlns:tds=\"http://www.onvif.org/ver10/device/wsdl\"/>");
}

static int is_rtsp_url(const char *s)
{
    return strncmp(s, "rtsp://", 7) == 0;
}

static int is_image_file(const char *s)
{
    const char *dot = strrchr(s, '.');
    if (!dot) return 0;
    dot++;
    if (!strcasecmp(dot, "jpg") || !strcasecmp(dot, "jpeg") ||
        !strcasecmp(dot, "png") || !strcasecmp(dot, "bmp"))
        return 1;
    return 0;
}

// Cached rail seg results. Masks are cloned (own their data) so they survive across frames.
struct CachedResults
{
    object_detect_result_list od_results;
    std::vector<cv::Mat> masks; // CV_8UC1, one per detection (may be empty if seg_mask was null)
    bool valid = false;
};

// Person detection results (yolov5 COCO, filtered to cls_id==0 person when drawing).
struct PersonResults
{
    yolov5_result_list od_results;
    bool valid = false;
};

// Check if point (cx, cy) is inside any rail mask.
static bool point_in_any_rail_mask(const std::vector<cv::Mat> &masks, int cx, int cy)
{
    if (cx < 0 || cy < 0) return false;
    for (const auto &m : masks)
    {
        if (m.empty()) continue;
        if (cy >= m.rows || cx >= m.cols) continue;
        if (m.at<uint8_t>(cy, cx) > 0) return true;
    }
    return false;
}

// Run rail seg inference. Clones seg_masks into cached->masks (deep copy, owns data).
// Frees the original seg_mask arrays returned by the inference call.
static int run_rail_inference(rknn_app_context_t *rknn_app_ctx, const cv::Mat &frame_bgr, CachedResults *cached)
{
    cv::Mat frame_rgb;
    cv::cvtColor(frame_bgr, frame_rgb, cv::COLOR_BGR2RGB);

    image_buffer_t src_image;
    memset(&src_image, 0, sizeof(image_buffer_t));
    src_image.width = frame_rgb.cols;
    src_image.height = frame_rgb.rows;
    src_image.width_stride = frame_rgb.cols;
    src_image.height_stride = frame_rgb.rows;
    src_image.format = IMAGE_FORMAT_RGB888;
    src_image.virt_addr = frame_rgb.data;
    src_image.size = frame_rgb.total() * frame_rgb.elemSize();
    src_image.fd = -1;

    int ret = inference_yolov8_seg_model(rknn_app_ctx, &src_image, &cached->od_results);
    if (ret != 0)
    {
        printf("inference_yolov8_seg_model fail! ret=%d\n", ret);
        return ret;
    }

    cached->masks.clear();
    cached->masks.reserve(cached->od_results.count);
    int width = frame_rgb.cols;
    int height = frame_rgb.rows;
    for (int b = 0; b < cached->od_results.count; b++)
    {
        uint8_t *seg_mask = cached->od_results.results_seg[b].seg_mask;
        if (seg_mask == nullptr)
        {
            cached->masks.push_back(cv::Mat());
            continue;
        }
        cv::Mat m(height, width, CV_8UC1, seg_mask);
        cached->masks.push_back(m.clone());
        free(seg_mask);
        cached->od_results.results_seg[b].seg_mask = nullptr;
    }
    cached->valid = true;
    return 0;
}

// Run yolov5 person detection. Returns yolov5_result_list with all 80 classes;
// caller filters cls_id == 0 (person) when drawing.
static int run_person_inference(yolov5_app_context_t *person_ctx, const cv::Mat &frame_bgr, PersonResults *p)
{
    cv::Mat frame_rgb;
    cv::cvtColor(frame_bgr, frame_rgb, cv::COLOR_BGR2RGB);

    image_buffer_t src_image;
    memset(&src_image, 0, sizeof(image_buffer_t));
    src_image.width = frame_rgb.cols;
    src_image.height = frame_rgb.rows;
    src_image.width_stride = frame_rgb.cols;
    src_image.height_stride = frame_rgb.rows;
    src_image.format = IMAGE_FORMAT_RGB888;
    src_image.virt_addr = frame_rgb.data;
    src_image.size = frame_rgb.total() * frame_rgb.elemSize();
    src_image.fd = -1;

    int ret = inference_yolov5_person_model(person_ctx, &src_image, &p->od_results);
    if (ret != 0)
    {
        printf("inference_yolov5_person_model fail! ret=%d\n", ret);
        return ret;
    }
    p->valid = true;
    return 0;
}

// Draw cached rail masks + rail boxes + labels onto frame_bgr in-place.
static void draw_rail_results(cv::Mat &frame_bgr, const CachedResults &cached)
{
    if (!cached.valid || cached.od_results.count == 0) return;

    int width = frame_bgr.cols;
    int height = frame_bgr.rows;
    float alpha = 0.5f;
    cv::Mat colored(frame_bgr.size(), frame_bgr.type());
    cv::Mat blended(frame_bgr.size(), frame_bgr.type());

    for (int b = 0; b < cached.od_results.count; b++)
    {
        if (cached.masks[b].empty()) continue;
        int cls_id = cached.od_results.results[b].cls_id;
        unsigned char *color = class_colors[cls_id % N_CLASS_COLORS];
        const cv::Mat &mask_mat = cached.masks[b];
        colored.setTo(cv::Scalar(color[0], color[1], color[2]), mask_mat);
        cv::addWeighted(frame_bgr, alpha, colored, 1.0f - alpha, 0.0, blended);
        blended.copyTo(frame_bgr, mask_mat);
    }

    char text[256];
    for (int i = 0; i < cached.od_results.count; i++)
    {
        const object_detect_result *det = &(cached.od_results.results[i]);
        // printf("%s @ (%d %d %d %d) %.3f\n", coco_cls_to_name(det->cls_id),
        //        det->box.left, det->box.top, det->box.right, det->box.bottom,
        //        det->prop);
        int x1 = det->box.left, y1 = det->box.top;
        int x2 = det->box.right, y2 = det->box.bottom;
        cv::rectangle(frame_bgr, cv::Point(x1, y1), cv::Point(x2, y2),
                      cv::Scalar(0, 255, 0), 3);
        snprintf(text, sizeof(text), "%s %.1f%%", coco_cls_to_name(det->cls_id),
                 det->prop * 100);
        int ytext = (y1 - 10 > 0) ? (y1 - 10) : (y1 + 20);
        cv::putText(frame_bgr, text, cv::Point(x1, ytext),
                    cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 0, 255), 2);
    }
}

// Draw person bbox. Red + WARNING if center in rail mask; green otherwise.
static void draw_person_results(cv::Mat &frame_bgr, const PersonResults &p,
                                const std::vector<cv::Mat> &rail_masks)
{
    if (!p.valid || p.od_results.count == 0) return;

    char text[256];
    for (int i = 0; i < p.od_results.count; i++)
    {
        const object_detect_result *det = &(p.od_results.results[i]);
        if (det->cls_id != 0) continue; // only person (COCO cls 0)

        int x1 = det->box.left, y1 = det->box.top;
        int x2 = det->box.right, y2 = det->box.bottom;
        int cx = (x1 + x2) / 2;
        int cy = (y1 + y2) / 2;
        bool on_rail = point_in_any_rail_mask(rail_masks, cx, cy);

        cv::Scalar color = on_rail ? cv::Scalar(0, 0, 255) : cv::Scalar(0, 255, 0);
        cv::rectangle(frame_bgr, cv::Point(x1, y1), cv::Point(x2, y2), color, 3);

        if (on_rail)
            snprintf(text, sizeof(text), "WARNING: person on rail %.1f%%", det->prop * 100);
        else
            snprintf(text, sizeof(text), "person %.1f%%", det->prop * 100);
        int ytext = (y1 - 10 > 0) ? (y1 - 10) : (y1 + 20);
        cv::putText(frame_bgr, text, cv::Point(x1, ytext),
                    cv::FONT_HERSHEY_SIMPLEX, 0.7, color, 2);

        // printf("person @ (%d %d %d %d) %.3f ON_RAIL=%d\n",
        //        x1, y1, x2, y2, det->prop, on_rail);
    }
}

// Convenience for image path: run both models + draw on the same frame.
static int process_frame(rknn_app_context_t *rail_ctx, yolov5_app_context_t *person_ctx, cv::Mat &frame_bgr)
{
    CachedResults rail_cached;
    PersonResults person;
    int ret = run_rail_inference(rail_ctx, frame_bgr, &rail_cached);
    if (ret != 0) return ret;
    ret = run_person_inference(person_ctx, frame_bgr, &person);
    if (ret != 0) return ret;
    draw_rail_results(frame_bgr, rail_cached);
    draw_person_results(frame_bgr, person, rail_cached.masks);
    return 0;
}

// ---- Main camera grabber (native GStreamer pull of RGA-converted BGR) ----
// Same raw C-API architecture as ThermalGrabber. The previous OpenCV
// VideoCapture wrapper delivered this identical pipeline with recurring
// 400-700 ms delivery stalls (fps_probe with the same chain ran 25 fps
// concurrently, 2026-09-21); pulling the sample directly removes OpenCV's
// backend from the frame path.
struct MainGrabber
{
    GstElement *pipeline = nullptr;
    GstElement *sink = nullptr;
    std::thread th;
    std::mutex mtx;
    cv::Mat frame; // latest BGR frame (empty until first grab)
    std::atomic<bool> running{false};
    std::atomic<bool> opened{false};
    std::atomic<uint64_t> seq{0}; // bumped on every published frame
};

static void main_teardown(MainGrabber *g)
{
    if (g->pipeline)
    {
        gst_element_set_state(g->pipeline, GST_STATE_NULL);
        gst_object_unref(g->pipeline);
        g->pipeline = nullptr;
        g->sink = nullptr;
    }
    g->opened = false;
}

static void main_thread_func(MainGrabber *g, const char *pipe_desc)
{
    gst_init(NULL, NULL); // idempotent; thermal grabber may have init'ed already
    int fail_streak = 0;  // consecutive empty pulls (100 ms each)
    while (g->running)
    {
        if (!g->pipeline)
        {
            printf("camera thread: opening %s\n", pipe_desc);
            GError *err = nullptr;
            g->pipeline = gst_parse_launch(pipe_desc, &err);
            if (err)
            {
                printf("camera thread: pipeline parse error: %s\n", err->message);
                g_error_free(err);
            }
            if (!g->pipeline)
            {
                std::this_thread::sleep_for(std::chrono::seconds(2));
                continue;
            }
            g->sink = gst_bin_get_by_name(GST_BIN(g->pipeline), "main_sink");
            gst_element_set_state(g->pipeline, GST_STATE_PLAYING);
            fail_streak = 0;
        }
        GstSample *sample =
            gst_app_sink_try_pull_sample(GST_APP_SINK(g->sink), 100 * GST_MSECOND);
        if (!sample)
        {
            // 20 consecutive empty pulls = 2 s without a frame (normal AGC
            // pauses are < 1 s; rtspsrc timeout=5s turns camera death into
            // permanent empty pulls) -> rebuild the session.
            if (++fail_streak >= 20)
            {
                printf("camera thread: no frames for %d polls, reconnecting\n",
                       fail_streak);
                main_teardown(g);
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            continue;
        }
        fail_streak = 0;
        GstVideoInfo info;
        gst_video_info_from_caps(&info, gst_sample_get_caps(sample));
        GstVideoFrame vframe;
        if (gst_video_frame_map(&vframe, &info, gst_sample_get_buffer(sample),
                                GST_MAP_READ))
        {
            cv::Mat f(cv::Size(info.width, info.height), CV_8UC3,
                      vframe.data[0], vframe.info.stride[0]);
            {
                std::lock_guard<std::mutex> lk(g->mtx);
                f.copyTo(g->frame);
                g->seq++;
            }
            gst_video_frame_unmap(&vframe);
            if (!g->opened)
            {
                printf("camera thread: opened, first frame %dx%d\n",
                       info.width, info.height);
                g->opened = true;
            }
        }
        gst_sample_unref(sample);
    }
    printf("camera thread: exit\n");
}

// ---- Thermal grabber (native GStreamer + RGA conversion) -----------------
// Pulls NV12 straight from the decoder via the GStreamer C API (OpenCV 4.5.4
// cannot do this: it pins BGR-only caps on a user appsink, so NV12 never
// prerolls and cap.open hangs) and converts NV12->BGR with librga on an RGA
// hardware core. Reconnects when the camera stops sending frames.
struct ThermalGrabber
{
    GstElement *pipeline = nullptr;
    GstElement *sink = nullptr;
    std::thread th;
    std::mutex mtx;
    cv::Mat frame; // latest BGR frame (empty until first grab)
    std::atomic<bool> running{false};
    std::atomic<bool> opened{false};
};

// uv_offset is the byte offset of the UV plane; decoder padding between Y and
// UV planes is absorbed by expressing it as hstride for RGA.
static bool nv12_to_bgr_rga(const uint8_t *nv12, int w, int h, int ystride,
                            size_t uv_offset, cv::Mat &out, bool log_err)
{
    int hstride = (int)(uv_offset / (size_t)ystride);
    rga_buffer_t src = wrapbuffer_virtualaddr((void *)nv12, w, h,
                                              RK_FORMAT_YCbCr_420_SP, ystride, hstride);
    rga_buffer_t dst = wrapbuffer_virtualaddr(out.data, w, h, RK_FORMAT_BGR_888, w, h);
    IM_STATUS st = imcvtcolor(src, dst, RK_FORMAT_YCbCr_420_SP, RK_FORMAT_BGR_888,
                              IM_YUV_TO_RGB_BT601_FULL);
    // librga has TWO success codes: NOERROR(2) and SUCCESS(1). imcvtcolor
    // returns SUCCESS; treating it as failure silently discards good frames.
    bool ok = (st == IM_STATUS_NOERROR || st == IM_STATUS_SUCCESS);
    if (!ok && log_err)
    {
        printf("thermal RGA convert failed: %s (ystride=%d hstride=%d uv_off=%zu)\n",
               imStrError(st), ystride, hstride, uv_offset);
    }
    return ok;
}

static void thermal_teardown(ThermalGrabber *g)
{
    if (g->pipeline)
    {
        gst_element_set_state(g->pipeline, GST_STATE_NULL);
        gst_object_unref(g->pipeline);
        g->pipeline = nullptr;
        g->sink = nullptr;
    }
    g->opened = false;
}

static void thermal_thread_func(ThermalGrabber *g, const char *url)
{
    gst_init(NULL, NULL); // idempotent; OpenCV may have init'ed already
    int logged_frames = 0;
    int skip_count = 0;
    unsigned long published = 0;
    int preroll_fails = 0;
    // Watchdog cooldown persists across reconnects; armed to fire immediately.
    auto last_reboot = std::chrono::steady_clock::now() -
                       std::chrono::milliseconds(WATCHDOG_COOLDOWN_MS);
    while (g->running)
    {
        printf("thermal thread: opening %s\n", url);
        GError *err = nullptr;
        g->pipeline = gst_parse_launch(build_thermal_pipeline(url), &err);
        if (err)
        {
            printf("thermal thread: pipeline error: %s\n", err->message);
            g_error_free(err);
        }
        if (!g->pipeline)
        {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            continue;
        }
        g->sink = gst_bin_get_by_name(GST_BIN(g->pipeline), "thermal_sink");
        gst_element_set_state(g->pipeline, GST_STATE_PLAYING);
        GstState st;
        if (gst_element_get_state(g->pipeline, &st, nullptr, 10 * GST_SECOND) !=
            GST_STATE_CHANGE_SUCCESS)
        {
            preroll_fails++;
            printf("thermal thread: no preroll in 10s (camera not sending keyframes?), retrying\n");
            if (preroll_fails >= PREROLL_FAIL_LIMIT &&
                std::chrono::steady_clock::now() - last_reboot >=
                    std::chrono::milliseconds(WATCHDOG_COOLDOWN_MS))
            {
                char cam_host[64];
                onvif_host_from_rtsp(url, cam_host, sizeof(cam_host));
                printf("thermal: %d preroll failures -> ONVIF SystemReboot %s\n",
                       preroll_fails, cam_host);
                int rc = onvif_system_reboot(cam_host);
                printf("thermal: ONVIF SystemReboot -> HTTP %d\n", rc);
                last_reboot = std::chrono::steady_clock::now();
                preroll_fails = 0;
            }
            thermal_teardown(g);
            std::this_thread::sleep_for(std::chrono::seconds(2));
            continue;
        }
        preroll_fails = 0;
        printf("thermal thread: opened\n");

        int idle_polls = 0;
        int storm_win_skips = 0;
        auto storm_win_start = std::chrono::steady_clock::now();
        auto last_pull = std::chrono::steady_clock::now();
        while (g->running)
        {
            GstSample *sample =
                gst_app_sink_try_pull_sample(GST_APP_SINK(g->sink), 200 * GST_MSECOND);
            if (!sample)
            {
                if (++idle_polls > 40) // ~8s without a frame -> rebuild session
                {
                    printf("thermal thread: no frames for 8s, reconnecting\n");
                    break;
                }
                continue;
            }
            idle_polls = 0;
            auto gap_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - last_pull)
                              .count();
            if (gap_ms > 500)
                printf("thermal: stream gap %lld ms (decoder stall / packet loss)\n",
                       (long long)gap_ms);
            last_pull = std::chrono::steady_clock::now();

            // Skip-storm watchdog: a sustained flood of near-zero filler
            // frames means the camera encoder is wedged; reboot it via ONVIF.
            auto now_ts = std::chrono::steady_clock::now();
            if (now_ts - storm_win_start >= std::chrono::milliseconds(STORM_WINDOW_MS))
            {
                if (storm_win_skips > STORM_SKIP_LIMIT &&
                    now_ts - last_reboot >= std::chrono::milliseconds(WATCHDOG_COOLDOWN_MS))
                {
                    char cam_host[64];
                    onvif_host_from_rtsp(url, cam_host, sizeof(cam_host));
                    printf("thermal: skip storm detected (%d skips in %ds) -> ONVIF SystemReboot %s\n",
                           storm_win_skips, (int)(STORM_WINDOW_MS / 1000), cam_host);
                    int rc = onvif_system_reboot(cam_host);
                    printf("thermal: ONVIF SystemReboot -> HTTP %d (camera back in ~90s)\n", rc);
                    last_reboot = now_ts;
                }
                storm_win_skips = 0;
                storm_win_start = now_ts;
            }

            GstCaps *caps = gst_sample_get_caps(sample);
            GstVideoInfo info;
            GstVideoFrame vf;
            if (!caps || !gst_video_info_from_caps(&info, caps) ||
                !gst_video_frame_map(&vf, &info, gst_sample_get_buffer(sample),
                                     GST_MAP_READ))
            {
                gst_sample_unref(sample);
                continue;
            }
            int w = GST_VIDEO_INFO_WIDTH(&info);
            int h = GST_VIDEO_INFO_HEIGHT(&info);
            int ystride = GST_VIDEO_INFO_PLANE_STRIDE(&info, 0);
            size_t uv_offset = (size_t)((const uint8_t *)vf.data[1] - (const uint8_t *)vf.data[0]);

            // Skip empty buffers: the decoder emits near-zero Y-plane fillers
            // at preroll and after stream resets/loss. Converted they render
            // as the uniform-green frame (Y~0,UV~0 -> BGR ~0/135/0).
            cv::Mat yCheck(h, w, CV_8UC1, (void *)vf.data[0], ystride);
            double ymean = cv::mean(yCheck)[0];
            // 10.0, not 2.0: filler frames around stream resets carry Y means
            // up to ~10 with noise and used to slip through as green flashes.
            // Real content is far above this (AGC ramp tops out at 1.9, normal
            // night scene ~83).
            if (ymean < 10.0)
            {
                skip_count++;
                storm_win_skips++;
                printf("thermal: skipped empty frame #%d (Y mean %.2f)\n", skip_count, ymean);
                gst_video_frame_unmap(&vf);
                gst_sample_unref(sample);
                continue;
            }

            cv::Mat bgr(h, w, CV_8UC3);
            bool rga_ok =
                nv12_to_bgr_rga((const uint8_t *)vf.data[0], w, h, ystride, uv_offset, bgr, true);
            if (!rga_ok)
            {
                // CPU fallback so the window stays alive if RGA rejects the layout
                cv::Mat yPlane(h, w, CV_8UC1, (void *)vf.data[0], ystride);
                cv::Mat uvPlane(h / 2, w / 2, CV_8UC2, (void *)vf.data[1], ystride);
                cv::cvtColorTwoPlane(yPlane, uvPlane, bgr, cv::COLOR_YUV2BGR_NV12);
            }
            gst_video_frame_unmap(&vf);
            gst_sample_unref(sample);

            // Decoder resync garbage: Y passes the emptiness filter but the UV
            // plane is stale, so the converted frame renders saturated green.
            // A white-hot gray scene always has B~G~R; B/R collapsed against G
            // is an artifact, never content.
            cv::Scalar bm = cv::mean(bgr);
            if (bm[1] > 50 && bm[0] < 0.2 * bm[1] && bm[2] < 0.2 * bm[1])
            {
                skip_count++;
                storm_win_skips++;
                printf("thermal: skipped green artifact frame #%d (BGR means %.1f/%.1f/%.1f)\n",
                       skip_count, bm[0], bm[1], bm[2]);
                continue;
            }

            {
                std::lock_guard<std::mutex> lk(g->mtx);
                g->frame = bgr;
            }
            g->opened = true;
            published++;
            if (logged_frames < 5)
            {
                logged_frames++;
                cv::Scalar m = cv::mean(bgr);
                printf("thermal frame %d (%s): BGR means %.1f/%.1f/%.1f\n",
                       logged_frames, rga_ok ? "RGA" : "CPU", m[0], m[1], m[2]);
            }
            else if (published % 300 == 0)
            {
                cv::Scalar m = cv::mean(bgr);
                printf("thermal heartbeat: BGR means %.1f/%.1f/%.1f (%lu skipped)\n",
                       m[0], m[1], m[2], (unsigned long)skip_count);
            }
        }
        thermal_teardown(g);
        if (g->running)
            std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    printf("thermal thread: exit\n");
}

static int run_loop(rknn_app_context_t *rail_ctx, yolov5_app_context_t *person_ctx,
                    const char *input, const char *thermal_url, int frame_w, int frame_h)
{
    cv::namedWindow("yolov8_seg", cv::WINDOW_NORMAL);

    if (is_image_file(input))
    {
        printf("Opening image: %s\n", input);
        cv::Mat frame_bgr = cv::imread(input, cv::IMREAD_COLOR);
        if (frame_bgr.empty())
        {
            printf("Failed to read image: %s\n", input);
            return -1;
        }
        frame_w = frame_bgr.cols;
        frame_h = frame_bgr.rows;
        cv::resizeWindow("yolov8_seg", frame_w, frame_h);
        printf("Image size: %dx%d. Press any key in window to exit.\n", frame_w, frame_h);

        int ret = process_frame(rail_ctx, person_ctx, frame_bgr);
        if (ret != 0) return ret;

        cv::imshow("yolov8_seg", frame_bgr);
        cv::waitKey(0);
        cv::destroyAllWindows();
        return 0;
    }

    // Main camera: dedicated grabber thread for RTSP (a stalled stream must
    // never block the detection loop); direct capture for video files (need
    // the "ended" signal to exit cleanly).
    MainGrabber cam;
    cv::VideoCapture cap;
    const bool rtsp_mode = is_rtsp_url(input);
    if (rtsp_mode)
    {
        const char *pipeline = build_gstreamer_pipeline(input, frame_w, frame_h);
        printf("Opening RTSP via grabber thread:\n  %s\n", pipeline);
        cam.running = true;
        cam.th = std::thread(main_thread_func, &cam, pipeline);
        for (int i = 0; i < 150 && !cam.opened; i++) // wait up to 15s for first frame
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (!cam.opened)
        {
            printf("Failed to open RTSP capture. Check pipeline / network.\n");
            cam.running = false;
            cam.th.detach();
            return -1;
        }
    }
    else
    {
        printf("Opening video file: %s\n", input);
        cap.open(input, cv::CAP_FFMPEG);
        if (cap.isOpened())
        {
            frame_w = (int)cap.get(cv::CAP_PROP_FRAME_WIDTH);
            frame_h = (int)cap.get(cv::CAP_PROP_FRAME_HEIGHT);
        }
        if (!cap.isOpened())
        {
            printf("Failed to open VideoCapture. Check input path / pipeline / network.\n");
            return -1;
        }
    }
    printf("VideoCapture opened. Frame size: %dx%d. Press 'q' in window to quit.\n",
           frame_w, frame_h);
    cv::resizeWindow("yolov8_seg", frame_w, frame_h);

    // Thermal camera: dedicated grabber thread + display-only second window.
    // Failure to open is non-fatal (main visible stream keeps running).
    ThermalGrabber thermal;
    bool thermal_window_created = false;
    if (thermal_url)
    {
        thermal.running = true;
        thermal.th = std::thread(thermal_thread_func, &thermal, thermal_url);
    }

    int frame_count = 0;
    struct timespec t_start, t_now;
    clock_gettime(CLOCK_MONOTONIC, &t_start);

    // Rail seg runs every Nth frame; person runs every frame.
    const int inference_interval = 5;
    int frame_idx = 0;
    CachedResults rail_cached;
    PersonResults person;

    uint64_t last_seq = 0;
    auto vis_stall_begin = std::chrono::steady_clock::time_point::max();
    while (true)
    {
        cv::Mat frame_bgr;
        bool vis_new = true;
        if (rtsp_mode)
        {
            uint64_t seq = 0;
            {
                std::lock_guard<std::mutex> lk(cam.mtx);
                if (!cam.frame.empty())
                    frame_bgr = cam.frame;
                seq = cam.seq.load();
            }
            if (frame_bgr.empty())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            vis_new = (seq != last_seq);
            if (vis_new)
            {
                last_seq = seq;
                if (vis_stall_begin != std::chrono::steady_clock::time_point::max())
                {
                    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::steady_clock::now() - vis_stall_begin)
                                  .count();
                    if (ms > 200)
                        printf("main loop: visible stream stalled %lld ms\n", (long long)ms);
                    vis_stall_begin = std::chrono::steady_clock::time_point::max();
                }
            }
            else if (vis_stall_begin == std::chrono::steady_clock::time_point::max())
            {
                vis_stall_begin = std::chrono::steady_clock::now();
            }
        }
        else
        {
            if (!cap.read(frame_bgr) || frame_bgr.empty())
            {
                printf("Video ended.\n");
                break;
            }
        }

        // Inference only on new frames; drawing/display runs EVERY iteration so
        // waitKey keeps pumping X11 events and the thermal window keeps
        // refreshing while one camera briefly stalls (a sub-second UDP stall
        // used to freeze BOTH windows because the old code skipped past
        // imshow/waitKey when the visible seq was unchanged).
        if (vis_new)
        {
            if (frame_idx % inference_interval == 0 || !rail_cached.valid)
            {
                run_rail_inference(rail_ctx, frame_bgr, &rail_cached);
            }
            // Person detection: every frame
            run_person_inference(person_ctx, frame_bgr, &person);

            frame_count++;
            frame_idx++;
            clock_gettime(CLOCK_MONOTONIC, &t_now);
            float elapsed = (t_now.tv_sec - t_start.tv_sec) + (t_now.tv_nsec - t_start.tv_nsec) / 1e9f;
            if (elapsed >= 5.0f)
            {
                printf("FPS: %.2f (%d frames / %.2fs)\n", frame_count / elapsed, frame_count, elapsed);
                frame_count = 0;
                t_start = t_now;
            }
        }

        draw_rail_results(frame_bgr, rail_cached);
        draw_person_results(frame_bgr, person, rail_cached.masks);

        cv::imshow("yolov8_seg", frame_bgr);
        if (thermal_url)
        {
            if (!thermal_window_created && thermal.opened)
            {
                cv::namedWindow("thermal", cv::WINDOW_NORMAL);
                cv::resizeWindow("thermal", 640, 512); // ch101 is 1280x1024; shrink to fit
                thermal_window_created = true;
            }
            cv::Mat thermal_frame;
            bool have = false;
            {
                std::lock_guard<std::mutex> lk(thermal.mtx);
                have = !thermal.frame.empty();
                if (have) thermal_frame = thermal.frame;
            }
            if (have) cv::imshow("thermal", thermal_frame);
        }
        if ((cv::waitKey(1) & 0xFF) == 'q')
            break;

        if (!vis_new)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    if (rtsp_mode)
    {
        cam.running = false;
        cam.th.detach(); // process exit tears down; capture owned solely by thread
    }
    else
    {
        cap.release();
    }
    if (thermal_url)
    {
        thermal.running = false;
        thermal.th.detach(); // process exit tears down; capture owned solely by thread
    }
    cv::destroyAllWindows();
    return 0;
}

int main(int argc, char **argv)
{
    // Line-buffered stdout: when redirected to a file, full buffering (4KB)
    // hides log lines for ~10 minutes and they are lost entirely on abort().
    setvbuf(stdout, NULL, _IOLBF, 4096);
    int frame_w = 1280, frame_h = 720;
    if (argc != 4 && argc != 5 && argc != 6 && argc != 7)
    {
        printf("Usage: %s <rail_model_path> <yolov5_model_path> <input> [thermal_rtsp_url] [width height]\n", argv[0]);
        printf("  input: image file / video file / rtsp:// URL\n");
        printf("  [thermal_rtsp_url]: optional, display-only second window (e.g. Hikvision thermal)\n");
        printf("  [width height]: optional, default 1280x720. Set to camera native resolution\n");
        printf("    for best quality (e.g. 1280 1024 for thermal, 1920 1080 for 1080p).\n");
        printf("  e.g.: %s model/yolov8s-seg.rknn model/yolov5s.rknn test_rail.bmp\n", argv[0]);
        printf("  e.g.: %s model/yolov8s-seg.rknn model/yolov5s.rknn /path/to/video.mp4\n", argv[0]);
        printf("  e.g.: %s model/yolov8s-seg.rknn model/yolov5s.rknn \"rtsp://admin:123456@192.168.0.162:554/h264/ch1/main/av_stream\"\n",
               argv[0]);
        printf("  e.g.: %s model/yolov8s-seg.rknn model/yolov5s.rknn \"rtsp://...\" 1280 1024\n",
               argv[0]);
        printf("  e.g.: %s model/yolov8s-seg.rknn model/yolov5s.rknn \"rtsp://...\" \"rtsp://thermal...\"\n",
               argv[0]);
        return -1;
    }

    const char *rail_model_path = argv[1];
    const char *person_model_path = argv[2];
    const char *input = argv[3];
    const char *thermal_url = NULL;
    if (argc == 5 || argc == 7)
        thermal_url = argv[4];
    if (argc == 6)
    {
        frame_w = atoi(argv[4]);
        frame_h = atoi(argv[5]);
        if (frame_w <= 0 || frame_h <= 0)
        {
            printf("Invalid width/height: %s %s\n", argv[4], argv[5]);
            return -1;
        }
    }
    else if (argc == 7)
    {
        frame_w = atoi(argv[5]);
        frame_h = atoi(argv[6]);
        if (frame_w <= 0 || frame_h <= 0)
        {
            printf("Invalid width/height: %s %s\n", argv[5], argv[6]);
            return -1;
        }
    }

    int ret;
    rknn_app_context_t rail_ctx;
    yolov5_app_context_t person_ctx;
    memset(&rail_ctx, 0, sizeof(rknn_app_context_t));
    memset(&person_ctx, 0, sizeof(yolov5_app_context_t));

    // Load label files for both models (rail: 1 class, yolov5: 80 classes)
    init_post_process();
    yolov5_init_post_process();

    ret = init_yolov8_seg_model(rail_model_path, &rail_ctx);
    if (ret != 0)
    {
        printf("init_yolov8_seg_model fail! ret=%d model_path=%s\n", ret, rail_model_path);
        goto out;
    }

    ret = init_yolov5_person_model(person_model_path, &person_ctx);
    if (ret != 0)
    {
        printf("init_yolov5_person_model fail! ret=%d model_path=%s\n", ret, person_model_path);
        goto out;
    }

    run_loop(&rail_ctx, &person_ctx, input, thermal_url, frame_w, frame_h);

out:
    deinit_post_process();
    yolov5_deinit_post_process();

    ret = release_yolov8_seg_model(&rail_ctx);
    if (ret != 0)
    {
        printf("release_yolov8_seg_model fail! ret=%d\n", ret);
    }

    ret = release_yolov5_person_model(&person_ctx);
    if (ret != 0)
    {
        printf("release_yolov5_person_model fail! ret=%d\n", ret);
    }

    return 0;
}
