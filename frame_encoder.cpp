//
// Created by peng on 2026/1/20.
//

#include "frame_encoder.hpp"
#include "base_config.hpp"
#include "ps_muxer.hpp"

#include <iomanip>
#include <iostream>
#include <opencv2/imgproc.hpp>

extern "C" {
#include <libavutil/avutil.h>
#include <libavutil/frame.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

bool FrameEncoder::prepare()
{
    // 查找H.264编码器
    _av_codec_ptr = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!_av_codec_ptr)
    {
        std::cerr << "找不到 H.264 编码器";
        return false;
    }

    // 分配编码器上下文
    if (_av_codec_ctx_ptr)
    {
        avcodec_free_context(&_av_codec_ctx_ptr);
    }
    _av_codec_ctx_ptr = avcodec_alloc_context3(_av_codec_ptr);
    if (!_av_codec_ctx_ptr)
    {
        std::cerr << "无法分配编码器上下文";
        return false;
    }

    // 配置编码参数
    _av_codec_ctx_ptr->pix_fmt = AV_PIX_FMT_YUV420P;
    _av_codec_ctx_ptr->width = VIDEO_WIDTH;
    _av_codec_ctx_ptr->height = VIDEO_HEIGHT;
    _av_codec_ctx_ptr->bit_rate = VIDEO_BIT_RATE;
    _av_codec_ctx_ptr->time_base = {1, VIDEO_FPS};
    _av_codec_ctx_ptr->framerate = {VIDEO_FPS, 1};
    _av_codec_ctx_ptr->gop_size = VIDEO_FPS;
    _av_codec_ctx_ptr->max_b_frames = 0;

    // 打开编码器
    const int ret = avcodec_open2(_av_codec_ctx_ptr, _av_codec_ptr, nullptr);
    if (ret < 0)
    {
        char err_buf[256];
        av_strerror(ret, err_buf, sizeof(err_buf));
        std::cerr << "无法打开编码器: " << err_buf << std::endl;
        return false;
    }

    _frame_count = 0;
    return true;
}

/**
 * 编码帧
 *
 * @param frame
 */
void FrameEncoder::encodeFrame(const cv::Mat& frame)
{
    std::lock_guard<std::mutex> lock(_encoder_mutex);
    if (!_av_codec_ctx_ptr)
    {
        return;
    }

    const auto mat = frame.clone();

    // 创建AVFrame并填充数据
    AVFrame* frame_ptr = av_frame_alloc();
    if (!frame_ptr)
    {
        std::cerr << "无法分配AVFrame" << std::endl;
        return;
    }

    const auto pts_90kHz = av_rescale_q(_frame_count++, {1, VIDEO_FPS}, {1, 90000});

    // 设置帧参数
    frame_ptr->format = AV_PIX_FMT_BGR24; // OpenCV使用BGR格式
    frame_ptr->width = mat.cols;
    frame_ptr->height = mat.rows;
    frame_ptr->pts = pts_90kHz;

    // 为帧分配缓冲区
    int ret = av_frame_get_buffer(frame_ptr, 32);
    if (ret < 0)
    {
        char err_buf[256];
        av_strerror(ret, err_buf, sizeof(err_buf));
        std::cerr << "无法为AVFrame分配缓冲区: " << err_buf << std::endl;
        av_frame_free(&frame_ptr);
        return;
    }

    // 将OpenCV Mat数据复制到AVFrame
    for (int i = 0; i < mat.rows; ++i)
    {
        memcpy(frame_ptr->data[0] + i * frame_ptr->linesize[0],
               mat.ptr<uint8_t>(i),
               std::min(mat.cols * mat.channels(), frame_ptr->linesize[0]));
    }

    // 转换到编码器所需的YUV420P格式
    AVFrame* yuv_frame_ptr = av_frame_alloc();
    if (!yuv_frame_ptr)
    {
        std::cerr << "无法分配YUV帧" << std::endl;
        av_frame_free(&frame_ptr);
        return;
    }

    yuv_frame_ptr->format = _av_codec_ctx_ptr->pix_fmt;
    yuv_frame_ptr->width = _av_codec_ctx_ptr->width;
    yuv_frame_ptr->height = _av_codec_ctx_ptr->height;
    yuv_frame_ptr->pts = pts_90kHz;

    ret = av_frame_get_buffer(yuv_frame_ptr, 32);
    if (ret < 0)
    {
        char err_buf[256];
        av_strerror(ret, err_buf, sizeof(err_buf));
        std::cerr << "无法为YUV帧分配缓冲区: " << err_buf << std::endl;
        av_frame_free(&frame_ptr);
        av_frame_free(&yuv_frame_ptr);
        return;
    }

    // 创建SWS上下文进行格式转换
    SwsContext* sws_ctx_ptr = sws_getContext(mat.cols, mat.rows, AV_PIX_FMT_BGR24,
                                             _av_codec_ctx_ptr->width, _av_codec_ctx_ptr->height,
                                             _av_codec_ctx_ptr->pix_fmt,
                                             SWS_BILINEAR,
                                             nullptr, nullptr,
                                             nullptr);
    if (!sws_ctx_ptr)
    {
        std::cerr << "无法创建SWS转换上下文" << std::endl;
        av_frame_free(&frame_ptr);
        av_frame_free(&yuv_frame_ptr);
        return;
    }

    // 执行格式转换
    const uint8_t* src_slice[] = {frame_ptr->data[0], nullptr, nullptr};
    const int src_stride[] = {frame_ptr->linesize[0], 0, 0};
    sws_scale(sws_ctx_ptr,
              src_slice, src_stride, 0, mat.rows,
              yuv_frame_ptr->data, yuv_frame_ptr->linesize);

    // 释放转换上下文
    sws_freeContext(sws_ctx_ptr);

    // 发送帧到编码器
    ret = avcodec_send_frame(_av_codec_ctx_ptr, yuv_frame_ptr);
    if (ret < 0)
    {
        char err_buf[256];
        av_strerror(ret, err_buf, sizeof(err_buf));
        std::cerr << "发送帧到编码器失败: " << err_buf << std::endl;
        av_frame_free(&frame_ptr);
        av_frame_free(&yuv_frame_ptr);
        return;
    }

    // 接收编码后的包
    AVPacket* pkt_ptr = av_packet_alloc();
    if (!pkt_ptr)
    {
        std::cerr << "无法分配AVPacket" << std::endl;
        av_frame_free(&frame_ptr);
        av_frame_free(&yuv_frame_ptr);
        return;
    }

    // 接收并处理编码包
    while (true)
    {
        ret = avcodec_receive_packet(_av_codec_ctx_ptr, pkt_ptr);
        if (ret == AVERROR(EAGAIN))
        {
            // 等待更多帧输入，不是错误
            break;
        }
        if (ret == AVERROR_EOF)
        {
            // 编码结束
            break;
        }

        if (ret >= 0)
        {
            if (_is_push_stream)
            {
                PsMuxer::get()->writeVideoFrame(pkt_ptr->data, pkt_ptr->pts, pkt_ptr->size);
            }
            av_packet_unref(pkt_ptr);
        }
    }
    av_packet_free(&pkt_ptr);
    av_frame_free(&frame_ptr);
    av_frame_free(&yuv_frame_ptr);
}

void FrameEncoder::startStream()
{
    _is_push_stream = true;
    std::cout << "开始推流" << std::endl;
}

void FrameEncoder::stopStream()
{
    _is_push_stream = false;
    std::cout << "停止推流" << std::endl;
}

void FrameEncoder::stopEncode()
{
    std::lock_guard<std::mutex> lock(_encoder_mutex);
    // 刷新编码器
    if (_av_codec_ctx_ptr)
    {
        avcodec_send_frame(_av_codec_ctx_ptr, nullptr);
        AVPacket* pkt_ptr = av_packet_alloc();
        while (avcodec_receive_packet(_av_codec_ctx_ptr, pkt_ptr) == 0)
        {
            if (_is_push_stream)
            {
                PsMuxer::get()->writeVideoFrame(pkt_ptr->data, pkt_ptr->pts, pkt_ptr->size);
            }
            av_packet_unref(pkt_ptr);
        }
        av_packet_free(&pkt_ptr);
    }

    _frame_count = 0;
    std::cout << "编码完成" << std::endl;
}

FrameEncoder::~FrameEncoder()
{
    std::lock_guard<std::mutex> lock(_encoder_mutex);
    if (_av_codec_ctx_ptr)
    {
        avcodec_free_context(&_av_codec_ctx_ptr);
        _av_codec_ctx_ptr = nullptr;
    }

    PsMuxer::get()->release();
    std::cout << "FrameEncoder 释放完成" << std::endl;
}