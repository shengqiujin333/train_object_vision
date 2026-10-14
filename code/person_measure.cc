// Capture a frame (native resolution) from RTSP or image file, save BMP, run
// yolov5 person detection, and report how many pixels the person occupies.
// - RTSP mode: grabs for up to ~20s until a person is detected (auto-exposure
//   needs a few frames first).
// - If full-frame detection finds no person (small/distant target), retries
//   with 2x2 overlapping tiles upscaled 2x, and maps boxes back to full frame.
// Usage: person_measure [rtsp_url | image_file] [out_bmp]
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <opencv2/opencv.hpp>
#include <opencv2/videoio.hpp>

#include "yolov5_person.h"
#include "yolov5_postprocess.h"
#include "image_utils.h"
#include "file_utils.h"

static int is_rtsp_url(const char *s)
{
    return strncmp(s, "rtsp://", 7) == 0;
}

static int detect_frame(const yolov5_app_context_t *ctx, const cv::Mat &frame_bgr,
                        yolov5_result_list *results)
{
    cv::Mat frame_rgb;
    cv::cvtColor(frame_bgr, frame_rgb, cv::COLOR_BGR2RGB);
    image_buffer_t src_image;
    memset(&src_image, 0, sizeof(src_image));
    src_image.width = frame_rgb.cols;
    src_image.height = frame_rgb.rows;
    src_image.width_stride = frame_rgb.cols;
    src_image.height_stride = frame_rgb.rows;
    src_image.format = IMAGE_FORMAT_RGB888;
    src_image.virt_addr = frame_rgb.data;
    src_image.size = frame_rgb.total() * frame_rgb.elemSize();
    src_image.fd = -1;
    memset(results, 0, sizeof(*results));
    return inference_yolov5_person_model((yolov5_app_context_t *)ctx, &src_image, results);
}

static int count_persons(const yolov5_result_list *r)
{
    int n = 0;
    for (int i = 0; i < r->count; i++)
        if (r->results[i].cls_id == 0) n++;
    return n;
}

// Print person boxes. Crop offset (ox,oy) + inverse of tile zoom maps coords
// back to full-frame pixels. scale640 = letterbox scale of the FULL frame.
static void print_persons(const yolov5_result_list *r, const char *tag,
                          int ox, int oy, float zoom, float scale640, int fw, int fh)
{
    for (int i = 0; i < r->count; i++)
    {
        const object_detect_result *d = &r->results[i];
        if (d->cls_id != 0) continue;
        int fx = ox + (int)(d->box.left / zoom);
        int fy = oy + (int)(d->box.top / zoom);
        int fx2 = ox + (int)(d->box.right / zoom);
        int fy2 = oy + (int)(d->box.bottom / zoom);
        if (fx < 0) fx = 0; if (fy < 0) fy = 0;
        if (fx2 >= fw) fx2 = fw - 1; if (fy2 >= fh) fy2 = fh - 1;
        int bw = fx2 - fx + 1, bh = fy2 - fy + 1;
        printf("%s person[%d]: box=(%d,%d)-(%d,%d)  frame %dx%d px  model640 ~%.0fx%.0f px  conf %.2f\n",
               tag, i, fx, fy, fx2, fy2, bw, bh, bw * scale640, bh * scale640, d->prop);
    }
}

int main(int argc, char **argv)
{
    const char *input = (argc > 1) ? argv[1]
        : "rtsp://admin:ipc12345@192.168.1.65:554/cam/realmonitor?channel=1&subtype=0";
    // Timestamped filename keeps every shot (no overwriting when comparing zooms)
    char bmp_default[128];
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    strftime(bmp_default, sizeof(bmp_default), "/tmp/capture_%Y%m%d_%H%M%S.bmp", &tm_now);
    const char *out_bmp = (argc > 2) ? argv[2] : bmp_default;

    yolov5_init_post_process();
    yolov5_app_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    if (init_yolov5_person_model("/home/admin/rknn_model_zoo/install/rk3588_linux_aarch64/rknn_yolov8_seg_demo/model/yolov5s.rknn", &ctx) != 0)
    {
        printf("init_yolov5_person_model fail\n");
        return -1;
    }

    cv::Mat frame_bgr;
    yolov5_result_list results;
    int found = 0;

    if (is_rtsp_url(input))
    {
        // Native resolution: no width/height caps; CPU convert (measurement is one-shot)
        char pipeline[2048];
        snprintf(pipeline, sizeof(pipeline),
                 "rtspsrc location=%s latency=500 "
                 "! rtph265depay ! h265parse ! mppvideodec "
                 "! videoconvert ! video/x-raw,format=BGR "
                 "! appsink drop=1 max-buffers=1 sync=0",
                 input);
        printf("Opening RTSP (native res)...\n");
        cv::VideoCapture cap(pipeline, cv::CAP_GSTREAMER);
        if (!cap.isOpened())
        {
            printf("Failed to open RTSP capture.\n");
            return -1;
        }
        for (int i = 0; i < 12; i++) // auto-exposure settle
            cap.grab();
        for (int attempt = 0; attempt < 10 && !found; attempt++)
        {
            if (!cap.read(frame_bgr) || frame_bgr.empty())
            {
                printf("Failed to read frame.\n");
                return -1;
            }
            detect_frame(&ctx, frame_bgr, &results);
            found = count_persons(&results) > 0;
            if (found) break;
            printf("attempt %d: no person, retrying...\n", attempt + 1);
            sleep(1);
        }
        cap.release();
    }
    else
    {
        printf("Reading image: %s\n", input);
        frame_bgr = cv::imread(input, cv::IMREAD_COLOR);
        if (frame_bgr.empty())
        {
            printf("Failed to read image: %s\n", input);
            return -1;
        }
        detect_frame(&ctx, frame_bgr, &results);
        found = count_persons(&results) > 0;
    }

    int W = frame_bgr.cols, H = frame_bgr.rows;
    float scale640 = 640.0f / (W > H ? W : H);
    printf("Frame size: %dx%d  (letterbox scale to 640 = %.4f)\n", W, H, scale640);

    if (!cv::imwrite(out_bmp, frame_bgr))
        printf("Failed to write %s\n", out_bmp);
    else
        printf("Saved: %s\n", out_bmp);

    printf("==== result ====\n");
    if (found)
    {
        print_persons(&results, "[FULL]", 0, 0, 1.0f, scale640, W, H);
    }
    else
    {
        printf("Full-frame: NO person detected.\n");
        // Diagnosis: what DID it see (person may be misclassified)?
        for (int i = 0; i < results.count && i < 8; i++)
            printf("  other[%d]: %s conf %.2f box=(%d,%d)-(%d,%d)\n", i,
                   yolov5_coco_cls_to_name(results.results[i].cls_id),
                   results.results[i].prop,
                   results.results[i].box.left, results.results[i].box.top,
                   results.results[i].box.right, results.results[i].box.bottom);
        // Tile retry: 2x2 overlapping tiles, upscaled 2x
        printf("Tile retry (2x2, 2x upscale)...\n");
        int tw = W / 2, th = H / 2;
        int tile_found = 0;
        for (int ty = 0; ty < 2 && !tile_found; ty++)
        {
            for (int tx = 0; tx < 2 && !tile_found; tx++)
            {
                int ox = tx * tw / 2, oy = ty * th / 2; // 50% overlap
                cv::Mat tile = frame_bgr(cv::Rect(ox, oy, tw, th)).clone();
                cv::resize(tile, tile, cv::Size(tw * 2, th * 2), 0, 0, cv::INTER_LINEAR);
                yolov5_result_list tres;
                detect_frame(&ctx, tile, &tres);
                if (count_persons(&tres) > 0)
                {
                    char tag[32];
                    snprintf(tag, sizeof(tag), "[TILE %d,%d]", tx, ty);
                    print_persons(&tres, tag, ox, oy, 2.0f, scale640, W, H);
                    tile_found = 1;
                }
            }
        }
        if (!tile_found)
            printf("Tile retry: still NO person. Target too small/occluded - measure manually on %s\n", out_bmp);
    }

    release_yolov5_person_model(&ctx);
    yolov5_deinit_post_process();
    return 0;
}
