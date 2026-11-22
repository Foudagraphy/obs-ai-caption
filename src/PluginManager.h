#pragma once

#include "PluginSettings.h"
#include "AudioSegmenter.h"
#include "AudioCaptureSession.h"
#include "TextOutputWriter.h"
#include "SrtWriter.h"
#include <string>
#include <memory>
#include <mutex>
#include <atomic>
#include <functional>

class PluginManager {
public:
    static PluginManager &instance();

    void initialize();
    void shutdown();

    void updateSettings(const PluginSettings &newSettings);
    PluginSettings getSettings();

    bool startCaptioning();
    void stopCaptioning();
    bool isCaptioningActive() const;

    // OBS Frontend Event Handlers
    void onRecordingStarted();
    void onRecordingStopped();
    void onStreamingStarted();
    void onStreamingStopped();

    // Scene collection save/load
    void loadSettings(obs_data_t *data);
    void saveSettings(obs_data_t *data);

    // Persistent JSON config file
    void loadFromConfigFile();
    void saveToConfigFile();

    // Quick Test feature
    void startQuickTest(
        const PluginSettings &testSettings,
        int durationSec,
        std::function<void(bool ok, const std::string &message)> callback
    );

private:
    PluginManager();
    ~PluginManager();

    PluginSettings settings;
    std::unique_ptr<AudioSegmenter> segmenter;
    std::unique_ptr<AudioCaptureSession> audioCapture;
    std::unique_ptr<TextOutputWriter> textWriter;
    std::unique_ptr<SrtWriter> srtWriter;

    std::atomic<bool> isCaptioning;
    std::atomic<bool> isRecording;
    std::atomic<int> pendingTranscriptions;
    std::atomic<bool> waitingForFinalSrt;
    std::string pendingSrtPath;
    std::string lastTranscribedContext;
    int64_t recordingStartTimeMs;

    mutable std::mutex settingsMutex;

    void onSegmentReady(const std::vector<uint8_t> &wavData, int64_t startMs, int64_t endMs);
    void onTranscriptionResult(bool success, const std::string &text, int64_t startMs, int64_t endMs);
    bool isHallucination(const std::string &text);
};
