#pragma once

#include <vector>
#include <cstdint>
#include <cstring>

class WavEncoder {
public:
    static std::vector<uint8_t> encode16BitMono(const int16_t *samples, size_t sampleCount, uint32_t sampleRate = 16000) {
        uint32_t dataSize = static_cast<uint32_t>(sampleCount * sizeof(int16_t));
        uint32_t totalFileSize = 36 + dataSize;

        std::vector<uint8_t> wav;
        wav.resize(44 + dataSize);
        uint8_t *p = wav.data();

        // 1. "RIFF" chunk descriptor
        std::memcpy(p, "RIFF", 4);
        writeUint32(p + 4, totalFileSize);
        std::memcpy(p + 8, "WAVE", 4);

        // 2. "fmt " sub-chunk
        std::memcpy(p + 12, "fmt ", 4);
        writeUint32(p + 16, 16);               // Subchunk1Size (16 for PCM)
        writeUint16(p + 20, 1);                // AudioFormat (1 for PCM)
        writeUint16(p + 22, 1);                // NumChannels (1 mono)
        writeUint32(p + 24, sampleRate);       // SampleRate (16000)
        writeUint32(p + 28, sampleRate * 2);   // ByteRate (SampleRate * 1 * 16 / 8)
        writeUint16(p + 32, 2);                // BlockAlign (1 * 16 / 8)
        writeUint16(p + 34, 16);               // BitsPerSample

        // 3. "data" sub-chunk
        std::memcpy(p + 36, "data", 4);
        writeUint32(p + 40, dataSize);

        // Copy raw PCM samples
        if (dataSize > 0) {
            std::memcpy(p + 44, samples, dataSize);
        }

        return wav;
    }

private:
    static void writeUint32(uint8_t *p, uint32_t val) {
        p[0] = static_cast<uint8_t>(val & 0xFF);
        p[1] = static_cast<uint8_t>((val >> 8) & 0xFF);
        p[2] = static_cast<uint8_t>((val >> 16) & 0xFF);
        p[3] = static_cast<uint8_t>((val >> 24) & 0xFF);
    }

    static void writeUint16(uint8_t *p, uint16_t val) {
        p[0] = static_cast<uint8_t>(val & 0xFF);
        p[1] = static_cast<uint8_t>((val >> 8) & 0xFF);
    }
};
