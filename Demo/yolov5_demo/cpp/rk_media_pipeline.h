#ifndef ECHO_MATE_RK_MEDIA_PIPELINE_H
#define ECHO_MATE_RK_MEDIA_PIPELINE_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "rk_comm_video.h"

struct RkMediaPipelineConfig {
    int vi_dev_id = 0;
    int vi_pipe_id = 0;
    int vi_channel_id = 0;
    int vpss_group_id = 0;
    int vpss_channel_id = 0;
    int source_width = 864;
    int source_height = 480;
    int output_width = 640;
    int output_height = 640;
    int queue_capacity = 2;
    int frame_timeout_ms = 200;
    int timeout_recovery_threshold = 5;
    bool manage_isp = true;
    std::string iq_dir = "/oem/usr/share/iqfiles";
};

struct RkMediaFrame {
    VIDEO_FRAME_INFO_S info{};
    uint64_t acquired_at_us = 0;
    bool valid = false;
};

struct RkMediaPipelineStats {
    uint64_t frames_acquired = 0;
    uint64_t frames_dropped = 0;
    uint64_t get_timeouts = 0;
    uint64_t invalid_frames = 0;
    uint64_t release_failures = 0;
    uint64_t recovery_attempts = 0;
    uint64_t recovery_successes = 0;
    uint32_t max_queue_depth = 0;
};

class RkMediaPipeline {
public:
    explicit RkMediaPipeline(RkMediaPipelineConfig config);
    ~RkMediaPipeline();

    RkMediaPipeline(const RkMediaPipeline &) = delete;
    RkMediaPipeline &operator=(const RkMediaPipeline &) = delete;

    int start();
    void stop();

    bool pop(RkMediaFrame *frame, int timeout_ms);
    int release(RkMediaFrame *frame);
    bool is_running() const;

    void *virtual_address(const RkMediaFrame &frame) const;
    int dma_buf_fd(const RkMediaFrame &frame) const;
    size_t buffer_size(const RkMediaFrame &frame) const;
    int sync_for_cpu(const RkMediaFrame &frame) const;

    RkMediaPipelineStats stats() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

#endif
