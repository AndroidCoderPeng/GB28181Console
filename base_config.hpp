//
// Created by pengx on 2025/9/26.
//

#ifndef GB28181_BASE_CONFIG_HPP
#define GB28181_BASE_CONFIG_HPP

#define VIDEO_WIDTH 640  // 视频画面宽度
#define VIDEO_HEIGHT 360 // 视频画面高度
#define VIDEO_FPS 25 // 视频帧率
#define VIDEO_BIT_RATE 1500000 // 视频比特率 480P: 1-2 Mbps, 720P: 2-3 Mbps, 1080P: 3-6 Mbps

#define AUDIO_SAMPLE_RATE 8000
#define AUDIO_CHANNEL 1
#define AUDIO_BUFFER_SIZE 1024

#define REGISTER_EXPIRED_TIME 7200 // 注册有效期
#define HEARTBEAT_INTERVAL 30 // 心跳间隔

#define STREAM_TYPE_H264 0x1B // 视频Stream Type
#define STREAM_TYPE_SVAC_VIDEO 0x80 // 视频Stream Type

#define STREAM_TYPE_G711 0x91 // 音频Stream Type
#define STREAM_TYPE_SVAC_AUDIO 0x9B // 音频Stream Type

#define VIDEO_STREAM_ID 0xE0 // 视频Stream ID
#define AUDIO_STREAM_ID 0xBD // 音频Stream ID（私有流，常用于非 MPEG 音频，如 AC-3、DTS、G.711）

#define MAX_RTP_PAYLOAD 1400      // RTP 负载最大长度（PS 分片大小）
#define MAX_RTP_PACKET (12 + MAX_RTP_PAYLOAD)  // 完整 RTP 包最大长度（1412）

#endif //GB28181_BASE_CONFIG_HPP
