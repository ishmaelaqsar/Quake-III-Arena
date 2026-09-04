#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "engine_fixture.hpp"
#include "q_shared.h"
#include "qcommon.h"
#include "../code/client/snd_local.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

TEST(Sound, AdpcmRoundTripStaysWithinRmsBound) {
    q3::test::EnsureEngineInitialised();

    constexpr int kSampleRate = 22050;
    constexpr int kNumSamples = 2205; // 0.1 s at 22050 Hz
    constexpr double kFrequency = 440.0;

    std::vector<short> original(kNumSamples);
    for (int i = 0; i < kNumSamples; ++i) {
        double t = static_cast<double>(i) / kSampleRate;
        original[i] = static_cast<short>(32000.0 * std::sin(2.0 * M_PI * kFrequency * t));
    }

    std::vector<char> encoded((kNumSamples / 2) + 4);
    adpcm_state enc_state{0, 0};
    S_AdpcmEncode(original.data(), encoded.data(), kNumSamples, &enc_state);

    std::vector<short> decoded(kNumSamples);
    adpcm_state dec_state{0, 0};
    S_AdpcmDecode(encoded.data(), decoded.data(), kNumSamples, &dec_state);

    double sum_sq = 0.0;
    for (int i = 0; i < kNumSamples; ++i) {
        double diff = original[i] - decoded[i];
        sum_sq += diff * diff;
    }
    double rms = std::sqrt(sum_sq / kNumSamples);

    // Calibrated RMS error for 4-bit IMA-ADPCM on 440 Hz full-scale sine is ~550
    EXPECT_LT(rms, 1500.0);
}

TEST(Sound, WaveletRoundTripStaysWithinRmsBound) {
    q3::test::EnsureEngineInitialised();
    SND_setup();

    constexpr int kSampleRate = 22050;
    constexpr int kNumSamples = 2048; // Must match wavelet chunk size
    constexpr double kFrequency = 440.0;

    std::vector<short> original(kNumSamples);
    for (int i = 0; i < kNumSamples; ++i) {
        double t = static_cast<double>(i) / kSampleRate;
        original[i] = static_cast<short>(4000.0 * std::sin(2.0 * M_PI * kFrequency * t));
    }

    sfx_t sfx{};
    sfx.soundLength = kNumSamples;
    sfx.soundData = nullptr;

    encodeWavelet(&sfx, original.data());
    ASSERT_NE(sfx.soundData, nullptr);

    std::vector<short> decoded(kNumSamples);
    decodeWavelet(sfx.soundData, decoded.data());

    double sum_sq = 0.0;
    for (int i = 0; i < kNumSamples; ++i) {
        double diff = original[i] - decoded[i];
        sum_sq += diff * diff;
    }
    double rms = std::sqrt(sum_sq / kNumSamples);

    // Calibrated RMS error for wavelet compression on 4000 amplitude sine wave is ~500
    EXPECT_LT(rms, 1500.0);
}

TEST(Sound, GetWavinfoParsesGeneratedRiff) {
    // Construct 44-byte standard RIFF header: 22050 Hz, 16-bit mono, 1000 samples
    uint8_t header[44];
    std::memcpy(&header[0], "RIFF", 4);
    uint32_t riff_size = 36 + 2000;
    std::memcpy(&header[4], &riff_size, 4);
    std::memcpy(&header[8], "WAVE", 4);
    std::memcpy(&header[12], "fmt ", 4);
    uint32_t fmt_size = 16;
    std::memcpy(&header[16], &fmt_size, 4);
    uint16_t format = 1; // PCM
    std::memcpy(&header[20], &format, 2);
    uint16_t channels = 1; // mono
    std::memcpy(&header[22], &channels, 2);
    uint32_t rate = 22050;
    std::memcpy(&header[24], &rate, 4);
    uint32_t byte_rate = 22050 * 2;
    std::memcpy(&header[28], &byte_rate, 4);
    uint16_t align = 2;
    std::memcpy(&header[32], &align, 2);
    uint16_t bits = 16;
    std::memcpy(&header[34], &bits, 2);
    std::memcpy(&header[36], "data", 4);
    uint32_t data_size = 2000;
    std::memcpy(&header[40], &data_size, 4);

    wavinfo_t info = GetWavinfo("test.wav", header, sizeof(header));
    EXPECT_EQ(info.format, 1);
    EXPECT_EQ(info.channels, 1);
    EXPECT_EQ(info.rate, 22050);
    EXPECT_EQ(info.width, 2);
    EXPECT_EQ(info.samples, 1000);
}

TEST(Sound, GetWavinfoRejectsTruncatedHeader) {
    uint8_t truncated[20] = {0};
    std::memcpy(&truncated[0], "RIFF", 4);
    std::memcpy(&truncated[8], "WAVE", 4);

    wavinfo_t info = GetWavinfo("truncated.wav", truncated, sizeof(truncated));
    EXPECT_EQ(info.samples, 0);
}
