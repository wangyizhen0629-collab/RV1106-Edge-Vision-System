// Copyright (c) 2023 by Rockchip Electronics Co., Ltd. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "AIcamera_c_interface.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <inttypes.h>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <pthread.h>

#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>

#if defined(DESKBOT_HAS_RK_MPI)
#include "rk_media_pipeline.h"
#endif
#include "yolov5.h"

namespace {

constexpr int kDisplayWidth = 320;
constexpr int kDisplayHeight = 240;
constexpr int kDisplayPixelSize = 2;
constexpr size_t kDisplayBufferSize =
    static_cast<size_t>(kDisplayWidth) * kDisplayHeight * kDisplayPixelSize;
constexpr size_t kLatencyBucketCount = 1001;

uint64_t monotonic_us()
{
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(now).count());
}

const char *backend_name(ai_camera_backend_t backend)
{
    switch (backend) {
    case AI_CAMERA_BACKEND_RK_MPI:
        return "rkmpi";
    case AI_CAMERA_BACKEND_OPENCV:
        return "opencv";
    default:
        return "auto";
    }
}

struct Histogram {
    std::array<uint64_t, kLatencyBucketCount> buckets{};
    uint64_t count = 0;
    uint64_t sum_us = 0;

    void add(uint64_t duration_us)
    {
        const size_t bucket = std::min(
            static_cast<size_t>((duration_us + 999U) / 1000U),
            kLatencyBucketCount - 1U);
        ++buckets[bucket];
        ++count;
        sum_us += duration_us;
    }

    double average_ms() const
    {
        return count == 0 ? 0.0
                          : static_cast<double>(sum_us) /
                                static_cast<double>(count) / 1000.0;
    }

    double p95_ms() const
    {
        if (count == 0) {
            return 0.0;
        }
        const uint64_t target = (count * 95U + 99U) / 100U;
        uint64_t cumulative = 0;
        for (size_t i = 0; i < buckets.size(); ++i) {
            cumulative += buckets[i];
            if (cumulative >= target) {
                return static_cast<double>(i);
            }
        }
        return static_cast<double>(buckets.size() - 1U);
    }
};

struct CameraStatsState {
    ai_camera_backend_t backend = AI_CAMERA_BACKEND_AUTO;
    uint64_t started_at_us = 0;
    uint64_t first_published_at_us = 0;
    uint64_t last_published_at_us = 0;
    uint64_t frames_acquired = 0;
    uint64_t frames_inferred = 0;
    uint64_t frames_published = 0;
    uint64_t measurement_frames = 0;
    uint64_t warmup_frames = 0;
    uint64_t media_pts_frames = 0;
    uint64_t software_start_frames = 0;
    uint64_t queue_drops = 0;
    uint64_t frame_timeouts = 0;
    uint64_t recoveries = 0;
    uint64_t failures = 0;
    uint32_t max_queue_depth = 0;
    Histogram end_to_end;
    Histogram inference;
    Histogram queue_wait;
};

std::mutex g_stats_mutex;
CameraStatsState g_stats;

void reset_stats(uint32_t warmup_frames)
{
    std::lock_guard<std::mutex> lock(g_stats_mutex);
    g_stats = {};
    g_stats.started_at_us = monotonic_us();
    g_stats.warmup_frames = warmup_frames;
}

void set_stats_backend(ai_camera_backend_t backend)
{
    std::lock_guard<std::mutex> lock(g_stats_mutex);
    g_stats.backend = backend;
}

void record_acquired_frame()
{
    std::lock_guard<std::mutex> lock(g_stats_mutex);
    ++g_stats.frames_acquired;
}

void record_failure()
{
    std::lock_guard<std::mutex> lock(g_stats_mutex);
    ++g_stats.failures;
}

void record_recovery()
{
    std::lock_guard<std::mutex> lock(g_stats_mutex);
    ++g_stats.recoveries;
}

#if defined(DESKBOT_HAS_RK_MPI)
void update_media_stats(const RkMediaPipelineStats &media)
{
    std::lock_guard<std::mutex> lock(g_stats_mutex);
    g_stats.frames_acquired = media.frames_acquired;
    g_stats.queue_drops = media.frames_dropped;
    g_stats.frame_timeouts = media.get_timeouts;
    g_stats.recoveries = media.recovery_successes;
    g_stats.max_queue_depth = media.max_queue_depth;
    const uint64_t recovery_failures =
        media.recovery_attempts - media.recovery_successes;
    g_stats.failures = std::max(
        g_stats.failures,
        media.invalid_frames + media.release_failures + recovery_failures);
}
#endif

void record_completed_frame(uint64_t queue_wait_us, uint64_t inference_us,
                            uint64_t end_to_end_us, bool used_media_pts)
{
    const uint64_t now_us = monotonic_us();
    std::lock_guard<std::mutex> lock(g_stats_mutex);
    ++g_stats.frames_inferred;
    ++g_stats.frames_published;
    if (g_stats.frames_published <= g_stats.warmup_frames) {
        return;
    }
    ++g_stats.measurement_frames;
    if (used_media_pts) {
        ++g_stats.media_pts_frames;
    } else {
        ++g_stats.software_start_frames;
    }
    if (g_stats.first_published_at_us == 0) {
        g_stats.first_published_at_us = now_us;
    }
    g_stats.last_published_at_us = now_us;
    g_stats.queue_wait.add(queue_wait_us);
    g_stats.inference.add(inference_us);
    g_stats.end_to_end.add(end_to_end_us);
}

ai_camera_stats_t snapshot_stats()
{
    const uint64_t now_us = monotonic_us();
    std::lock_guard<std::mutex> lock(g_stats_mutex);

    ai_camera_stats_t result{};
    result.struct_size = sizeof(result);
    result.backend = g_stats.backend;
    result.uptime_ms = g_stats.started_at_us == 0
                           ? 0
                           : (now_us - g_stats.started_at_us) / 1000U;
    result.frames_acquired = g_stats.frames_acquired;
    result.frames_inferred = g_stats.frames_inferred;
    result.frames_published = g_stats.frames_published;
    result.measurement_frames = g_stats.measurement_frames;
    result.warmup_frames_ignored =
        std::min(g_stats.frames_published, g_stats.warmup_frames);
    result.media_pts_frames = g_stats.media_pts_frames;
    result.software_start_frames = g_stats.software_start_frames;
    result.queue_drops = g_stats.queue_drops;
    result.frame_timeouts = g_stats.frame_timeouts;
    result.recoveries = g_stats.recoveries;
    result.failures = g_stats.failures;
    result.max_queue_depth = g_stats.max_queue_depth;
    if (g_stats.measurement_frames > 1 &&
        g_stats.last_published_at_us > g_stats.first_published_at_us) {
        result.fps = static_cast<double>(g_stats.measurement_frames - 1U) * 1000000.0 /
                     static_cast<double>(g_stats.last_published_at_us -
                                         g_stats.first_published_at_us);
    }
    result.end_to_end_avg_ms = g_stats.end_to_end.average_ms();
    result.end_to_end_p95_ms = g_stats.end_to_end.p95_ms();
    result.inference_avg_ms = g_stats.inference.average_ms();
    result.inference_p95_ms = g_stats.inference.p95_ms();
    result.queue_wait_avg_ms = g_stats.queue_wait.average_ms();
    result.queue_wait_p95_ms = g_stats.queue_wait.p95_ms();
    return result;
}

void log_metrics(bool final)
{
    const ai_camera_stats_t stats = snapshot_stats();
    std::fprintf(stdout,
                 "[ai_camera_metrics] final=%d backend=%s uptime_s=%.3f "
                 "frames=%" PRIu64 " published=%" PRIu64
                 " warmup=%" PRIu64 " acquired=%" PRIu64 " fps=%.3f "
                 "e2e_avg_ms=%.3f e2e_p95_ms=%.3f infer_avg_ms=%.3f "
                 "infer_p95_ms=%.3f queue_avg_ms=%.3f queue_p95_ms=%.3f "
                 "queue_drop=%" PRIu64 " timeouts=%" PRIu64
                 " recoveries=%" PRIu64 " failures=%" PRIu64
                 " max_queue=%u media_pts=%" PRIu64
                 " software_start=%" PRIu64 "\n",
                 final ? 1 : 0, backend_name(stats.backend),
                 static_cast<double>(stats.uptime_ms) / 1000.0,
                 stats.measurement_frames, stats.frames_published,
                 stats.warmup_frames_ignored, stats.frames_acquired, stats.fps,
                 stats.end_to_end_avg_ms, stats.end_to_end_p95_ms,
                 stats.inference_avg_ms, stats.inference_p95_ms,
                 stats.queue_wait_avg_ms, stats.queue_wait_p95_ms,
                 stats.queue_drops, stats.frame_timeouts, stats.recoveries,
                 stats.failures, stats.max_queue_depth, stats.media_pts_frames,
                 stats.software_start_frames);
    std::fflush(stdout);
}

std::mutex g_frame_mutex;
std::array<uint8_t *, 2> g_output_buffers{{nullptr, nullptr}};
int g_front_buffer = 0;
uint64_t g_frame_sequence = 0;

bool allocate_output_buffers()
{
    std::lock_guard<std::mutex> lock(g_frame_mutex);
    g_output_buffers[0] = static_cast<uint8_t *>(std::calloc(1, kDisplayBufferSize));
    g_output_buffers[1] = static_cast<uint8_t *>(std::calloc(1, kDisplayBufferSize));
    if (g_output_buffers[0] == nullptr || g_output_buffers[1] == nullptr) {
        std::free(g_output_buffers[0]);
        std::free(g_output_buffers[1]);
        g_output_buffers = {{nullptr, nullptr}};
        return false;
    }
    g_front_buffer = 0;
    g_frame_sequence = 0;
    return true;
}

void free_output_buffers()
{
    std::lock_guard<std::mutex> lock(g_frame_mutex);
    std::free(g_output_buffers[0]);
    std::free(g_output_buffers[1]);
    g_output_buffers = {{nullptr, nullptr}};
    g_front_buffer = 0;
    g_frame_sequence = 0;
}

bool publish_output(const uint8_t *pixels)
{
    if (pixels == nullptr) {
        return false;
    }
    std::lock_guard<std::mutex> lock(g_frame_mutex);
    if (g_output_buffers[0] == nullptr || g_output_buffers[1] == nullptr) {
        return false;
    }
    const int back_buffer = 1 - g_front_buffer;
    std::memcpy(g_output_buffers[back_buffer], pixels, kDisplayBufferSize);
    g_front_buffer = back_buffer;
    ++g_frame_sequence;
    return true;
}

class RenderBuffers {
public:
    RenderBuffers()
        : display_rgb_(kDisplayHeight, kDisplayWidth, CV_8UC3),
          display_bgr_(kDisplayHeight, kDisplayWidth, CV_8UC3),
          display_565_(kDisplayHeight, kDisplayWidth, CV_16UC1)
    {
    }

    bool render(const cv::Mat &source, bool source_is_rgb,
                const object_detect_result_list &results,
                int model_width, int model_height, double fps)
    {
        if (source.empty() || model_width <= 0 || model_height <= 0) {
            return false;
        }

        if (source_is_rgb) {
            cv::resize(source, display_rgb_, cv::Size(kDisplayWidth, kDisplayHeight),
                       0, 0, cv::INTER_LINEAR);
            cv::cvtColor(display_rgb_, display_bgr_, cv::COLOR_RGB2BGR);
        } else {
            cv::resize(source, display_bgr_, cv::Size(kDisplayWidth, kDisplayHeight),
                       0, 0, cv::INTER_LINEAR);
        }

        for (int i = 0; i < results.count; ++i) {
            const object_detect_result &det = results.results[i];
            int left = det.box.left * kDisplayWidth / model_width;
            int top = det.box.top * kDisplayHeight / model_height;
            int right = det.box.right * kDisplayWidth / model_width;
            int bottom = det.box.bottom * kDisplayHeight / model_height;
            left = std::clamp(left, 0, kDisplayWidth - 1);
            right = std::clamp(right, 0, kDisplayWidth - 1);
            top = std::clamp(top, 0, kDisplayHeight - 1);
            bottom = std::clamp(bottom, 0, kDisplayHeight - 1);
            if (right <= left || bottom <= top) {
                continue;
            }

            cv::rectangle(display_bgr_, cv::Point(left, top),
                          cv::Point(right, bottom), cv::Scalar(0, 255, 0), 2);
            char label[128];
            const char *class_name = coco_cls_to_name(det.cls_id);
            std::snprintf(label, sizeof(label), "%s %.1f%%",
                          class_name == nullptr ? "unknown" : class_name,
                          det.prop * 100.0F);
            cv::putText(display_bgr_, label, cv::Point(left, std::max(top - 4, 10)),
                        cv::FONT_HERSHEY_SIMPLEX, 0.4, cv::Scalar(0, 255, 0), 1);
        }

        char fps_text[32];
        std::snprintf(fps_text, sizeof(fps_text), "fps=%.1f", fps);
        cv::putText(display_bgr_, fps_text, cv::Point(0, 14),
                    cv::FONT_HERSHEY_SIMPLEX, 0.4, cv::Scalar(0, 255, 0), 1);
        cv::cvtColor(display_bgr_, display_565_, cv::COLOR_BGR2BGR565);
        return publish_output(display_565_.data);
    }

private:
    cv::Mat display_rgb_;
    cv::Mat display_bgr_;
    cv::Mat display_565_;
};

struct CameraThreadConfig {
    ai_camera_config_t camera{};
    std::string model_path;
    std::string iq_dir;
};

enum class StartupState {
    idle,
    pending,
    ready,
    failed,
};

std::mutex g_lifecycle_mutex;
std::condition_variable g_startup_condition;
pthread_t g_camera_thread{};
bool g_thread_created = false;
bool g_camera_running = false;
StartupState g_startup_state = StartupState::idle;
int g_startup_error = 0;
std::atomic<bool> g_stop_requested{false};

void signal_startup(StartupState state, int error)
{
    std::lock_guard<std::mutex> lock(g_lifecycle_mutex);
    if (g_startup_state == StartupState::pending) {
        g_startup_state = state;
        g_startup_error = error;
        g_camera_running = state == StartupState::ready;
        g_startup_condition.notify_all();
    }
}

#if defined(DESKBOT_HAS_RK_MPI)
uint64_t capture_timestamp_us(uint64_t frame_pts, uint64_t acquired_at_us,
                              bool *used_media_pts)
{
    const uint64_t now_us = monotonic_us();
    if (frame_pts != 0 && frame_pts <= now_us && now_us - frame_pts < 10000000U) {
        *used_media_pts = true;
        return frame_pts;
    }
    *used_media_pts = false;
    return acquired_at_us;
}
#endif

void maybe_log_metrics(const CameraThreadConfig &config)
{
    const uint32_t interval = config.camera.metrics_log_interval_frames;
    if (interval == 0) {
        return;
    }
    const ai_camera_stats_t stats = snapshot_stats();
    if (stats.measurement_frames != 0 &&
        stats.measurement_frames % interval == 0) {
        log_metrics(false);
    }
}

#if defined(DESKBOT_HAS_RK_MPI)
int run_rkmpi_backend(rknn_app_context_t *rknn_context,
                      const CameraThreadConfig &config)
{
    RkMediaPipelineConfig media_config;
    media_config.vi_dev_id = config.camera.vi_dev_id;
    media_config.vi_pipe_id = config.camera.vi_pipe_id;
    media_config.vi_channel_id = config.camera.vi_channel_id;
    media_config.vpss_group_id = config.camera.vpss_group_id;
    media_config.vpss_channel_id = config.camera.vpss_channel_id;
    media_config.source_width = config.camera.source_width;
    media_config.source_height = config.camera.source_height;
    media_config.output_width = rknn_context->model_width;
    media_config.output_height = rknn_context->model_height;
    media_config.queue_capacity = config.camera.queue_capacity;
    media_config.frame_timeout_ms = config.camera.frame_timeout_ms;
    media_config.timeout_recovery_threshold =
        config.camera.timeout_recovery_threshold;
    media_config.manage_isp = config.camera.manage_isp != 0;
    media_config.iq_dir = config.iq_dir;

    RkMediaPipeline pipeline(std::move(media_config));
    if (pipeline.start() != 0) {
        return -2;
    }

    set_stats_backend(AI_CAMERA_BACKEND_RK_MPI);
    signal_startup(StartupState::ready, 0);
    RenderBuffers renderer;
    int consecutive_failures = 0;

    while (!g_stop_requested.load(std::memory_order_acquire)) {
        RkMediaFrame frame{};
        if (!pipeline.pop(&frame, 100)) {
            update_media_stats(pipeline.stats());
            if (!pipeline.is_running()) {
                break;
            }
            continue;
        }

        bool completed = false;
        const uint64_t queue_exit_us = monotonic_us();
        const uint64_t queue_wait_us = queue_exit_us - frame.acquired_at_us;
        bool used_media_pts = false;
        const uint64_t frame_start_us = capture_timestamp_us(
            frame.info.stVFrame.u64PTS, frame.acquired_at_us, &used_media_pts);
        uint64_t inference_us = 0;

        try {
            const uint32_t expected_stride =
                rknn_context->input_attrs[0].w_stride == 0
                    ? static_cast<uint32_t>(rknn_context->model_width)
                    : rknn_context->input_attrs[0].w_stride;
            if (frame.info.stVFrame.u32VirWidth != expected_stride ||
                frame.info.stVFrame.u32VirHeight <
                    static_cast<uint32_t>(rknn_context->model_height)) {
                std::fprintf(stderr,
                             "[ai_camera] incompatible VPSS stride: %ux%u, RKNN expects %ux%d\n",
                             frame.info.stVFrame.u32VirWidth,
                             frame.info.stVFrame.u32VirHeight, expected_stride,
                             rknn_context->model_height);
            } else {
                void *virtual_address = pipeline.virtual_address(frame);
                const int dma_buf_fd = pipeline.dma_buf_fd(frame);
                const size_t buffer_size = pipeline.buffer_size(frame);
                object_detect_result_list results{};

                const uint64_t inference_start_us = monotonic_us();
                const int inference_ret = inference_yolov5_model_dmabuf(
                    rknn_context, dma_buf_fd, virtual_address, buffer_size, &results);
                inference_us = monotonic_us() - inference_start_us;

                if (inference_ret == 0 && pipeline.sync_for_cpu(frame) == 0) {
                    const size_t row_stride =
                        static_cast<size_t>(frame.info.stVFrame.u32VirWidth) * 3U;
                    cv::Mat rgb_frame(rknn_context->model_height,
                                      rknn_context->model_width, CV_8UC3,
                                      virtual_address, row_stride);
                    const double fps = snapshot_stats().fps;
                    completed = renderer.render(rgb_frame, true, results,
                                                rknn_context->model_width,
                                                rknn_context->model_height, fps);
                }
            }
        } catch (const cv::Exception &error) {
            std::fprintf(stderr, "[ai_camera] OpenCV render failure: %s\n", error.what());
        }

        pipeline.release(&frame);
        update_media_stats(pipeline.stats());

        if (completed) {
            consecutive_failures = 0;
            const uint64_t end_us = monotonic_us();
            record_completed_frame(queue_wait_us, inference_us,
                                   end_us - frame_start_us, used_media_pts);
            maybe_log_metrics(config);
        } else {
            ++consecutive_failures;
            record_failure();
            if (consecutive_failures >= config.camera.timeout_recovery_threshold) {
                std::fprintf(stderr,
                             "[ai_camera] %d consecutive inference/render failures\n",
                             consecutive_failures);
                break;
            }
        }
    }

    pipeline.stop();
    update_media_stats(pipeline.stats());
    return 0;
}
#else
int run_rkmpi_backend(rknn_app_context_t *, const CameraThreadConfig &)
{
    std::fprintf(stderr, "[ai_camera] RK MPI backend is not built\n");
    return -2;
}
#endif

bool reopen_opencv_camera(cv::VideoCapture *capture,
                          const CameraThreadConfig &config)
{
    capture->release();
    if (!capture->open(config.camera.opencv_device_index)) {
        return false;
    }
    capture->set(cv::CAP_PROP_FRAME_WIDTH, config.camera.source_width);
    capture->set(cv::CAP_PROP_FRAME_HEIGHT, config.camera.source_height);
    return true;
}

int run_opencv_backend(rknn_app_context_t *rknn_context,
                       const CameraThreadConfig &config)
{
    cv::VideoCapture capture;
    if (!reopen_opencv_camera(&capture, config)) {
        return -2;
    }

    set_stats_backend(AI_CAMERA_BACKEND_OPENCV);
    signal_startup(StartupState::ready, 0);
    RenderBuffers renderer;
    cv::Mat bgr_frame;
    cv::Mat model_input(rknn_context->model_height, rknn_context->model_width,
                        CV_8UC3, rknn_context->input_mems[0]->virt_addr);
    int consecutive_capture_failures = 0;

    while (!g_stop_requested.load(std::memory_order_acquire)) {
        const uint64_t frame_start_us = monotonic_us();
        capture >> bgr_frame;
        if (bgr_frame.empty()) {
            ++consecutive_capture_failures;
            record_failure();
            if (consecutive_capture_failures >=
                config.camera.timeout_recovery_threshold) {
                if (!reopen_opencv_camera(&capture, config)) {
                    break;
                }
                record_recovery();
                consecutive_capture_failures = 0;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        consecutive_capture_failures = 0;
        record_acquired_frame();
        object_detect_result_list results{};
        bool completed = false;
        uint64_t inference_us = 0;
        try {
            cv::resize(bgr_frame, model_input,
                       cv::Size(rknn_context->model_width,
                                rknn_context->model_height),
                       0, 0, cv::INTER_LINEAR);
            const uint64_t inference_start_us = monotonic_us();
            const int inference_ret = inference_yolov5_model(rknn_context, &results);
            inference_us = monotonic_us() - inference_start_us;
            if (inference_ret == 0) {
                completed = renderer.render(bgr_frame, false, results,
                                            rknn_context->model_width,
                                            rknn_context->model_height,
                                            snapshot_stats().fps);
            }
        } catch (const cv::Exception &error) {
            std::fprintf(stderr, "[ai_camera] OpenCV pipeline failure: %s\n", error.what());
        }

        if (completed) {
            const uint64_t end_us = monotonic_us();
            record_completed_frame(0, inference_us, end_us - frame_start_us,
                                   false);
            maybe_log_metrics(config);
        } else {
            record_failure();
        }
    }

    capture.release();
    return 0;
}

void *inference_thread(void *opaque)
{
    std::unique_ptr<CameraThreadConfig> config(
        static_cast<CameraThreadConfig *>(opaque));
    rknn_app_context_t rknn_context{};
    bool post_process_initialized = false;
    int result = -1;

    try {
        if (init_yolov5_model(config->model_path.c_str(), &rknn_context) != 0) {
            signal_startup(StartupState::failed, -1);
        } else if (init_post_process() != 0) {
            signal_startup(StartupState::failed, -1);
        } else {
            post_process_initialized = true;
            const ai_camera_backend_t requested_backend = config->camera.backend;

            if (requested_backend != AI_CAMERA_BACKEND_OPENCV) {
                result = run_rkmpi_backend(&rknn_context, *config);
                if (result == -2 && requested_backend == AI_CAMERA_BACKEND_AUTO &&
                    !g_stop_requested.load(std::memory_order_acquire)) {
                    std::fprintf(stderr,
                                 "[ai_camera] RK MPI init failed; falling back to OpenCV\n");
                    result = run_opencv_backend(&rknn_context, *config);
                }
            } else {
                result = run_opencv_backend(&rknn_context, *config);
            }

            if (result == -2) {
                signal_startup(StartupState::failed, -1);
            }
        }
    } catch (const std::exception &error) {
        std::fprintf(stderr, "[ai_camera] unhandled exception: %s\n", error.what());
        record_failure();
        signal_startup(StartupState::failed, -1);
    } catch (...) {
        std::fprintf(stderr, "[ai_camera] unhandled unknown exception\n");
        record_failure();
        signal_startup(StartupState::failed, -1);
    }

    if (post_process_initialized) {
        deinit_post_process();
    }
    release_yolov5_model(&rknn_context);
    log_metrics(true);

    {
        std::lock_guard<std::mutex> lock(g_lifecycle_mutex);
        if (g_startup_state == StartupState::pending) {
            g_startup_state = StartupState::failed;
            g_startup_error = result == 0 ? -1 : result;
            g_startup_condition.notify_all();
        }
        g_camera_running = false;
    }
    return nullptr;
}

bool parse_environment_int(const char *name, int minimum, int maximum, int *value)
{
    const char *text = std::getenv(name);
    if (text == nullptr || *text == '\0') {
        return false;
    }
    errno = 0;
    char *end = nullptr;
    const long parsed = std::strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || parsed < minimum ||
        parsed > maximum) {
        std::fprintf(stderr, "[ai_camera] ignoring invalid %s=%s\n", name, text);
        return false;
    }
    *value = static_cast<int>(parsed);
    return true;
}

void apply_environment_config(ai_camera_config_t *config)
{
    const char *backend = std::getenv("DESKBOT_CAMERA_BACKEND");
    if (backend != nullptr) {
        if (std::strcmp(backend, "rkmpi") == 0) {
            config->backend = AI_CAMERA_BACKEND_RK_MPI;
        } else if (std::strcmp(backend, "opencv") == 0) {
            config->backend = AI_CAMERA_BACKEND_OPENCV;
        } else if (std::strcmp(backend, "auto") != 0) {
            std::fprintf(stderr,
                         "[ai_camera] ignoring invalid DESKBOT_CAMERA_BACKEND=%s\n",
                         backend);
        }
    }

    parse_environment_int("DESKBOT_CAMERA_SOURCE_WIDTH", 64, 4096,
                          &config->source_width);
    parse_environment_int("DESKBOT_CAMERA_SOURCE_HEIGHT", 64, 4096,
                          &config->source_height);
    parse_environment_int("DESKBOT_CAMERA_QUEUE", 1, 4, &config->queue_capacity);
    parse_environment_int("DESKBOT_CAMERA_TIMEOUT_MS", 1, 5000,
                          &config->frame_timeout_ms);
    parse_environment_int("DESKBOT_CAMERA_RECOVERY_THRESHOLD", 1, 100,
                          &config->timeout_recovery_threshold);
    parse_environment_int("DESKBOT_CAMERA_MANAGE_ISP", 0, 1, &config->manage_isp);
    parse_environment_int("DESKBOT_VI_DEVICE", 0, 7, &config->vi_dev_id);
    parse_environment_int("DESKBOT_VI_PIPE", 0, 7, &config->vi_pipe_id);
    parse_environment_int("DESKBOT_VI_CHANNEL", 0, 7, &config->vi_channel_id);
    parse_environment_int("DESKBOT_VPSS_GROUP", 0, 63, &config->vpss_group_id);
    parse_environment_int("DESKBOT_VPSS_CHANNEL", 0, 3,
                          &config->vpss_channel_id);
    int warmup_frames = static_cast<int>(config->metrics_warmup_frames);
    if (parse_environment_int("DESKBOT_CAMERA_WARMUP_FRAMES", 0, 10000,
                              &warmup_frames)) {
        config->metrics_warmup_frames = static_cast<uint32_t>(warmup_frames);
    }
    const char *iq_dir = std::getenv("DESKBOT_IQ_DIR");
    if (iq_dir != nullptr && *iq_dir != '\0') {
        config->iq_dir = iq_dir;
    }
}

bool valid_config(const ai_camera_config_t &config)
{
    return config.struct_size == sizeof(ai_camera_config_t) &&
           config.backend >= AI_CAMERA_BACKEND_AUTO &&
           config.backend <= AI_CAMERA_BACKEND_OPENCV &&
           config.source_width >= 64 && config.source_height >= 64 &&
           config.queue_capacity >= 1 && config.queue_capacity <= 4 &&
           config.frame_timeout_ms > 0 && config.timeout_recovery_threshold > 0 &&
           config.startup_timeout_ms > 0 &&
           config.metrics_warmup_frames <= 10000 && config.iq_dir != nullptr;
}

} // namespace

void ai_camera_default_config(ai_camera_config_t *config)
{
    if (config == nullptr) {
        return;
    }
    *config = {};
    config->struct_size = sizeof(*config);
    config->backend = AI_CAMERA_BACKEND_AUTO;
    config->vi_dev_id = 0;
    config->vi_pipe_id = 0;
    config->vi_channel_id = 0;
    config->vpss_group_id = 0;
    config->vpss_channel_id = 0;
    config->source_width = 864;
    config->source_height = 480;
    config->queue_capacity = 2;
    config->frame_timeout_ms = 200;
    config->timeout_recovery_threshold = 5;
    config->startup_timeout_ms = 20000;
    config->opencv_device_index = 0;
    config->manage_isp = 1;
    config->iq_dir = "/oem/usr/share/iqfiles";
    config->metrics_warmup_frames = 30;
    config->metrics_log_interval_frames = 300;
}

int start_ai_camera(const char *model_path)
{
    ai_camera_config_t config;
    ai_camera_default_config(&config);
    apply_environment_config(&config);
    return start_ai_camera_ex(model_path, &config);
}

int start_ai_camera_ex(const char *model_path, const ai_camera_config_t *config)
{
    if (model_path == nullptr || *model_path == '\0' || config == nullptr ||
        !valid_config(*config)) {
        return -1;
    }

    std::unique_lock<std::mutex> lock(g_lifecycle_mutex);
    if (g_thread_created) {
        return -1;
    }
    if (!allocate_output_buffers()) {
        return -1;
    }

    std::unique_ptr<CameraThreadConfig> thread_config(new CameraThreadConfig);
    thread_config->camera = *config;
    thread_config->model_path = model_path;
    thread_config->iq_dir = config->iq_dir;
    thread_config->camera.iq_dir = thread_config->iq_dir.c_str();

    reset_stats(config->metrics_warmup_frames);
    g_stop_requested.store(false, std::memory_order_release);
    g_startup_state = StartupState::pending;
    g_startup_error = 0;
    g_camera_running = false;

    const int create_ret = pthread_create(&g_camera_thread, nullptr, inference_thread,
                                          thread_config.get());
    if (create_ret != 0) {
        g_startup_state = StartupState::idle;
        free_output_buffers();
        return -1;
    }
    thread_config.release();
    g_thread_created = true;

    const bool startup_finished = g_startup_condition.wait_for(
        lock, std::chrono::milliseconds(config->startup_timeout_ms), [] {
            return g_startup_state != StartupState::pending;
        });
    if (startup_finished && g_startup_state == StartupState::ready) {
        return 0;
    }

    g_stop_requested.store(true, std::memory_order_release);
    const pthread_t thread = g_camera_thread;
    lock.unlock();
    pthread_join(thread, nullptr);
    lock.lock();
    g_thread_created = false;
    g_camera_running = false;
    g_startup_state = StartupState::idle;
    free_output_buffers();
    return startup_finished ? g_startup_error : -1;
}

int stop_ai_camera(void)
{
    std::unique_lock<std::mutex> lock(g_lifecycle_mutex);
    if (!g_thread_created) {
        return -1;
    }

    g_stop_requested.store(true, std::memory_order_release);
    const pthread_t thread = g_camera_thread;
    lock.unlock();
    const int join_ret = pthread_join(thread, nullptr);
    lock.lock();

    g_thread_created = false;
    g_camera_running = false;
    g_startup_state = StartupState::idle;
    g_startup_error = 0;
    free_output_buffers();
    return join_ret == 0 ? 0 : -1;
}

void get_buf_data(uint8_t *buffer)
{
    (void)get_buf_data_ex(buffer, kDisplayBufferSize, nullptr);
}

int get_buf_data_ex(uint8_t *buffer, size_t buffer_size, uint64_t *sequence)
{
    if (buffer == nullptr || buffer_size < kDisplayBufferSize) {
        return -1;
    }

    std::lock_guard<std::mutex> lock(g_frame_mutex);
    if (g_output_buffers[g_front_buffer] == nullptr) {
        return -1;
    }
    std::memcpy(buffer, g_output_buffers[g_front_buffer], kDisplayBufferSize);
    if (sequence != nullptr) {
        *sequence = g_frame_sequence;
    }
    return g_frame_sequence == 0 ? 1 : 0;
}

int get_ai_camera_stats(ai_camera_stats_t *stats)
{
    if (stats == nullptr) {
        return -1;
    }
    *stats = snapshot_stats();
    return 0;
}
