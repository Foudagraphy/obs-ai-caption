#pragma once

#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <obs.h>
#include <media-io/audio-resampler.h>
#include "AudioSegmenter.h"

class AudioCaptureSession {
public:
    AudioCaptureSession(AudioSegmenter *segmenter);
    ~AudioCaptureSession();

    bool start(const std::string &sourceName);
    void stop();
    bool isActive() const;

    void handleAudioData(const struct audio_data *data, bool muted);

    // Quick Test capture
    void startTestCapture(
        const std::string &sourceName,
        int durationSec,
        std::function<void(const std::vector<int16_t> &samples)> onFinished
    );

private:
    AudioSegmenter *segmenter;
    obs_source_t *audioSource;
    audio_resampler_t *resampler;
    std::string currentSourceName;
    bool active;
    bool firstFrameLogged;

    // Quick test state
    bool isTesting;
    size_t testTargetSamples;
    std::vector<int16_t> testBuffer;
    std::function<void(const std::vector<int16_t>&)> testCallback;
    std::mutex testMtx;

    void cleanupResampler();
};
