#ifndef AICAMERA_C_INTERFACE_H
#define AICAMERA_C_INTERFACE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ai_camera_backend {
    AI_CAMERA_BACKEND_AUTO = 0,
    AI_CAMERA_BACKEND_RK_MPI = 1,
    AI_CAMERA_BACKEND_OPENCV = 2,
} ai_camera_backend_t;

typedef struct ai_camera_config {
    size_t struct_size;
    ai_camera_backend_t backend;
    int vi_dev_id;
    int vi_pipe_id;
    int vi_channel_id;
    int vpss_group_id;
    int vpss_channel_id;
    int source_width;
    int source_height;
    int queue_capacity;
    int frame_timeout_ms;
    int timeout_recovery_threshold;
    int startup_timeout_ms;
    int opencv_device_index;
    int manage_isp;
    const char *iq_dir;
    uint32_t metrics_warmup_frames;
    uint32_t metrics_log_interval_frames;
} ai_camera_config_t;

typedef struct ai_camera_stats {
    size_t struct_size;
    ai_camera_backend_t backend;
    uint64_t uptime_ms;
    uint64_t frames_acquired;
    uint64_t frames_inferred;
    uint64_t frames_published;
    uint64_t measurement_frames;
    uint64_t warmup_frames_ignored;
    uint64_t media_pts_frames;
    uint64_t software_start_frames;
    uint64_t queue_drops;
    uint64_t frame_timeouts;
    uint64_t recoveries;
    uint64_t failures;
    uint32_t max_queue_depth;
    double fps;
    double end_to_end_avg_ms;
    double end_to_end_p95_ms;
    double inference_avg_ms;
    double inference_p95_ms;
    double queue_wait_avg_ms;
    double queue_wait_p95_ms;
} ai_camera_stats_t;

void ai_camera_default_config(ai_camera_config_t *config);

int start_ai_camera(const char *model_path);
int start_ai_camera_ex(const char *model_path, const ai_camera_config_t *config);
int stop_ai_camera(void);

/* Compatibility wrapper: copies the current 320x240 RGB565 front buffer. */
void get_buf_data(uint8_t *buffer);

/* Returns 0 only after at least one complete frame has been published. */
int get_buf_data_ex(uint8_t *buffer, size_t buffer_size, uint64_t *sequence);
int get_ai_camera_stats(ai_camera_stats_t *stats);

#ifdef __cplusplus
}
#endif

#endif
