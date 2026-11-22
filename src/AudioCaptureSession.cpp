#include "AudioCaptureSession.h"
#include <vector>

static void audio_capture_callback(void *param, obs_source_t *source, const struct audio_data *data, bool muted) {
    if (!param) return;
    AudioCaptureSession *session = static_cast<AudioCaptureSession*>(param);
    session->handleAudioData(data, muted);
}

AudioCaptureSession::AudioCaptureSession(AudioSegmenter *segmenter)
    : segmenter(segmenter),
      audioSource(nullptr),
      resampler(nullptr),
      active(false) {
}

AudioCaptureSession::~AudioCaptureSession() {
    stop();
}

bool AudioCaptureSession::start(const std::string &sourceName) {
    stop();

    audioSource = obs_get_source_by_name(sourceName.c_str());
    if (!audioSource) {
        // Fallback: Try "Mic/Aux" or first audio source
        audioSource = obs_get_source_by_name("Mic/Aux");
    }

    if (!audioSource) {
        return false;
    }

    currentSourceName = sourceName;
    obs_source_add_audio_capture_callback(audioSource, audio_capture_callback, this);
    active = true;
    return true;
}

void AudioCaptureSession::stop() {
    if (audioSource && active) {
        obs_source_remove_audio_capture_callback(audioSource, audio_capture_callback, this);
        obs_source_release(audioSource);
        audioSource = nullptr;
    }
    cleanupResampler();
    active = false;
}

bool AudioCaptureSession::isActive() const {
    return active;
}

void AudioCaptureSession::cleanupResampler() {
    if (resampler) {
        audio_resampler_destroy(resampler);
        resampler = nullptr;
    }
}

void AudioCaptureSession::handleAudioData(const struct audio_data *data, bool muted) {
    if (muted || !data || !segmenter) {
        return;
    }

    // Lazy initialization of 16kHz mono resampler
    if (!resampler && audioSource) {
        struct resample_info src_info;
        struct resample_info dst_info;

        src_info.samples_per_sec = obs_source_get_sample_rate(audioSource);
        src_info.format = AUDIO_FORMAT_16BIT;
        src_info.speakers = SPEAKERS_MONO;

        dst_info.samples_per_sec = 16000;
        dst_info.format = AUDIO_FORMAT_16BIT;
        dst_info.speakers = SPEAKERS_MONO;

        resampler = audio_resampler_create(&dst_info, &src_info);
    }

    if (resampler) {
        uint8_t *output[MAX_AV_PLANES];
        uint32_t out_frames;
        uint64_t ts_offset;

        if (audio_resampler_resample(resampler, output, &out_frames, &ts_offset, (const uint8_t *const *)data->data, data->frames)) {
            const int16_t *pcm16 = reinterpret_cast<const int16_t*>(output[0]);
            segmenter->processSamples(pcm16, out_frames);
        }
    } else {
        // Direct 16-bit mono passthrough if resampler wasn't needed
        const int16_t *pcm16 = reinterpret_cast<const int16_t*>(data->data[0]);
        segmenter->processSamples(pcm16, data->frames);
    }
}
