cmake_minimum_required(VERSION 3.16)
project(GB28181Console)

set(CMAKE_CXX_STANDARD 14)

find_package(PkgConfig REQUIRED)

# 查找 osip2 库
find_library(OSIP2_LIBRARY
        NAMES osip2
        HINTS /usr/local/lib
        /usr/lib
)

# 查找 osipparser2 库
find_library(OSIPPARSER2_LIBRARY
        NAMES osipparser2
        HINTS /usr/local/lib
        /usr/lib
)

# 查找 eXosip2 库
find_library(EX_OSIP2_LIBRARY
        NAMES eXosip2
        HINTS /usr/local/lib
        /usr/lib
)

# 查找 FFmpeg 组件
set(FFMPEG_LIBRARIES
        libavformat
        libavcodec
        libavutil
        libavdevice
        libavfilter
        libswscale
        libswresample
)

# 遍历模块并查找
foreach (lib ${FFMPEG_LIBRARIES})
    pkg_check_modules(${lib} REQUIRED IMPORTED_TARGET ${lib})
endforeach ()

# 查找 OpenCV
find_package(OpenCV REQUIRED)

# 基础源文件
set(BASE_SOURCES
        main.cpp
        pugixml.cpp
        xml_builder.cpp
        sdp_parser.cpp
        rtp_sender.cpp
        utils.cpp
#        stream_worker.cpp
        logger.cpp
)

# SIP模块源文件
set(SIP_SOURCES
        sip/sip_manager.cpp
        sip/sip_context.cpp
        sip/register_manager.cpp
        sip/heartbeat_manager.cpp
        sip/event_dispatcher.cpp
        sip/stream_manager.cpp
        sip/response_sender.cpp
)

# 视频模块源文件
set(VIDEO_SOURCES
        video/frame_capture.cpp
        video/frame_encoder.cpp
        video/h264_splitter.cpp
        video/header_builder.cpp
        video/ps_muxer.cpp
)

# 音频模块源文件
set(AUDIO_SOURCES
        audio/audio_processor.cpp
        audio/audio_receiver.cpp
        audio/ring_buffer.cpp
)

# 合并所有源文件
set(ALL_SOURCES ${BASE_SOURCES} ${SIP_SOURCES} ${VIDEO_SOURCES} ${AUDIO_SOURCES})

# 创建单一的目标
add_executable(${CMAKE_PROJECT_NAME} ${ALL_SOURCES})

target_link_libraries(GB28181Console
        #        sip
        ${OSIP2_LIBRARY}
        ${OSIPPARSER2_LIBRARY}
        ${EX_OSIP2_LIBRARY}

        pthread

        #        ffmpeg
        PkgConfig::libavformat
        PkgConfig::libavcodec
        PkgConfig::libavutil
        PkgConfig::libavdevice
        PkgConfig::libavfilter
        PkgConfig::libswscale
        PkgConfig::libswresample

        #        opencv
        ${OpenCV_LIBS}
)

# 包含头文件路径
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
        /usr/local/include
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/sip
        ${CMAKE_CURRENT_SOURCE_DIR}/video
        ${CMAKE_CURRENT_SOURCE_DIR}/audio)