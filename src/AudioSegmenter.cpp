#include "AudioSegmenter.h"
#include "WavEncoder.h"
#include <cmath>

AudioSegmenter::AudioSegmenter(int sampleRate)
    : sampleRate(sampleRate),
      preRollMs(400),
      silenceTimeoutMs(800),
      vadThreshold(0.02f),
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
    preRollMs = preRoll > 0 ? preRoll : 400;
    silenceTimeoutMs = silenceTimeout > 0 ? silenceTimeout : 800;
    vadThreshold = vad > 0.0f ? vad : 0.02f;
    maxPreRollSamples = static_cast<size_t>((sampleRate * preRollMs) / 1000);
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
            // Prepend all buffered pre-roll audio so initial consonants are intact
            currentSpeechSamples.insert(currentSpeechSamples.end(), preRollBuffer.begin(), preRollBuffer.end());
        }

        currentSpeechSamples.insert(currentSpeechSamples.end(), samples, samples + count);
        lastVoiceTimeMs = now;

        // Max speech length safeguard (10s)
        if (now - speechStartTimeMs > 10000) {
            finalizeSegment(now);
            isSpeaking = true;
            speechStartTimeMs = now;
            currentSpeechSamples.clear();
        }
    } else {
        if (isSpeaking) {
            currentSpeechSamples.insert(currentSpeechSamples.end(), samples, samples + count);
            if (now - lastVoiceTimeMs >= silenceTimeoutMs) {
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
    // Discard tiny micro clicks (< 300ms)
    size_t minSamples = static_cast<size_t>(sampleRate * 0.3);
    if (currentSpeechSamples.size() < minSamples) {
        currentSpeechSamples.clear();
        return;
    }

    int64_t relStartMs = recordingStartTimeMs > 0 ? (speechStartTimeMs - recordingStartTimeMs) : 0;
    int64_t relEndMs = recordingStartTimeMs > 0 ? (endTimeMs - recordingStartTimeMs) : (endTimeMs - speechStartTimeMs);
    if (relStartMs < 0) relStartMs = 0;
    if (relEndMs <= relStartMs) relEndMs = relStartMs + 1000;

    std::vector<uint8_t> wavData = WavEncoder::encode16BitMono(currentSpeechSamples.data(), currentSpeechSamples.size(), sampleRate);
    currentSpeechSamples.clear();

    if (callback) {
        callback(wavData, relStartMs, relEndMs);
    }
}

void AudioSegmenter::flush() {
    std::lock_guard<std::mutex> lock(mtx);
    if (isSpeaking && !currentSpeechSamples.empty()) {
        int64_t now = getCurrentTimeMs();
        finalizeSegment(now);
        isSpeaking = false;
    }
}

void AudioSegmenter::reset() {
    std::lock_guard<std::mutex> lock(mtx);
    isSpeaking = false;
    preRollBuffer.clear();
    currentSpeechSamples.clear();
}
