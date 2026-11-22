#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <functional>

class GroqWhisperClient {
public:
    using TranscriptionCallback = std::function<void(bool success, const std::string &text, int64_t startMs, int64_t endMs)>;

    static std::string transcribe(
        const std::vector<uint8_t> &wavData,
        const std::string &apiKey,
        const std::string &model,
        const std::string &language,
        const std::string &customKeywords,
        std::string *outError = nullptr
    );

    // Asynchronous helper
    static void transcribeAsync(
        const std::vector<uint8_t> &wavData,
        int64_t startMs,
        int64_t endMs,
        const std::string &apiKey,
        const std::string &model,
        const std::string &language,
        const std::string &customKeywords,
        TranscriptionCallback callback
    );

private:
    static std::string extractJsonField(const std::string &json, const std::string &fieldName);
};
