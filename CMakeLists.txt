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

# 输出 OpenCV 信息
if (OpenCV_FOUND)
    message(STATUS "OpenCV found:")
    message(STATUS "  OpenCV_VERSION: ${OpenCV_VERSION}")
else ()
    message(WARNING "OpenCV not found")
endif ()

add_executable(GB28181Console
        main.cpp
        pugixml.cpp
        xml_builder.cpp
        sdp_parser.cpp
        sip_register.cpp
        detection_worker.cpp
        video_frame_encoder.cpp
        utils.cpp
        header_builder.cpp
        pcm_encoder.cpp
        ps_muxer.cpp
        rtp_sender.cpp
)

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
