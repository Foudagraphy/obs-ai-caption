#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <functional>

class GeminiClient {
public:
    using TranscriptionCallback = std::function<void(bool success, const std::string &text, int64_t startMs, int64_t endMs)>;

    static std::string transcribe(
        const std::vector<uint8_t> &wavData,
        const std::string &apiKey,
        const std::string &model,
        const std::string &customKeywords,
        std::string *outError = nullptr
    );

    static void transcribeAsync(
        const std::vector<uint8_t> &wavData,
        int64_t startMs,
        int64_t endMs,
        const std::string &apiKey,
        const std::string &model,
        const std::string &customKeywords,
        TranscriptionCallback callback
    );

private:
    static std::string base64Encode(const uint8_t *data, size_t length);
    static std::string extractCandidateText(const std::string &json);
    static std::string escapeJson(const std::string &s);
};
