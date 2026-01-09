//
// Created by pengx on 2026/1/7.
//

#include "pcm_encoder.hpp"

static uint8_t linear_to_alaw(const int16_t sample) {
    static const int seg_end[8] = {0xFF, 0x1FF, 0x3FF, 0x7FF, 0xFFF, 0x1FFF, 0x3FFF, 0x7FFF};

    const int sign = sample >> 8 & 0x80;
    int temp_sample = sample; // 提升为 int 避免溢出
    if (sign) temp_sample = -temp_sample;
    if (temp_sample > 32635) temp_sample = 32635;

    int seg = 0;
    for (int i = 0; i < 8; i++) {
        if (sample <= seg_end[i]) {
            seg = i;
            break;
        }
    }

    // 计算量化子段内值
    const uint8_t quant = sample >> (seg == 0 ? 4 : seg + 3) & 0x0F;
    return ~(sign | seg << 4 | quant);
}

static uint8_t linear_to_mulaw(const int16_t sample) {
    static const int BIAS = 0x84;
    static const int CLIP = 32635;

    const int sign = sample >> 8 & 0x80;
    int temp_sample = sample;
    if (sign) temp_sample = -temp_sample;
    if (temp_sample > CLIP) temp_sample = CLIP;

    temp_sample += BIAS;

    int exponent = 7;
    for (int mask = 0x4000; (temp_sample & mask) == 0 && exponent > 0; mask >>= 1) {
        exponent--;
    }

    const int mantissa = temp_sample >> (exponent + 3) & 0x0F;
    return ~(sign | exponent << 4 | mantissa);
}

void PcmEncoder::encode_to_alaw(const int16_t *pcm, uint8_t *alaw, const std::size_t samples) {
    for (std::size_t i = 0; i < samples; ++i) {
        alaw[i] = linear_to_alaw(pcm[i]);
    }
}

void PcmEncoder::encode_to_mulaw(const int16_t *pcm, uint8_t *mulaw, const std::size_t samples) {
    for (std::size_t i = 0; i < samples; ++i) {
        mulaw[i] = linear_to_mulaw(pcm[i]);
    }
}
