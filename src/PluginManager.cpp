#include "PluginManager.h"
#include "GroqWhisperClient.h"
#include "GeminiClient.h"
#include "WavEncoder.h"
#include <obs-frontend-api.h>
#include <obs-module.h>
#include <sstream>
#include <regex>
#include <chrono>
#include <cmath>

PluginManager &PluginManager::instance() {
    static PluginManager mgr;
    return mgr;
}

PluginManager::PluginManager()
    : isCaptioning(false),
      isRecording(false),
      pendingTranscriptions(0),
      waitingForFinalSrt(false),
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

    loadFromConfigFile();
}

void PluginManager::shutdown() {
    stopCaptioning();
}

void PluginManager::updateSettings(const PluginSettings &newSettings) {
    {
        std::lock_guard<std::mutex> lock(settingsMutex);
        settings = newSettings;
    }

    segmenter->setParameters(settings.pre_roll_ms, settings.silence_threshold_ms, settings.vad_sensitivity);
    textWriter->setSourceName(settings.text_source_name);
    textWriter->setAutoClear(settings.auto_clear_text, settings.clear_text_timeout_ms);
    srtWriter->setMaxLineLength(settings.srt_max_line_length);

    saveToConfigFile();

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
    if (s.getActiveApiKey().empty()) {
        textWriter->setSourceName(s.text_source_name);
        std::string provName = (s.provider == "gemini") ? "Google Gemini" : "Groq";
        textWriter->updateText("⚠️ [AI Captions] يرجى إدخال مفتاح " + provName + " API في قائمة Tools");
        blog(LOG_WARNING, "[AI Captions] Cannot start captioning: API Key for %s is empty", s.provider.c_str());
        return false;
    }

    segmenter->reset();
    textWriter->setSourceName(s.text_source_name);
    bool ok = audioCapture->start(s.audio_source_name);
    if (ok) {
        isCaptioning = true;
        blog(LOG_INFO, "[AI Captions] Captioning session STARTED using provider: %s (Audio: '%s', Text: '%s')",
             s.provider.c_str(), s.audio_source_name.c_str(), s.text_source_name.c_str());
    } else {
        blog(LOG_ERROR, "[AI Captions] Failed to attach audio source '%s'", s.audio_source_name.c_str());
    }
    return ok;
}

void PluginManager::stopCaptioning() {
    if (isCaptioning.load()) {
        audioCapture->stop();
        segmenter->flush();
        isCaptioning = false;
        blog(LOG_INFO, "[AI Captions] Captioning session STOPPED");
    }
}

bool PluginManager::isCaptioningActive() const {
    return isCaptioning.load();
}

void PluginManager::onRecordingStarted() {
    isRecording = true;
    waitingForFinalSrt = false;
    pendingSrtPath.clear();
    lastTranscribedContext.clear();

    auto now = std::chrono::steady_clock::now();
    recordingStartTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    segmenter->setRecordingStartTime(recordingStartTimeMs);
    srtWriter->reset();

    blog(LOG_INFO, "[AI Captions] OBS Recording Started. Syncing captions...");
    PluginSettings s = getSettings();
    if (s.sync_with_recording) {
        startCaptioning();
    }
}

void PluginManager::onRecordingStopped() {
    isRecording = false;
    blog(LOG_INFO, "[AI Captions] OBS Recording Stopped. Flushing final audio segment...");

    // Retrieve the exact video file path from OBS
    const char *lastRec = obs_frontend_get_last_recording();
    if (lastRec && strlen(lastRec) > 0) {
        std::string videoPath(lastRec);
        size_t dotPos = videoPath.find_last_of('.');
        pendingSrtPath = (dotPos != std::string::npos) ? (videoPath.substr(0, dotPos) + ".srt") : (videoPath + ".srt");
        blog(LOG_INFO, "[AI Captions] Target SRT file path: '%s'", pendingSrtPath.c_str());
    }

    // Flush any pending speech in the segmenter
    segmenter->flush();

    PluginSettings s = getSettings();
    if (s.sync_with_recording) {
        if (pendingTranscriptions.load() == 0) {
            if (!pendingSrtPath.empty() && srtWriter->getEntryCount() > 0) {
                srtWriter->saveToFile(pendingSrtPath);
                blog(LOG_INFO, "[AI Captions] Final SRT saved immediately (%zu entries) to '%s'",
                     srtWriter->getEntryCount(), pendingSrtPath.c_str());
            }
            stopCaptioning();
        } else {
            waitingForFinalSrt = true;
            blog(LOG_INFO, "[AI Captions] Waiting for %d pending transcription(s) before saving SRT...",
                 pendingTranscriptions.load());
        }
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
    if (s.getActiveApiKey().empty()) return;

    pendingTranscriptions++;

    if (s.provider == "gemini") {
        GeminiClient::transcribeAsync(
            wavData,
            startMs,
            endMs,
            s.gemini_api_key,
            s.gemini_model,
            s.custom_keywords,
            lastTranscribedContext,
            [this](bool success, const std::string &text, int64_t sMs, int64_t eMs) {
                this->onTranscriptionResult(success, text, sMs, eMs);
            }
        );
    } else {
        GroqWhisperClient::transcribeAsync(
            wavData,
            startMs,
            endMs,
            s.groq_api_key,
            s.groq_model,
            s.language,
            s.custom_keywords,
            [this](bool success, const std::string &text, int64_t sMs, int64_t eMs) {
                this->onTranscriptionResult(success, text, sMs, eMs);
            }
        );
    }
}

bool PluginManager::isHallucination(const std::string &rawText) {
    std::string text = rawText;
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r' || text.front() == '\n')) {
        text.erase(text.begin());
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' || text.back() == '\n')) {
        text.pop_back();
    }

    if (text.length() < 2) return true;

    // Check for repetitive word loops like "Closed, Closed, Closed"
    {
        std::istringstream iss(text);
        std::string word;
        std::vector<std::string> words;
        while (iss >> word) {
            std::string clean;
            for (char c : word) {
                if (isalnum((unsigned char)c) || (c & 0x80)) clean += (char)tolower((unsigned char)c);
            }
            if (!clean.empty()) words.push_back(clean);
        }
        if (words.size() >= 3) {
            bool allSame = true;
            for (size_t i = 1; i < words.size(); ++i) {
                if (words[i] != words[0]) {
                    allSame = false;
                    break;
                }
            }
            if (allSame) {
                blog(LOG_INFO, "[AI Captions] Hallucination filtered out (repetitive loop): \"%s\"", text.c_str());
                return true;
            }
        }
    }

    // Subtitle credits and hallucinated YouTube phrases
    static const std::vector<std::string> bannedPhrases = {
        "نانسي", "قنقر", "ترجمة", "المترجمات", "اشترك في القناة", "اشتراك",
        "تفعيل الجرس", "لا تنسوا الاشتراك", "شكرا للمشاهدة", "شكراً للمشاهدة",
        "closed, closed", "Closed, Closed", "thank you for watching",
        "thanks for watching", "subtitles by", "Sous-titres", "amara.org"
    };

    for (const auto &phrase : bannedPhrases) {
        if (text.find(phrase) != std::string::npos) {
            blog(LOG_INFO, "[AI Captions] Hallucination filtered out (banned token '%s'): \"%s\"",
                 phrase.c_str(), text.c_str());
            return true;
        }
    }
    return false;
}

void PluginManager::onTranscriptionResult(bool success, const std::string &text, int64_t startMs, int64_t endMs) {
    pendingTranscriptions--;

    if (success && !text.empty() && !isHallucination(text)) {
        PluginSettings s = getSettings();

        // 1. Live Text Source update in OBS scene
        if (s.update_text_source) {
            textWriter->updateText(text);
            blog(LOG_INFO, "[AI Captions] Text updated in source '%s': \"%s\"", s.text_source_name.c_str(), text.c_str());
        }

        // 2. Append to SRT transcript
        if (s.srt_enabled) {
            srtWriter->addEntry(startMs, endMs, text);
        }
        lastTranscribedContext = text;
    }

    // If recording was stopped and this was the last pending transcription:
    if (waitingForFinalSrt.load() && pendingTranscriptions.load() <= 0) {
        waitingForFinalSrt = false;
        if (!pendingSrtPath.empty() && srtWriter->getEntryCount() > 0) {
            srtWriter->saveToFile(pendingSrtPath);
            blog(LOG_INFO, "[AI Captions] Final SRT written with %zu entries to '%s'",
                 srtWriter->getEntryCount(), pendingSrtPath.c_str());
        }
        PluginSettings s = getSettings();
        if (s.sync_with_recording) {
            stopCaptioning();
        }
    }
}

void PluginManager::startQuickTest(
    const PluginSettings &testSettings,
    int durationSec,
    std::function<void(bool ok, const std::string &message)> callback
) {
    if (testSettings.getActiveApiKey().empty()) {
        std::string name = (testSettings.provider == "gemini") ? "Google Gemini" : "Groq";
        if (callback) callback(false, "يرجى كتابة مفتاح " + name + " API أولاً.");
        return;
    }

    blog(LOG_INFO, "[AI Captions] Starting Quick Test (%s) for %d seconds on '%s'...",
         testSettings.provider.c_str(), durationSec, testSettings.audio_source_name.c_str());

    audioCapture->startTestCapture(
        testSettings.audio_source_name,
        durationSec,
        [this, testSettings, callback](const std::vector<int16_t> &samples) {
            if (samples.empty()) {
                if (callback) callback(false, "لم يتم العثور على مصدر الصوت أو تعذر التقاط الصوت من المايكروفون.");
                return;
            }

            // Check RMS energy to verify microphone is receiving actual sound
            double sum = 0.0;
            for (int16_t s : samples) {
                double v = s / 32768.0;
                sum += v * v;
            }
            float rms = static_cast<float>(std::sqrt(sum / samples.size()));
            blog(LOG_INFO, "[AI Captions] Quick Test captured %zu samples (RMS: %.4f)", samples.size(), rms);

            if (rms < 0.0003f) {
                if (callback) callback(false, "الميكروفون صامت تماماً (RMS = 0). تأكد من اختيار المايكروفون الصحيح وتأكد أنه غير مكتوم (Unmuted).");
                return;
            }

            std::vector<uint8_t> wavData = WavEncoder::encode16BitMono(samples.data(), samples.size(), 16000);

            std::string errorMsg;
            std::string text;
            if (testSettings.provider == "gemini") {
                text = GeminiClient::transcribe(
                    wavData,
                    testSettings.gemini_api_key,
                    testSettings.gemini_model,
                    testSettings.custom_keywords,
                    "",
                    &errorMsg
                );
            } else {
                text = GroqWhisperClient::transcribe(
                    wavData,
                    testSettings.groq_api_key,
                    testSettings.groq_model,
                    testSettings.language,
                    testSettings.custom_keywords,
                    &errorMsg
                );
            }

            if (!text.empty()) {
                textWriter->setSourceName(testSettings.text_source_name);
                textWriter->updateText(text);
                if (callback) callback(true, text);
            } else {
                std::string err = errorMsg.empty() ? "فشل التعرف على الصوت من محرك الذكاء الاصطناعي." : errorMsg;
                if (callback) callback(false, err);
            }
        }
    );
}

void PluginManager::loadSettings(obs_data_t *data) {
    if (!data) return;
    obs_data_t *obj = obs_data_get_obj(data, "obs_ai_captions");
    if (!obj) {
        obs_data_t *modules = obs_data_get_obj(data, "modules");
        if (modules) {
            obj = obs_data_get_obj(modules, "obs_ai_captions");
            obs_data_release(modules);
        }
    }

    if (obj) {
        PluginSettings s;
        s.load(obj);
        updateSettings(s);
        obs_data_release(obj);
        blog(LOG_INFO, "[AI Captions] Settings loaded successfully from scene collection");
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

void PluginManager::loadFromConfigFile() {
    char *path = obs_module_config_path("config.json");
    if (path) {
        obs_data_t *obj = obs_data_create_from_json_file(path);
        if (obj) {
            PluginSettings s;
            s.load(obj);
            updateSettings(s);
            obs_data_release(obj);
            blog(LOG_INFO, "[AI Captions] Persistent config loaded from '%s'", path);
        }
        bfree(path);
    }
}

void PluginManager::saveToConfigFile() {
    char *path = obs_module_config_path("config.json");
    if (path) {
        obs_data_t *obj = obs_data_create();
        PluginSettings s = getSettings();
        s.save(obj);
        obs_data_save_json(obj, path);
        obs_data_release(obj);
        bfree(path);
    }
}
