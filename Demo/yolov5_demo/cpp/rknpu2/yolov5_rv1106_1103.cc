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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#include "yolov5.h"
#include "common.h"
#include "file_utils.h"
#include "image_utils.h"

static void dump_tensor_attr(rknn_tensor_attr *attr)
{
    printf("  index=%d, name=%s, n_dims=%d, dims=[%d, %d, %d, %d], n_elems=%d, size=%d, fmt=%s, type=%s, qnt_type=%s, "
           "zp=%d, scale=%f\n",
           attr->index, attr->name, attr->n_dims, attr->dims[0], attr->dims[1], attr->dims[2], attr->dims[3],
           attr->n_elems, attr->size, get_format_string(attr->fmt), get_type_string(attr->type),
           get_qnt_type_string(attr->qnt_type), attr->zp, attr->scale);
}

int init_yolov5_model(const char *model_path, rknn_app_context_t *app_ctx)
{
    if (model_path == nullptr || app_ctx == nullptr)
    {
        return -1;
    }

    int ret = 0;
    rknn_context ctx = 0;
    rknn_input_output_num io_num{};
    std::vector<rknn_tensor_attr> input_attrs;
    std::vector<rknn_tensor_attr> output_attrs;

    ret = rknn_init(&ctx, (char *)model_path, 0, 0, NULL);
    if (ret < 0)
    {
        printf("rknn_init fail! ret=%d\n", ret);
        return -1;
    }
    app_ctx->rknn_ctx = ctx;

    // Get Model Input Output Number
    ret = rknn_query(ctx, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num));
    if (ret != RKNN_SUCC)
    {
        printf("rknn_query fail! ret=%d\n", ret);
        goto fail;
    }

    if (io_num.n_input != 1 || io_num.n_output != 3 ||
        io_num.n_input > sizeof(app_ctx->input_mems) / sizeof(app_ctx->input_mems[0]) ||
        io_num.n_output > sizeof(app_ctx->output_mems) / sizeof(app_ctx->output_mems[0]))
    {
        printf("unsupported model io count: input=%u output=%u\n",
               io_num.n_input, io_num.n_output);
        goto fail;
    }
    app_ctx->io_num = io_num;
    //printf("model input num: %d, output num: %d\n", io_num.n_input, io_num.n_output);

    // Get Model Input Info
    //printf("input tensors:\n");
    input_attrs.resize(io_num.n_input);
    memset(input_attrs.data(), 0, input_attrs.size() * sizeof(rknn_tensor_attr));
    for (int i = 0; i < io_num.n_input; i++)
    {
        input_attrs[i].index = i;
        ret = rknn_query(ctx, RKNN_QUERY_NATIVE_INPUT_ATTR, &(input_attrs[i]), sizeof(rknn_tensor_attr));
        if (ret != RKNN_SUCC)
        {
            printf("rknn_query fail! ret=%d\n", ret);
            goto fail;
        }
        dump_tensor_attr(&(input_attrs[i]));
    }

    // Get Model Output Info
    //printf("output tensors:\n");
    output_attrs.resize(io_num.n_output);
    memset(output_attrs.data(), 0, output_attrs.size() * sizeof(rknn_tensor_attr));
    for (int i = 0; i < io_num.n_output; i++)
    {
        output_attrs[i].index = i;
        //When using the zero-copy API interface, query the native output tensor attribute
        ret = rknn_query(ctx, RKNN_QUERY_NATIVE_NHWC_OUTPUT_ATTR, &(output_attrs[i]), sizeof(rknn_tensor_attr));
        if (ret != RKNN_SUCC)
        {
            printf("rknn_query fail! ret=%d\n", ret);
            goto fail;
        }
        dump_tensor_attr(&(output_attrs[i]));
    }

    // default input type is int8 (normalize and quantize need compute in outside)
    // if set uint8, will fuse normalize and quantize to npu
    input_attrs[0].type = RKNN_TENSOR_UINT8;
    // default fmt is NHWC,1106 npu only support NHWC in zero copy mode
    input_attrs[0].fmt = RKNN_TENSOR_NHWC;
    //printf("input_attrs[0].size_with_stride=%d\n", input_attrs[0].size_with_stride);
    app_ctx->input_mems[0] = rknn_create_mem(ctx, input_attrs[0].size_with_stride);
    if (app_ctx->input_mems[0] == nullptr)
    {
        printf("rknn_create_mem(input) fail!\n");
        goto fail;
    }

    // Set input tensor memory
    ret = rknn_set_io_mem(ctx, app_ctx->input_mems[0], &input_attrs[0]);
    if (ret < 0) {
        printf("input_mems rknn_set_io_mem fail! ret=%d\n", ret);
        goto fail;
    }

    // Set output tensor memory
    for (uint32_t i = 0; i < io_num.n_output; ++i) {
        app_ctx->output_mems[i] = rknn_create_mem(ctx, output_attrs[i].size_with_stride);
        if (app_ctx->output_mems[i] == nullptr) {
            printf("rknn_create_mem(output %u) fail!\n", i);
            goto fail;
        }
        ret = rknn_set_io_mem(ctx, app_ctx->output_mems[i], &output_attrs[i]);
        if (ret < 0) {
            printf("output_mems rknn_set_io_mem fail! ret=%d\n", ret);
            goto fail;
        }
    }

    // TODO
    if (output_attrs[0].qnt_type == RKNN_TENSOR_QNT_AFFINE_ASYMMETRIC)
    {
        app_ctx->is_quant = true;
    }
    else
    {
        app_ctx->is_quant = false;
    }

    app_ctx->input_attrs = (rknn_tensor_attr *)malloc(io_num.n_input * sizeof(rknn_tensor_attr));
    if (app_ctx->input_attrs == nullptr)
    {
        goto fail;
    }
    memcpy(app_ctx->input_attrs, input_attrs.data(), io_num.n_input * sizeof(rknn_tensor_attr));
    app_ctx->output_attrs = (rknn_tensor_attr *)malloc(io_num.n_output * sizeof(rknn_tensor_attr));
    if (app_ctx->output_attrs == nullptr)
    {
        goto fail;
    }
    memcpy(app_ctx->output_attrs, output_attrs.data(), io_num.n_output * sizeof(rknn_tensor_attr));

    if (input_attrs[0].fmt == RKNN_TENSOR_NCHW) 
    {
        printf("model is NCHW input fmt\n");
        app_ctx->model_channel = input_attrs[0].dims[1];
        app_ctx->model_height  = input_attrs[0].dims[2];
        app_ctx->model_width   = input_attrs[0].dims[3];
    } else 
    {
        printf("model is NHWC input fmt\n");
        app_ctx->model_height  = input_attrs[0].dims[1];
        app_ctx->model_width   = input_attrs[0].dims[2];
        app_ctx->model_channel = input_attrs[0].dims[3];
    } 

    printf("model input height=%d, width=%d, channel=%d\n",
           app_ctx->model_height, app_ctx->model_width, app_ctx->model_channel);

    return 0;

fail:
    release_yolov5_model(app_ctx);
    return -1;
}

int release_yolov5_model(rknn_app_context_t *app_ctx)
{
    if (app_ctx == nullptr)
    {
        return -1;
    }

    for (uint32_t i = 0; i < app_ctx->io_num.n_input &&
                         i < sizeof(app_ctx->input_mems) / sizeof(app_ctx->input_mems[0]); i++)
    {
        if (app_ctx->input_mems[i] != nullptr && app_ctx->rknn_ctx != 0)
        {
            rknn_destroy_mem(app_ctx->rknn_ctx, app_ctx->input_mems[i]);
            app_ctx->input_mems[i] = nullptr;
        }
    }
    for (uint32_t i = 0; i < app_ctx->io_num.n_output &&
                         i < sizeof(app_ctx->output_mems) / sizeof(app_ctx->output_mems[0]); i++)
    {
        if (app_ctx->output_mems[i] != nullptr && app_ctx->rknn_ctx != 0)
        {
            rknn_destroy_mem(app_ctx->rknn_ctx, app_ctx->output_mems[i]);
            app_ctx->output_mems[i] = nullptr;
        }
    }

    free(app_ctx->input_attrs);
    app_ctx->input_attrs = nullptr;
    free(app_ctx->output_attrs);
    app_ctx->output_attrs = nullptr;

    if (app_ctx->rknn_ctx != 0)
    {
        rknn_destroy(app_ctx->rknn_ctx);
        app_ctx->rknn_ctx = 0;
    }
    app_ctx->io_num = {};
    return 0;
}

int inference_yolov5_model(rknn_app_context_t *app_ctx,  object_detect_result_list *od_results)
{
    if (app_ctx == nullptr || od_results == nullptr || app_ctx->rknn_ctx == 0)
    {
        return -1;
    }

    int ret;
    const float nms_threshold = NMS_THRESH;      // 默认的NMS阈值
    const float box_conf_threshold = BOX_THRESH; // 默认的置信度阈值
   
    ret = rknn_run(app_ctx->rknn_ctx, nullptr);
    if (ret < 0) {
        printf("rknn_run fail! ret=%d\n", ret);
        return -1;
    }

    // Post Process
    ret = post_process(app_ctx, app_ctx->output_mems, box_conf_threshold,
                       nms_threshold, od_results);
    return ret;
}

int inference_yolov5_model_dmabuf(rknn_app_context_t *app_ctx,
                                  int dma_buf_fd,
                                  void *virt_addr,
                                  size_t buffer_size,
                                  object_detect_result_list *od_results)
{
    if (app_ctx == nullptr || app_ctx->rknn_ctx == 0 || app_ctx->input_attrs == nullptr ||
        app_ctx->input_mems[0] == nullptr || dma_buf_fd < 0 || virt_addr == nullptr ||
        od_results == nullptr)
    {
        return -1;
    }

    const uint32_t required_size = app_ctx->input_attrs[0].size_with_stride;
    if (buffer_size < required_size)
    {
        printf("dmabuf is too small: have=%zu need=%u\n", buffer_size, required_size);
        return -1;
    }

    rknn_tensor_mem *external_mem = rknn_create_mem_from_fd(
        app_ctx->rknn_ctx, dma_buf_fd, virt_addr, required_size, 0);
    if (external_mem == nullptr)
    {
        printf("rknn_create_mem_from_fd fail! fd=%d size=%u\n",
               dma_buf_fd, required_size);
        return -1;
    }

    int ret = rknn_set_io_mem(app_ctx->rknn_ctx, external_mem,
                              &app_ctx->input_attrs[0]);
    if (ret == RKNN_SUCC)
    {
        ret = inference_yolov5_model(app_ctx, od_results);
    }
    else
    {
        printf("external input rknn_set_io_mem fail! ret=%d\n", ret);
    }

    const int restore_ret = rknn_set_io_mem(app_ctx->rknn_ctx,
                                             app_ctx->input_mems[0],
                                             &app_ctx->input_attrs[0]);
    if (restore_ret != RKNN_SUCC)
    {
        printf("restore internal input memory fail! ret=%d\n", restore_ret);
        ret = restore_ret;
    }

    const int destroy_ret = rknn_destroy_mem(app_ctx->rknn_ctx, external_mem);
    if (destroy_ret != RKNN_SUCC)
    {
        printf("destroy imported dmabuf memory fail! ret=%d\n", destroy_ret);
        if (ret == RKNN_SUCC)
        {
            ret = destroy_ret;
        }
    }
    return ret;
}
