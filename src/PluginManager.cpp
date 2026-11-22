#include "PluginManager.h"
#include "GroqWhisperClient.h"
#include <obs-frontend-api.h>
#include <regex>
#include <chrono>

PluginManager &PluginManager::instance() {
    static PluginManager mgr;
    return mgr;
}

PluginManager::PluginManager()
    : isCaptioning(false),
      isRecording(false),
      recordingStartTimeMs(0) {
    segmenter = std::make_unique<AudioSegmenter>(16000);
    audioCapture = std::make_unique<AudioCaptureSession>(segmenter.get());
    textWriter = std::make_unique<TextOutputWriter>();
    srtWriter = std::make_unique<SrtWriter>();
}

PluginManager::~PluginManager() {
    shutdown();
}

void PluginManager::initialize() {
    segmenter->setCallback([this](const std::vector<uint8_t> &wavData, int64_t startMs, int64_t endMs) {
        this->onSegmentReady(wavData, startMs, endMs);
    });
}

void PluginManager::shutdown() {
    stopCaptioning();
}

void PluginManager::updateSettings(const PluginSettings &newSettings) {
    std::lock_guard<std::mutex> lock(settingsMutex);
    settings = newSettings;

    segmenter->setParameters(settings.pre_roll_ms, settings.silence_threshold_ms, settings.vad_sensitivity);
    textWriter->setSourceName(settings.text_source_name);
    textWriter->setAutoClear(settings.auto_clear_text, settings.clear_text_timeout_ms);
    srtWriter->setMaxLineLength(settings.srt_max_line_length);

    if (isCaptioning.load()) {
        audioCapture->start(settings.audio_source_name);
    }
}

PluginSettings PluginManager::getSettings() {
    std::lock_guard<std::mutex> lock(settingsMutex);
    return settings;
}

bool PluginManager::startCaptioning() {
    PluginSettings s = getSettings();
    if (s.api_key.empty()) {
        textWriter->updateText("⚠️ [AI Captions] يرجى إدخال مفتاح Groq API Key أولاً");
        return false;
    }

    segmenter->reset();
    textWriter->setSourceName(s.text_source_name);
    bool ok = audioCapture->start(s.audio_source_name);
    if (ok) {
        isCaptioning = true;
    }
    return ok;
}

void PluginManager::stopCaptioning() {
    if (isCaptioning.load()) {
        audioCapture->stop();
        segmenter->flush();
        isCaptioning = false;
    }
}

bool PluginManager::isCaptioningActive() const {
    return isCaptioning.load();
}

void PluginManager::onRecordingStarted() {
    isRecording = true;
    auto now = std::chrono::steady_clock::now();
    recordingStartTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    segmenter->setRecordingStartTime(recordingStartTimeMs);
    srtWriter->reset();

    PluginSettings s = getSettings();
    if (s.sync_with_recording) {
        startCaptioning();
    }
}

void PluginManager::onRecordingStopped() {
    isRecording = false;
    segmenter->flush();

    // Retrieve the exact video file path from OBS
    const char *lastRec = obs_frontend_get_last_recording();
    if (lastRec && strlen(lastRec) > 0) {
        std::string videoPath(lastRec);
        // Replace extension with .srt
        size_t dotPos = videoPath.find_last_of('.');
        std::string srtPath = (dotPos != std::string::npos) ? (videoPath.substr(0, dotPos) + ".srt") : (videoPath + ".srt");

        if (srtWriter->getEntryCount() > 0) {
            srtWriter->saveToFile(srtPath);
        }
    }

    PluginSettings s = getSettings();
    if (s.sync_with_recording) {
        stopCaptioning();
    }
}

void PluginManager::onStreamingStarted() {
    PluginSettings s = getSettings();
    if (s.sync_with_streaming) {
        startCaptioning();
    }
}

void PluginManager::onStreamingStopped() {
    PluginSettings s = getSettings();
    if (s.sync_with_streaming) {
        stopCaptioning();
    }
}

void PluginManager::onSegmentReady(const std::vector<uint8_t> &wavData, int64_t startMs, int64_t endMs) {
    PluginSettings s = getSettings();
    if (s.api_key.empty()) return;

    GroqWhisperClient::transcribeAsync(
        wavData,
        startMs,
        endMs,
        s.api_key,
        s.model,
        s.language,
        s.custom_keywords,
        [this](bool success, const std::string &text, int64_t sMs, int64_t eMs) {
            this->onTranscriptionResult(success, text, sMs, eMs);
        }
    );
}

bool PluginManager::isHallucination(const std::string &text) {
    if (text.length() < 2) return true;

    static const std::vector<std::string> bannedPhrases = {
        "اشترك في القناة", "شكرا للمشاهدة", "شكراً للمشاهدة",
        "تفعيل الجرس", "لا تنسوا الاشتراك", "thank you for watching",
        "thanks for watching", "subtitles by"
    };

    for (const auto &phrase : bannedPhrases) {
        if (text.find(phrase) != std::string::npos) return true;
    }
    return false;
}

void PluginManager::onTranscriptionResult(bool success, const std::string &text, int64_t startMs, int64_t endMs) {
    if (!success || text.empty() || isHallucination(text)) {
        return;
    }

    PluginSettings s = getSettings();

    // 1. Live Text Source update in OBS scene
    if (s.update_text_source) {
        textWriter->updateText(text);
    }

    // 2. Append to SRT transcript
    if (s.srt_enabled) {
        srtWriter->addEntry(startMs, endMs, text);
    }
}

void PluginManager::loadSettings(obs_data_t *data) {
    if (!data) return;
    obs_data_t *obj = obs_data_get_obj(data, "obs_ai_captions");
    if (obj) {
        PluginSettings s;
        s.load(obj);
        updateSettings(s);
        obs_data_release(obj);
    }
}

void PluginManager::saveSettings(obs_data_t *data) {
    if (!data) return;
    obs_data_t *obj = obs_data_create();
    PluginSettings s = getSettings();
    s.save(obj);
    obs_data_set_obj(data, "obs_ai_captions", obj);
    obs_data_release(obj);
}
