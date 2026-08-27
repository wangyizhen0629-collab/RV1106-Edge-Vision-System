#include "rk_media_pipeline.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <utility>

#include "rk_comm_vi.h"
#include "rk_comm_vpss.h"
#include "rk_mpi_mb.h"
#include "rk_mpi_sys.h"
#include "rk_mpi_vi.h"
#include "rk_mpi_vpss.h"

#if defined(DESKBOT_HAS_RKAIQ)
#include "rk_aiq_user_api2_sysctl.h"
#endif

namespace {

constexpr int kMaxQueueCapacity = 4;
constexpr int kRecoveryAttempts = 3;
constexpr int kRecoveryBackoffMs = 100;

uint64_t monotonic_us()
{
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(now).count());
}

void log_mpi_error(const char *operation, int error)
{
    std::fprintf(stderr, "[rk_media] %s failed: %#x\n", operation, error);
}

} // namespace

class RkMediaPipeline::Impl {
public:
    explicit Impl(RkMediaPipelineConfig config)
        : config_(std::move(config))
    {
        config_.queue_capacity =
            std::clamp(config_.queue_capacity, 1, kMaxQueueCapacity);
        config_.frame_timeout_ms = std::max(config_.frame_timeout_ms, 1);
        config_.timeout_recovery_threshold =
            std::max(config_.timeout_recovery_threshold, 1);
    }

    ~Impl()
    {
        stop();
    }

    int start()
    {
        std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
        if (running_.load(std::memory_order_acquire)) {
            return -1;
        }

        if (init_pipeline() != 0) {
            teardown_pipeline();
            return -1;
        }

        running_.store(true, std::memory_order_release);
        producer_ = std::thread(&Impl::producer_loop, this);
        return 0;
    }

    void stop()
    {
        std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
        const bool was_running = running_.exchange(false, std::memory_order_acq_rel);
        queue_condition_.notify_all();
        ownership_condition_.notify_all();

        if (producer_.joinable()) {
            producer_.join();
        }

        if (was_running || pipeline_initialized_) {
            std::unique_lock<std::mutex> queue_lock(queue_mutex_);
            drain_queue_locked();
            ownership_condition_.wait(queue_lock,
                                      [this] { return outstanding_frames_ == 0; });
            queue_lock.unlock();
            teardown_pipeline();
        }
    }

    bool pop(RkMediaFrame *frame, int timeout_ms)
    {
        if (frame == nullptr) {
            return false;
        }

        std::unique_lock<std::mutex> lock(queue_mutex_);
        const auto ready = [this] {
            return queue_count_ > 0 || !running_.load(std::memory_order_acquire);
        };
        if (timeout_ms < 0) {
            queue_condition_.wait(lock, ready);
        } else if (!queue_condition_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                                              ready)) {
            return false;
        }

        if (queue_count_ == 0) {
            return false;
        }

        *frame = queue_[queue_head_];
        queue_[queue_head_] = {};
        queue_head_ = (queue_head_ + 1U) % queue_.size();
        --queue_count_;
        return true;
    }

    int release(RkMediaFrame *frame)
    {
        if (frame == nullptr || !frame->valid) {
            return -1;
        }

        const int ret = RK_MPI_VPSS_ReleaseChnFrame(
            config_.vpss_group_id, config_.vpss_channel_id, &frame->info);
        frame->valid = false;

        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (outstanding_frames_ > 0) {
            --outstanding_frames_;
        }
        if (ret != RK_SUCCESS) {
            ++stats_.release_failures;
            log_mpi_error("RK_MPI_VPSS_ReleaseChnFrame", ret);
        }
        ownership_condition_.notify_all();
        return ret == RK_SUCCESS ? 0 : -1;
    }

    bool is_running() const
    {
        return running_.load(std::memory_order_acquire);
    }

    void *virtual_address(const RkMediaFrame &frame) const
    {
        if (!frame.valid || frame.info.stVFrame.pMbBlk == nullptr) {
            return nullptr;
        }
        return RK_MPI_MB_Handle2VirAddr(frame.info.stVFrame.pMbBlk);
    }

    int dma_buf_fd(const RkMediaFrame &frame) const
    {
        if (!frame.valid || frame.info.stVFrame.pMbBlk == nullptr) {
            return -1;
        }
        return RK_MPI_MB_Handle2Fd(frame.info.stVFrame.pMbBlk);
    }

    size_t buffer_size(const RkMediaFrame &frame) const
    {
        if (!frame.valid || frame.info.stVFrame.pMbBlk == nullptr) {
            return 0;
        }
        return static_cast<size_t>(RK_MPI_MB_GetSize(frame.info.stVFrame.pMbBlk));
    }

    int sync_for_cpu(const RkMediaFrame &frame) const
    {
        if (!frame.valid || frame.info.stVFrame.pMbBlk == nullptr) {
            return -1;
        }
        const int ret = RK_MPI_SYS_MmzFlushCache(frame.info.stVFrame.pMbBlk, RK_TRUE);
        if (ret != RK_SUCCESS) {
            log_mpi_error("RK_MPI_SYS_MmzFlushCache(read)", ret);
            return -1;
        }
        return 0;
    }

    RkMediaPipelineStats stats() const
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        return stats_;
    }

private:
    int start_isp()
    {
        if (!config_.manage_isp) {
            return 0;
        }
#if defined(DESKBOT_HAS_RKAIQ)
        rk_aiq_static_info_t static_info{};
        XCamReturn aiq_ret =
            rk_aiq_uapi2_sysctl_enumStaticMetasByPhyId(config_.vi_dev_id, &static_info);
        if (aiq_ret != XCAM_RETURN_NO_ERROR) {
            std::fprintf(stderr, "[rk_media] cannot enumerate RKAIQ camera %d: %d\n",
                         config_.vi_dev_id, static_cast<int>(aiq_ret));
            return -1;
        }

        aiq_ret = rk_aiq_uapi2_sysctl_preInit_devBufCnt(
            static_info.sensor_info.sensor_name, "rkraw_rx", 2);
        if (aiq_ret != XCAM_RETURN_NO_ERROR) {
            std::fprintf(stderr, "[rk_media] RKAIQ preInit_devBufCnt failed: %d\n",
                         static_cast<int>(aiq_ret));
            return -1;
        }

        aiq_context_ = rk_aiq_uapi2_sysctl_init(static_info.sensor_info.sensor_name,
                                                config_.iq_dir.c_str(), nullptr, nullptr);
        if (aiq_context_ == nullptr) {
            std::fprintf(stderr, "[rk_media] RKAIQ init failed for sensor %s, iq=%s\n",
                         static_info.sensor_info.sensor_name, config_.iq_dir.c_str());
            return -1;
        }

        aiq_ret = rk_aiq_uapi2_sysctl_prepare(aiq_context_, 0, 0,
                                              RK_AIQ_WORKING_MODE_NORMAL);
        if (aiq_ret != XCAM_RETURN_NO_ERROR) {
            std::fprintf(stderr, "[rk_media] RKAIQ prepare failed: %d\n",
                         static_cast<int>(aiq_ret));
            stop_isp();
            return -1;
        }

        aiq_ret = rk_aiq_uapi2_sysctl_start(aiq_context_);
        if (aiq_ret != XCAM_RETURN_NO_ERROR) {
            std::fprintf(stderr, "[rk_media] RKAIQ start failed: %d\n",
                         static_cast<int>(aiq_ret));
            stop_isp();
            return -1;
        }
        aiq_started_ = true;
        return 0;
#else
        std::fprintf(stderr,
                     "[rk_media] manage_isp requested but RKAIQ support is not built\n");
        return -1;
#endif
    }

    void stop_isp()
    {
#if defined(DESKBOT_HAS_RKAIQ)
        if (aiq_context_ != nullptr) {
            if (aiq_started_) {
                const XCamReturn ret = rk_aiq_uapi2_sysctl_stop(aiq_context_, false);
                if (ret != XCAM_RETURN_NO_ERROR) {
                    std::fprintf(stderr, "[rk_media] RKAIQ stop failed: %d\n",
                                 static_cast<int>(ret));
                }
            }
            rk_aiq_uapi2_sysctl_deinit(aiq_context_);
        }
        aiq_context_ = nullptr;
        aiq_started_ = false;
#endif
    }

    int init_pipeline()
    {
        if (start_isp() != 0) {
            return -1;
        }

        int ret = RK_MPI_SYS_Init();
        if (ret != RK_SUCCESS) {
            log_mpi_error("RK_MPI_SYS_Init", ret);
            stop_isp();
            return -1;
        }
        sys_initialized_ = true;

        VI_DEV_ATTR_S dev_attr{};
        ret = RK_MPI_VI_GetDevAttr(config_.vi_dev_id, &dev_attr);
        if (ret == RK_ERR_VI_NOT_CONFIG) {
            ret = RK_MPI_VI_SetDevAttr(config_.vi_dev_id, &dev_attr);
        }
        if (ret != RK_SUCCESS) {
            log_mpi_error("RK_MPI_VI_Get/SetDevAttr", ret);
            return -1;
        }

        ret = RK_MPI_VI_GetDevIsEnable(config_.vi_dev_id);
        if (ret != RK_SUCCESS) {
            ret = RK_MPI_VI_EnableDev(config_.vi_dev_id);
            if (ret != RK_SUCCESS) {
                log_mpi_error("RK_MPI_VI_EnableDev", ret);
                return -1;
            }
            owns_vi_device_ = true;

            VI_DEV_BIND_PIPE_S bind_pipe{};
            bind_pipe.u32Num = 1;
            bind_pipe.PipeId[0] = config_.vi_pipe_id;
            ret = RK_MPI_VI_SetDevBindPipe(config_.vi_dev_id, &bind_pipe);
            if (ret != RK_SUCCESS) {
                log_mpi_error("RK_MPI_VI_SetDevBindPipe", ret);
                return -1;
            }
        }

        VI_CHN_ATTR_S vi_attr{};
        vi_attr.stIspOpt.u32BufCount =
            static_cast<RK_U32>(std::min(config_.queue_capacity + 2, 8));
        vi_attr.stIspOpt.enMemoryType = VI_V4L2_MEMORY_TYPE_DMABUF;
        vi_attr.stSize.u32Width = static_cast<RK_U32>(config_.source_width);
        vi_attr.stSize.u32Height = static_cast<RK_U32>(config_.source_height);
        vi_attr.enPixelFormat = RK_FMT_YUV420SP;
        vi_attr.enDynamicRange = DYNAMIC_RANGE_SDR8;
        vi_attr.enVideoFormat = VIDEO_FORMAT_LINEAR;
        vi_attr.enCompressMode = COMPRESS_MODE_NONE;
        vi_attr.u32Depth = 0;
        vi_attr.stFrameRate.s32SrcFrameRate = -1;
        vi_attr.stFrameRate.s32DstFrameRate = -1;

        ret = RK_MPI_VI_SetChnAttr(config_.vi_pipe_id, config_.vi_channel_id, &vi_attr);
        if (ret != RK_SUCCESS) {
            log_mpi_error("RK_MPI_VI_SetChnAttr", ret);
            return -1;
        }
        ret = RK_MPI_VI_EnableChn(config_.vi_pipe_id, config_.vi_channel_id);
        if (ret != RK_SUCCESS) {
            log_mpi_error("RK_MPI_VI_EnableChn", ret);
            return -1;
        }
        vi_channel_enabled_ = true;

        VPSS_GRP_ATTR_S group_attr{};
        group_attr.u32MaxW = static_cast<RK_U32>(
            std::max(config_.source_width, config_.output_width));
        group_attr.u32MaxH = static_cast<RK_U32>(
            std::max(config_.source_height, config_.output_height));
        group_attr.enPixelFormat = RK_FMT_YUV420SP;
        group_attr.enDynamicRange = DYNAMIC_RANGE_SDR8;
        group_attr.enCompressMode = COMPRESS_MODE_NONE;
        group_attr.stFrameRate.s32SrcFrameRate = -1;
        group_attr.stFrameRate.s32DstFrameRate = -1;

        ret = RK_MPI_VPSS_CreateGrp(config_.vpss_group_id, &group_attr);
        if (ret != RK_SUCCESS) {
            log_mpi_error("RK_MPI_VPSS_CreateGrp", ret);
            return -1;
        }
        vpss_group_created_ = true;

        VPSS_CHN_ATTR_S channel_attr{};
        channel_attr.enChnMode = VPSS_CHN_MODE_USER;
        channel_attr.u32Width = static_cast<RK_U32>(config_.output_width);
        channel_attr.u32Height = static_cast<RK_U32>(config_.output_height);
        channel_attr.enVideoFormat = VIDEO_FORMAT_LINEAR;
        channel_attr.enPixelFormat = RK_FMT_RGB888;
        channel_attr.enDynamicRange = DYNAMIC_RANGE_SDR8;
        channel_attr.enCompressMode = COMPRESS_MODE_NONE;
        channel_attr.stFrameRate.s32SrcFrameRate = -1;
        channel_attr.stFrameRate.s32DstFrameRate = -1;
        channel_attr.u32Depth =
            static_cast<RK_U32>(std::min(config_.queue_capacity + 1, 8));
        channel_attr.u32FrameBufCnt =
            static_cast<RK_U32>(std::min(config_.queue_capacity + 2, 8));
        channel_attr.stAspectRatio.enMode = ASPECT_RATIO_NONE;

        ret = RK_MPI_VPSS_SetChnAttr(config_.vpss_group_id,
                                     config_.vpss_channel_id, &channel_attr);
        if (ret != RK_SUCCESS) {
            log_mpi_error("RK_MPI_VPSS_SetChnAttr", ret);
            return -1;
        }
        ret = RK_MPI_VPSS_EnableChn(config_.vpss_group_id, config_.vpss_channel_id);
        if (ret != RK_SUCCESS) {
            log_mpi_error("RK_MPI_VPSS_EnableChn", ret);
            return -1;
        }
        vpss_channel_enabled_ = true;

        ret = RK_MPI_VPSS_StartGrp(config_.vpss_group_id);
        if (ret != RK_SUCCESS) {
            log_mpi_error("RK_MPI_VPSS_StartGrp", ret);
            return -1;
        }
        vpss_group_started_ = true;

        vi_source_ = {};
        vi_source_.enModId = RK_ID_VI;
        vi_source_.s32DevId = config_.vi_dev_id;
        vi_source_.s32ChnId = config_.vi_channel_id;
        vpss_destination_ = {};
        vpss_destination_.enModId = RK_ID_VPSS;
        vpss_destination_.s32DevId = config_.vpss_group_id;
        vpss_destination_.s32ChnId = 0;

        ret = RK_MPI_SYS_Bind(&vi_source_, &vpss_destination_);
        if (ret != RK_SUCCESS) {
            log_mpi_error("RK_MPI_SYS_Bind(VI,VPSS)", ret);
            return -1;
        }
        vi_vpss_bound_ = true;
        pipeline_initialized_ = true;

        std::fprintf(stdout,
                     "[rk_media] VI(%d,%d,%d) %dx%d NV12 -> VPSS(%d,%d) "
                     "%dx%d RGB888, queue=%d, timeout=%dms\n",
                     config_.vi_dev_id, config_.vi_pipe_id, config_.vi_channel_id,
                     config_.source_width, config_.source_height,
                     config_.vpss_group_id, config_.vpss_channel_id,
                     config_.output_width, config_.output_height,
                     config_.queue_capacity, config_.frame_timeout_ms);
        return 0;
    }

    void teardown_pipeline()
    {
        if (vi_vpss_bound_) {
            const int ret = RK_MPI_SYS_UnBind(&vi_source_, &vpss_destination_);
            if (ret != RK_SUCCESS) {
                log_mpi_error("RK_MPI_SYS_UnBind(VI,VPSS)", ret);
            }
            vi_vpss_bound_ = false;
        }
        if (vpss_channel_enabled_) {
            const int ret = RK_MPI_VPSS_DisableChn(config_.vpss_group_id,
                                                   config_.vpss_channel_id);
            if (ret != RK_SUCCESS) {
                log_mpi_error("RK_MPI_VPSS_DisableChn", ret);
            }
            vpss_channel_enabled_ = false;
        }
        if (vpss_group_started_) {
            const int ret = RK_MPI_VPSS_StopGrp(config_.vpss_group_id);
            if (ret != RK_SUCCESS) {
                log_mpi_error("RK_MPI_VPSS_StopGrp", ret);
            }
            vpss_group_started_ = false;
        }
        if (vpss_group_created_) {
            const int ret = RK_MPI_VPSS_DestroyGrp(config_.vpss_group_id);
            if (ret != RK_SUCCESS) {
                log_mpi_error("RK_MPI_VPSS_DestroyGrp", ret);
            }
            vpss_group_created_ = false;
        }
        if (vi_channel_enabled_) {
            const int ret = RK_MPI_VI_DisableChn(config_.vi_pipe_id,
                                                 config_.vi_channel_id);
            if (ret != RK_SUCCESS) {
                log_mpi_error("RK_MPI_VI_DisableChn", ret);
            }
            vi_channel_enabled_ = false;
        }
        if (owns_vi_device_) {
            const int ret = RK_MPI_VI_DisableDev(config_.vi_dev_id);
            if (ret != RK_SUCCESS) {
                log_mpi_error("RK_MPI_VI_DisableDev", ret);
            }
            owns_vi_device_ = false;
        }
        if (sys_initialized_) {
            const int ret = RK_MPI_SYS_Exit();
            if (ret != RK_SUCCESS) {
                log_mpi_error("RK_MPI_SYS_Exit", ret);
            }
            sys_initialized_ = false;
        }
        stop_isp();
        pipeline_initialized_ = false;
    }

    void producer_loop()
    {
        int consecutive_timeouts = 0;
        while (running_.load(std::memory_order_acquire)) {
            RkMediaFrame frame{};
            const int ret = RK_MPI_VPSS_GetChnFrame(
                config_.vpss_group_id, config_.vpss_channel_id, &frame.info,
                config_.frame_timeout_ms);

            if (ret != RK_SUCCESS) {
                {
                    std::lock_guard<std::mutex> lock(queue_mutex_);
                    ++stats_.get_timeouts;
                }
                ++consecutive_timeouts;
                if (consecutive_timeouts >= config_.timeout_recovery_threshold &&
                    running_.load(std::memory_order_acquire)) {
                    if (!recover_pipeline()) {
                        running_.store(false, std::memory_order_release);
                        queue_condition_.notify_all();
                        break;
                    }
                    consecutive_timeouts = 0;
                }
                continue;
            }

            consecutive_timeouts = 0;
            frame.acquired_at_us = monotonic_us();
            frame.valid = true;

            std::unique_lock<std::mutex> lock(queue_mutex_);
            ++outstanding_frames_;
            ++stats_.frames_acquired;

            if (!valid_frame(frame)) {
                ++stats_.invalid_frames;
                release_locked(&frame);
                continue;
            }

            if (queue_count_ == static_cast<size_t>(config_.queue_capacity)) {
                RkMediaFrame dropped = queue_[queue_head_];
                queue_[queue_head_] = {};
                queue_head_ = (queue_head_ + 1U) % queue_.size();
                --queue_count_;
                ++stats_.frames_dropped;
                release_locked(&dropped);
            }

            queue_[queue_tail_] = frame;
            queue_tail_ = (queue_tail_ + 1U) % queue_.size();
            ++queue_count_;
            stats_.max_queue_depth = std::max(
                stats_.max_queue_depth, static_cast<uint32_t>(queue_count_));
            lock.unlock();
            queue_condition_.notify_one();
        }
    }

    bool valid_frame(const RkMediaFrame &frame) const
    {
        const VIDEO_FRAME_S &video = frame.info.stVFrame;
        return video.pMbBlk != nullptr && video.enPixelFormat == RK_FMT_RGB888 &&
               video.u32Width == static_cast<RK_U32>(config_.output_width) &&
               video.u32Height == static_cast<RK_U32>(config_.output_height) &&
               video.u32VirWidth >= video.u32Width && video.u32VirHeight >= video.u32Height;
    }

    void release_locked(RkMediaFrame *frame)
    {
        if (frame == nullptr || !frame->valid) {
            return;
        }
        const int ret = RK_MPI_VPSS_ReleaseChnFrame(
            config_.vpss_group_id, config_.vpss_channel_id, &frame->info);
        frame->valid = false;
        if (outstanding_frames_ > 0) {
            --outstanding_frames_;
        }
        if (ret != RK_SUCCESS) {
            ++stats_.release_failures;
            log_mpi_error("RK_MPI_VPSS_ReleaseChnFrame", ret);
        }
        ownership_condition_.notify_all();
    }

    void drain_queue_locked()
    {
        while (queue_count_ > 0) {
            RkMediaFrame frame = queue_[queue_head_];
            queue_[queue_head_] = {};
            queue_head_ = (queue_head_ + 1U) % queue_.size();
            --queue_count_;
            release_locked(&frame);
        }
        queue_head_ = 0;
        queue_tail_ = 0;
    }

    bool recover_pipeline()
    {
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            ++stats_.recovery_attempts;
            recovering_ = true;
            drain_queue_locked();
            ownership_condition_.wait(lock, [this] {
                return outstanding_frames_ == 0 ||
                       !running_.load(std::memory_order_acquire);
            });
        }

        if (!running_.load(std::memory_order_acquire)) {
            return false;
        }

        std::fprintf(stderr,
                     "[rk_media] %d consecutive frame timeouts; rebuilding pipeline\n",
                     config_.timeout_recovery_threshold);
        teardown_pipeline();

        bool recovered = false;
        for (int attempt = 0;
             attempt < kRecoveryAttempts && running_.load(std::memory_order_acquire);
             ++attempt) {
            if (init_pipeline() == 0) {
                recovered = true;
                break;
            }
            teardown_pipeline();
            std::this_thread::sleep_for(std::chrono::milliseconds(kRecoveryBackoffMs));
        }

        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            recovering_ = false;
            if (recovered) {
                ++stats_.recovery_successes;
            }
        }
        queue_condition_.notify_all();
        return recovered;
    }

    RkMediaPipelineConfig config_;
    mutable std::mutex lifecycle_mutex_;
    mutable std::mutex queue_mutex_;
    std::condition_variable queue_condition_;
    std::condition_variable ownership_condition_;
    std::array<RkMediaFrame, kMaxQueueCapacity> queue_{};
    size_t queue_head_ = 0;
    size_t queue_tail_ = 0;
    size_t queue_count_ = 0;
    size_t outstanding_frames_ = 0;
    bool recovering_ = false;
    std::atomic<bool> running_{false};
    std::thread producer_;
    RkMediaPipelineStats stats_{};

    bool pipeline_initialized_ = false;
    bool sys_initialized_ = false;
    bool owns_vi_device_ = false;
    bool vi_channel_enabled_ = false;
    bool vpss_group_created_ = false;
    bool vpss_channel_enabled_ = false;
    bool vpss_group_started_ = false;
    bool vi_vpss_bound_ = false;
    MPP_CHN_S vi_source_{};
    MPP_CHN_S vpss_destination_{};

#if defined(DESKBOT_HAS_RKAIQ)
    rk_aiq_sys_ctx_t *aiq_context_ = nullptr;
    bool aiq_started_ = false;
#endif
};

RkMediaPipeline::RkMediaPipeline(RkMediaPipelineConfig config)
    : impl_(std::make_unique<Impl>(std::move(config)))
{
}

RkMediaPipeline::~RkMediaPipeline() = default;

int RkMediaPipeline::start()
{
    return impl_->start();
}

void RkMediaPipeline::stop()
{
    impl_->stop();
}

bool RkMediaPipeline::pop(RkMediaFrame *frame, int timeout_ms)
{
    return impl_->pop(frame, timeout_ms);
}

int RkMediaPipeline::release(RkMediaFrame *frame)
{
    return impl_->release(frame);
}

bool RkMediaPipeline::is_running() const
{
    return impl_->is_running();
}

void *RkMediaPipeline::virtual_address(const RkMediaFrame &frame) const
{
    return impl_->virtual_address(frame);
}

int RkMediaPipeline::dma_buf_fd(const RkMediaFrame &frame) const
{
    return impl_->dma_buf_fd(frame);
}

size_t RkMediaPipeline::buffer_size(const RkMediaFrame &frame) const
{
    return impl_->buffer_size(frame);
}

int RkMediaPipeline::sync_for_cpu(const RkMediaFrame &frame) const
{
    return impl_->sync_for_cpu(frame);
}

RkMediaPipelineStats RkMediaPipeline::stats() const
{
    return impl_->stats();
}
