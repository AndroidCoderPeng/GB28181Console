//
// Created by pengx on 2026/1/7.
//

#ifndef FRAMEDETECTOR_PCM_ENCODER_HPP
#define FRAMEDETECTOR_PCM_ENCODER_HPP

#include <cstdint>

class PcmEncoder {
public:
    /**
     * @brief 将 PCM 数据编码为 A-law
     * @param pcm 输入 PCM 数据
     * @param alaw 输出 A-law 数据
     * @param samples 输入 PCM 数据的采样点数
     */
    static void encode_to_alaw(const int16_t *pcm, uint8_t *alaw, std::size_t samples);

    /**
     * @brief 将 PCM 数据编码为 μ-law
     * @param pcm 输入 PCM 数据
     * @param mulaw 输出 μ-law 数据
     * @param samples 输入 PCM 数据的采样点数
     */
    static void encode_to_mulaw(const int16_t *pcm, uint8_t *mulaw, std::size_t samples);
};


#endif //FRAMEDETECTOR_PCM_ENCODER_HPP
