#include "AudioSegmenter.h"
#include "WavEncoder.h"
#include <cmath>
#include <obs.h>

AudioSegmenter::AudioSegmenter(int sampleRate)
    : sampleRate(sampleRate),
      preRollMs(350),
      silenceTimeoutMs(600),
      vadThreshold(0.010f),
      isSpeaking(false),
      recordingStartTimeMs(0),
      speechStartTimeMs(0),
      lastVoiceTimeMs(0) {
    maxPreRollSamples = static_cast<size_t>((sampleRate * preRollMs) / 1000);
}

AudioSegmenter::~AudioSegmenter() {
}

void AudioSegmenter::setParameters(int preRoll, int silenceTimeout, float vad) {
    std::lock_guard<std::mutex> lock(mtx);
    preRollMs = preRoll > 0 ? preRoll : 350;
    silenceTimeoutMs = silenceTimeout > 0 ? silenceTimeout : 750;
    vadThreshold = vad > 0.0f ? vad : 0.010f;
    maxPreRollSamples = static_cast<size_t>((sampleRate * preRollMs) / 1000);
    blog(LOG_INFO, "[AI Captions] VAD parameters: preRoll=%dms, silence=%dms, threshold=%.4f",
         preRollMs, silenceTimeoutMs, vadThreshold);
}

void AudioSegmenter::setCallback(SegmentCallback cb) {
    std::lock_guard<std::mutex> lock(mtx);
    callback = cb;
}

void AudioSegmenter::setRecordingStartTime(int64_t startTimeMs) {
    std::lock_guard<std::mutex> lock(mtx);
    recordingStartTimeMs = startTimeMs;
}

int64_t AudioSegmenter::getCurrentTimeMs() {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
}

float AudioSegmenter::calculateRms(const int16_t *samples, size_t count) {
    if (count == 0) return 0.0f;
    double sum = 0.0;
    for (size_t i = 0; i < count; ++i) {
        double val = samples[i] / 32768.0;
        sum += val * val;
    }
    return static_cast<float>(std::sqrt(sum / count));
}

void AudioSegmenter::processSamples(const int16_t *samples, size_t count) {
    std::lock_guard<std::mutex> lock(mtx);
    if (count == 0) return;

    float rms = calculateRms(samples, count);
    int64_t now = getCurrentTimeMs();

    if (rms >= vadThreshold) {
        if (!isSpeaking) {
            isSpeaking = true;
            speechStartTimeMs = now - preRollMs;
            currentSpeechSamples.clear();
            // Prepend buffered pre-roll audio so the very start of the first word is intact
            currentSpeechSamples.insert(currentSpeechSamples.end(), preRollBuffer.begin(), preRollBuffer.end());
            blog(LOG_INFO, "[AI Captions] Speech START detected (RMS: %.4f >= %.4f)", rms, vadThreshold);
        }

        currentSpeechSamples.insert(currentSpeechSamples.end(), samples, samples + count);
        lastVoiceTimeMs = now;

        // Continuous speech limit: 7.0 seconds to keep full phrases intact without cutting words
        if (now - speechStartTimeMs > 7000) {
            blog(LOG_INFO, "[AI Captions] Continuous phrase limit reached (7.0s) - finalizing segment cleanly");
            finalizeSegment(now);
            isSpeaking = true;
            speechStartTimeMs = now;
            currentSpeechSamples.clear();
        }
    } else {
        if (isSpeaking) {
            currentSpeechSamples.insert(currentSpeechSamples.end(), samples, samples + count);
            if (now - lastVoiceTimeMs >= silenceTimeoutMs) {
                blog(LOG_INFO, "[AI Captions] Speech PAUSE detected (%d ms silence) - finalizing phrase", silenceTimeoutMs);
                isSpeaking = false;
                finalizeSegment(now);
            }
        }

        // Maintain circular pre-roll buffer during silence
        for (size_t i = 0; i < count; ++i) {
            preRollBuffer.push_back(samples[i]);
            if (preRollBuffer.size() > maxPreRollSamples) {
                preRollBuffer.pop_front();
            }
        }
    }
}

void AudioSegmenter::finalizeSegment(int64_t endTimeMs) {
    // Discard tiny micro clicks / taps (< 600ms)
    size_t minSamples = static_cast<size_t>(sampleRate * 0.60);
    if (currentSpeechSamples.size() < minSamples) {
        currentSpeechSamples.clear();
        return;
    }

    // Energy Gate: Ensure the segment contains actual speech energy, not just quiet breathing/noise
    float avgRms = calculateRms(currentSpeechSamples.data(), currentSpeechSamples.size());
    float minEnergyRequired = vadThreshold * 0.70f;
    if (avgRms < minEnergyRequired) {
        blog(LOG_INFO, "[AI Captions] Discarding silent/breathing segment (avg RMS: %.4f < %.4f, duration: %.2fs)",
             avgRms, minEnergyRequired, static_cast<double>(currentSpeechSamples.size()) / sampleRate);
        currentSpeechSamples.clear();
        return;
    }

    int64_t relStartMs = recordingStartTimeMs > 0 ? (speechStartTimeMs - recordingStartTimeMs) : 0;
    int64_t relEndMs = recordingStartTimeMs > 0 ? (endTimeMs - recordingStartTimeMs) : (endTimeMs - speechStartTimeMs);
    if (relStartMs < 0) relStartMs = 0;
    if (relEndMs <= relStartMs) relEndMs = relStartMs + 1000;

    blog(LOG_INFO, "[AI Captions] Segment ready: %zu samples (%.2f s, avg RMS: %.4f), range: %lld - %lld ms",
         currentSpeechSamples.size(), static_cast<double>(currentSpeechSamples.size()) / sampleRate,
         avgRms, relStartMs, relEndMs);

    std::vector<uint8_t> wavData = WavEncoder::encode16BitMono(currentSpeechSamples.data(), currentSpeechSamples.size(), sampleRate);
    currentSpeechSamples.clear();

    if (callback) {
        callback(wavData, relStartMs, relEndMs);
    }
}

void AudioSegmenter::flush() {
    std::lock_guard<std::mutex> lock(mtx);
    int64_t now = getCurrentTimeMs();
    if (!currentSpeechSamples.empty()) {
        float avgRms = calculateRms(currentSpeechSamples.data(), currentSpeechSamples.size());
        if (avgRms >= vadThreshold * 0.70f && currentSpeechSamples.size() >= static_cast<size_t>(sampleRate * 0.50)) {
            blog(LOG_INFO, "[AI Captions] AudioSegmenter flush: finalizing %zu buffered speech samples (RMS: %.4f)",
                 currentSpeechSamples.size(), avgRms);
            finalizeSegment(now);
        } else {
            blog(LOG_INFO, "[AI Captions] AudioSegmenter flush: discarding trailing low-energy samples (%zu samples, RMS: %.4f)",
                 currentSpeechSamples.size(), avgRms);
            currentSpeechSamples.clear();
        }
        isSpeaking = false;
    }
}

void AudioSegmenter::reset() {
    std::lock_guard<std::mutex> lock(mtx);
    isSpeaking = false;
    preRollBuffer.clear();
    currentSpeechSamples.clear();
}
