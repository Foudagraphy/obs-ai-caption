#include "AudioCaptureSession.h"
#include <media-io/audio-io.h>
#include <vector>
#include <cstring>
#include <obs.h>

static void audio_capture_callback(void *param, obs_source_t *source, const struct audio_data *data, bool muted) {
    if (!param) return;
    AudioCaptureSession *session = static_cast<AudioCaptureSession*>(param);
    session->handleAudioData(data, muted);
}

AudioCaptureSession::AudioCaptureSession(AudioSegmenter *segmenter)
    : segmenter(segmenter),
      audioSource(nullptr),
      resampler(nullptr),
      active(false),
      firstFrameLogged(false),
      isTesting(false),
      testTargetSamples(0) {
}

AudioCaptureSession::~AudioCaptureSession() {
    stop();
}

bool AudioCaptureSession::start(const std::string &sourceName) {
    if (active && audioSource && currentSourceName == sourceName) {
        return true;
    }

    stop();

    audioSource = obs_get_source_by_name(sourceName.c_str());
    if (!audioSource) {
        // Fallback 1: Try "Mic/Aux"
        audioSource = obs_get_source_by_name("Mic/Aux");
    }
    if (!audioSource) {
        // Fallback 2: Enumerate first audio input source
        struct EnumData {
            obs_source_t *found = nullptr;
        } ed;
        obs_enum_sources([](void *param, obs_source_t *src) -> bool {
            EnumData *d = static_cast<EnumData*>(param);
            uint32_t flags = obs_source_get_output_flags(src);
            if ((flags & OBS_SOURCE_AUDIO) != 0 && (flags & OBS_SOURCE_DO_NOT_DUPLICATE) == 0) {
                d->found = obs_source_get_ref(src);
                return false; // Stop enumeration
            }
            return true;
        }, &ed);
        audioSource = ed.found;
    }

    if (!audioSource) {
        blog(LOG_ERROR, "[AI Captions] AudioCaptureSession::start - Could not find audio source '%s' or any fallback mic!",
             sourceName.c_str());
        return false;
    }

    currentSourceName = obs_source_get_name(audioSource);
    firstFrameLogged = false;
    obs_source_add_audio_capture_callback(audioSource, audio_capture_callback, this);
    active = true;
    blog(LOG_INFO, "[AI Captions] AudioCaptureSession attached successfully to '%s'", currentSourceName.c_str());
    return true;
}

void AudioCaptureSession::stop() {
    if (audioSource && active) {
        obs_source_remove_audio_capture_callback(audioSource, audio_capture_callback, this);
        obs_source_release(audioSource);
        audioSource = nullptr;
        blog(LOG_INFO, "[AI Captions] AudioCaptureSession stopped on '%s'", currentSourceName.c_str());
    }
    cleanupResampler();
    active = false;
    firstFrameLogged = false;
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
    if (muted || !data) {
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
            blog(LOG_INFO, "[AI Captions] Resampler created: %u Hz (format: %d, speakers: %d) -> 16000 Hz Mono 16-bit",
                 obs_audio->samples_per_sec, (int)obs_audio->format, (int)obs_audio->speakers);
        }
    }

    if (resampler) {
        uint8_t *output[MAX_AV_PLANES];
        std::memset(output, 0, sizeof(output));
        uint32_t out_frames = 0;
        uint64_t ts_offset = 0;

        if (audio_resampler_resample(resampler, output, &out_frames, &ts_offset,
                                     (const uint8_t *const *)data->data, data->frames)) {
            if (output[0] && out_frames > 0) {
                if (!firstFrameLogged) {
                    firstFrameLogged = true;
                    blog(LOG_INFO, "[AI Captions] Audio streaming active! First resampled block received: %u frames", out_frames);
                }

                const int16_t *pcm16 = reinterpret_cast<const int16_t*>(output[0]);

                // 1. Process for regular segmenter
                if (active && segmenter) {
                    segmenter->processSamples(pcm16, out_frames);
                }

                // 2. Process for Quick Test capture if active
                std::function<void(const std::vector<int16_t>&)> cb = nullptr;
                std::vector<int16_t> completedBuf;
                {
                    std::lock_guard<std::mutex> lock(testMtx);
                    if (isTesting) {
                        testBuffer.insert(testBuffer.end(), pcm16, pcm16 + out_frames);
                        if (testBuffer.size() >= testTargetSamples) {
                            isTesting = false;
                            completedBuf = std::move(testBuffer);
                            cb = testCallback;
                            testCallback = nullptr;
                        }
                    }
                }

                if (cb) {
                    blog(LOG_INFO, "[AI Captions] Quick Test capture complete (%zu samples)", completedBuf.size());
                    cb(completedBuf);
                }
            }
        }
    }
}

void AudioCaptureSession::startTestCapture(
    const std::string &sourceName,
    int durationSec,
    std::function<void(const std::vector<int16_t> &samples)> onFinished
) {
    {
        std::lock_guard<std::mutex> lock(testMtx);
        testTargetSamples = static_cast<size_t>(16000 * durationSec);
        testBuffer.clear();
        testCallback = onFinished;
        isTesting = true;
    }

    // If not currently capturing, start capture on the requested source
    if (!active) {
        start(sourceName);
    }
}
