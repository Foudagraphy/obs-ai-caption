#pragma once

#include <vector>
#include <deque>
#include <cstdint>
#include <functional>
#include <mutex>
#include <chrono>

class AudioSegmenter {
public:
    using SegmentCallback = std::function<void(const std::vector<uint8_t> &wavData, int64_t startMs, int64_t endMs)>;

    AudioSegmenter(int sampleRate = 16000);
    ~AudioSegmenter();

    void setParameters(int preRollMs, int silenceTimeoutMs, float vadThreshold);
    void setCallback(SegmentCallback callback);
    void setRecordingStartTime(int64_t startTimeMs);

    void processSamples(const int16_t *samples, size_t count);
    void flush();
    void reset();

private:
    int sampleRate;
    int preRollMs;
    int silenceTimeoutMs;
    float vadThreshold;

    bool isSpeaking;
    int64_t recordingStartTimeMs;
    int64_t speechStartTimeMs;
    int64_t lastVoiceTimeMs;

    std::deque<int16_t> preRollBuffer;
    size_t maxPreRollSamples;
    std::vector<int16_t> currentSpeechSamples;

    SegmentCallback callback;
    std::mutex mtx;

    float calculateRms(const int16_t *samples, size_t count);
    int64_t getCurrentTimeMs();
    void finalizeSegment(int64_t endTimeMs);
};
