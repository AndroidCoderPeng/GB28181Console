//
// Created by pengx on 2026/2/10.
//

#ifndef GB28181CONSOLE_AUDIO_PROCESSOR_HPP
#define GB28181CONSOLE_AUDIO_PROCESSOR_HPP

#include <cstddef>
#include <cstdint>

class AudioProcessor {
public:
    /**
     * @brief 将 PCM 数据编码为 μ-law。16-bit PCM (-32768 ~ 32767) → μ-law 编码 → 8-bit (0 ~ 255)
     * @param input 输入 PCM 数据
     * @param output 输出 μ-law 数据
     * @param samples 输入 PCM 数据的采样点数
     */
    static void pcm_to_ulaw(const int16_t* input, uint8_t* output, size_t samples);

    /**
     * @brief 将 μ-law 数据解码为 PCM
     * @param input 输入 μ-law 数据
     * @param output 输出 PCM 数据
     * @param samples 输入 μ-law 数据的采样点数
     */
    static void ulaw_to_pcm(const uint8_t* input, int16_t* output, size_t samples);

    /**
     * @brief 将 PCM 数据编码为 A-law
     * @param input 输入 PCM 数据
     * @param output 输出 A-law 数据
     * @param samples 输入 PCM 数据的采样点数
     */
    static void pcm_to_alaw(const int16_t* input, uint8_t* output, size_t samples);

    /**
     * @brief 将 A-law 数据解码为 PCM
     * @param input 输入 A-law 数据
     * @param output 输出 PCM 数据
     * @param samples 输入 A-law 数据的采样点数
     */
    static void alaw_to_pcm(const uint8_t* input, int16_t* output, size_t samples);
};


#endif //GB28181CONSOLE_AUDIO_PROCESSOR_HPP
