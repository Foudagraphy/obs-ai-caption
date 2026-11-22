#pragma once

#include <string>
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

private:
    AudioSegmenter *segmenter;
    obs_source_t *audioSource;
    audio_resampler_t *resampler;
    std::string currentSourceName;
    bool active;

    void cleanupResampler();
};
