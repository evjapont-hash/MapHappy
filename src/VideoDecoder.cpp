#include "VideoDecoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/avutil.h>
}

bool VideoDecoder::open(const std::string& path, std::string& error) {
    close();

    if (avformat_open_input(&fmt_, path.c_str(), nullptr, nullptr) < 0) {
        error = "Could not open file";
        fmt_ = nullptr;
        return false;
    }
    if (avformat_find_stream_info(fmt_, nullptr) < 0) {
        error = "Could not read stream info";
        close();
        return false;
    }

    const AVCodec* decoder = nullptr;
    vstream_ = av_find_best_stream(fmt_, AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
    if (vstream_ < 0 || !decoder) {
        error = "No video stream found";
        close();
        return false;
    }

    AVStream* st = fmt_->streams[vstream_];
    codec_ = avcodec_alloc_context3(decoder);
    if (!codec_ || avcodec_parameters_to_context(codec_, st->codecpar) < 0) {
        error = "Could not set up decoder";
        close();
        return false;
    }
    codec_->thread_count = 0;          // let FFmpeg pick a sensible thread count
    if (avcodec_open2(codec_, decoder, nullptr) < 0) {
        error = "Unsupported video codec";
        close();
        return false;
    }

    width_  = codec_->width;
    height_ = codec_->height;
    AVRational fr = av_guess_frame_rate(fmt_, st, nullptr);
    fps_ = (fr.num > 0 && fr.den > 0) ? av_q2d(fr) : 30.0;
    startTime_ = (st->start_time != AV_NOPTS_VALUE) ? st->start_time * av_q2d(st->time_base) : 0.0;

    stop_ = false;
    finished_ = false;
    thread_ = std::thread(&VideoDecoder::run, this);
    return true;
}

void VideoDecoder::close() {
    stop_ = true;
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
    queue_.clear();

    if (codec_) avcodec_free_context(&codec_);
    if (fmt_)   avformat_close_input(&fmt_);
    vstream_ = -1;
    width_ = height_ = 0;
    finished_ = false;
}

bool VideoDecoder::getFrameFor(double clock, std::vector<uint8_t>& out) {
    std::lock_guard<std::mutex> lock(mtx_);
    bool got = false;
    while (!queue_.empty() && queue_.front().pts <= clock) {
        out.swap(queue_.front().rgba);
        queue_.pop_front();
        got = true;
    }
    if (got) cv_.notify_one();
    return got;
}

void VideoDecoder::run() {
    AVPacket* pkt = av_packet_alloc();
    AVFrame*  frm = av_frame_alloc();
    SwsContext* sws = nullptr;

    const double tb = av_q2d(fmt_->streams[vstream_]->time_base);
    const double frameDur = 1.0 / fps_;
    double offset = 0.0;     // grows each loop so timestamps keep increasing forever
    double lastPts = 0.0;

    // Pulls every frame the decoder has ready, converts to RGBA and queues it.
    // Returns false if we were asked to stop.
    auto receiveAll = [&]() -> bool {
        while (!stop_) {
            if (avcodec_receive_frame(codec_, frm) < 0) return true;  // needs more input

            double pts = (frm->best_effort_timestamp == AV_NOPTS_VALUE)
                             ? lastPts + frameDur
                             : frm->best_effort_timestamp * tb - startTime_;
            lastPts = pts;

            Frame f;
            f.pts = pts + offset;
            f.rgba.resize(size_t(width_) * height_ * 4);

            sws = sws_getCachedContext(sws, frm->width, frm->height, (AVPixelFormat)frm->format,
                                       width_, height_, AV_PIX_FMT_RGBA,
                                       SWS_BILINEAR, nullptr, nullptr, nullptr);
            if (!sws) { av_frame_unref(frm); return false; }

            // A negative stride writing from the last row upward flips the image
            // vertically, which is the order OpenGL textures want.
            uint8_t* dst[4]   = { f.rgba.data() + size_t(height_ - 1) * width_ * 4, nullptr, nullptr, nullptr };
            int dstStride[4]  = { -width_ * 4, 0, 0, 0 };
            sws_scale(sws, frm->data, frm->linesize, 0, frm->height, dst, dstStride);
            av_frame_unref(frm);

            std::unique_lock<std::mutex> lock(mtx_);
            cv_.wait(lock, [&] { return queue_.size() < kMaxQueued || stop_; });
            if (stop_) return false;
            queue_.push_back(std::move(f));
        }
        return false;
    };

    while (!stop_) {
        int r = av_read_frame(fmt_, pkt);
        if (r >= 0) {
            if (pkt->stream_index == vstream_ && avcodec_send_packet(codec_, pkt) >= 0) {
                if (!receiveAll()) { av_packet_unref(pkt); break; }
            }
            av_packet_unref(pkt);
            continue;
        }

        // End of file (or a read error): flush frames still inside the decoder.
        avcodec_send_packet(codec_, nullptr);
        if (!receiveAll()) break;

        if (loop_ && r == AVERROR_EOF) {
            offset += lastPts + frameDur;
            lastPts = 0.0;
            av_seek_frame(fmt_, -1, 0, AVSEEK_FLAG_BACKWARD);
            avcodec_flush_buffers(codec_);
            continue;
        }
        finished_ = true;
        break;
    }

    sws_freeContext(sws);
    av_frame_free(&frm);
    av_packet_free(&pkt);
}
