#include "AudioCaptureSession.h"
#include <media-io/audio-io.h>
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
        // Fallback: Try "Mic/Aux" or default audio source
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

    // Lazy initialization of 16kHz mono resampler matching main OBS audio settings
    if (!resampler) {
        const struct audio_output_info *obs_audio = audio_output_get_info(obs_get_audio());
        if (obs_audio) {
            struct resample_info src_info;
            src_info.samples_per_sec = obs_audio->samples_per_sec;
            src_info.format = obs_audio->format;
            src_info.speakers = obs_audio->speakers;

            struct resample_info dst_info;
            dst_info.samples_per_sec = 16000;
            dst_info.format = AUDIO_FORMAT_16BIT;
            dst_info.speakers = SPEAKERS_MONO;

            resampler = audio_resampler_create(&dst_info, &src_info);
        }
    }

    if (resampler) {
        uint8_t *output[MAX_AV_PLANES];
        uint32_t out_frames;
        uint64_t ts_offset;

        if (audio_resampler_resample(resampler, output, &out_frames, &ts_offset, (const uint8_t *const *)data->data, data->frames)) {
            if (output[0] && out_frames > 0) {
                const int16_t *pcm16 = reinterpret_cast<const int16_t*>(output[0]);
                segmenter->processSamples(pcm16, out_frames);
            }
        }
    }
}
