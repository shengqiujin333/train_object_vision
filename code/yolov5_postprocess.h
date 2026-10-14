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
//
// Adapted from examples/yolov5/cpp/postprocess.h. All macros, types and
// functions are prefixed with yolov5_ to avoid symbol conflicts when
// linked into the yolov8_seg demo (which has its own postprocess.cc).

#ifndef _YOLOV5_PERSON_POSTPROCESS_H_
#define _YOLOV5_PERSON_POSTPROCESS_H_

#include <stdint.h>
#include <vector>
#include "rknn_api.h"
#include "common.h"
#include "image_utils.h"

#define YOLOV5_OBJ_NAME_MAX_SIZE 64
#define YOLOV5_OBJ_NUMB_MAX_SIZE 128
#define YOLOV5_OBJ_CLASS_NUM 80
#define YOLOV5_NMS_THRESH 0.45
#define YOLOV5_BOX_THRESH 0.25
#define YOLOV5_PROP_BOX_SIZE (5 + YOLOV5_OBJ_CLASS_NUM)

// object_detect_result is also defined in yolov8_seg/postprocess.h with identical
// layout. Guard against double definition (C++ rejects duplicate typedef even if
// identical). yolov8_seg.h is included before yolov5_person.h in main.cc, so the
// first definition wins.
#ifndef OBJECT_DETECT_RESULT_DEFINED
#define OBJECT_DETECT_RESULT_DEFINED
typedef struct {
    image_rect_t box;
    float prop;
    int cls_id;
} object_detect_result;
#endif

typedef struct {
    int id;
    int count;
    object_detect_result results[YOLOV5_OBJ_NUMB_MAX_SIZE];
} yolov5_result_list;

int yolov5_init_post_process();
void yolov5_deinit_post_process();
char *yolov5_coco_cls_to_name(int cls_id);
int yolov5_post_process(yolov5_app_context_t *app_ctx, void *outputs, letterbox_t *letter_box,
                        float conf_threshold, float nms_threshold, yolov5_result_list *od_results);

#endif // _YOLOV5_PERSON_POSTPROCESS_H_
