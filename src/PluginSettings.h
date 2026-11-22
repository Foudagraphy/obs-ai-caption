#pragma once

#include <string>
#include <obs.h>

struct PluginSettings {
    bool enabled = true;
    
    // AI Provider: "gemini" or "groq"
    std::string provider = "gemini";

    // Google Gemini Settings (The Antigravity Engine)
    std::string gemini_api_key = "";
    std::string gemini_model = "gemini-3.5-flash";

    // Groq Settings
    std::string groq_api_key = "";
    std::string groq_model = "whisper-large-v3-turbo";
    std::string language = "ar";

    // Common technical vocabulary biasing
    std::string custom_keywords = "Lightroom, Capture One, Photoshop, Masking, Exposure, LUT, White Balance, Highlights, Shadows, Color Grading, Premiere Pro";
    
    // Audio & VAD Settings
    std::string audio_source_name = "Mic/Aux";
    int pre_roll_ms = 350;             // 350ms pre-roll to catch word starts
    int silence_threshold_ms = 750;    // 0.75s silence detection (natural pause between phrases)
    float vad_sensitivity = 0.010f;    // Balanced RMS threshold (~ -40 dB) to eliminate breaths and mic noise
    
    // OBS Text Output Settings
    std::string text_source_name = "Captions";
    bool update_text_source = true;
    bool auto_clear_text = true;
    int clear_text_timeout_ms = 4000;  // 4s timeout
    
    // SRT Transcript Settings
    bool srt_enabled = true;
    std::string srt_output_folder = ""; // Empty = same folder as video recording
    int srt_max_line_length = 80;
    
    // Synchronization
    bool sync_with_recording = true;
    bool sync_with_streaming = false;

    // Helper to get active API key
    std::string getActiveApiKey() const {
        if (provider == "gemini") {
            return gemini_api_key;
        } else {
            return groq_api_key;
        }
    }

    void load(obs_data_t *data) {
        if (!data) return;
        enabled = obs_data_get_bool(data, "enabled");

        const char *prov = obs_data_get_string(data, "provider");
        if (prov && strlen(prov) > 0) provider = prov;

        const char *gKey = obs_data_get_string(data, "gemini_api_key");
        if (gKey) gemini_api_key = gKey;

        const char *gMod = obs_data_get_string(data, "gemini_model");
        if (gMod && strlen(gMod) > 0) {
            gemini_model = gMod;
            if (gemini_model == "gemini-2.0-flash" || gemini_model == "gemini-2.5-flash") {
                gemini_model = "gemini-3.5-flash";
            }
        }

        const char *grKey = obs_data_get_string(data, "groq_api_key");
        if (grKey) groq_api_key = grKey;

        const char *grMod = obs_data_get_string(data, "groq_model");
        if (grMod && strlen(grMod) > 0) groq_model = grMod;

        // Legacy api_key fallback migration
        const char *oldKey = obs_data_get_string(data, "api_key");
        if (oldKey && strlen(oldKey) > 0) {
            std::string k(oldKey);
            if (k.rfind("AIzaSy", 0) == 0 && gemini_api_key.empty()) {
                gemini_api_key = k;
                provider = "gemini";
            } else if (k.rfind("gsk_", 0) == 0 && groq_api_key.empty()) {
                groq_api_key = k;
            }
        }

        const char *lang = obs_data_get_string(data, "language");
        if (lang && strlen(lang) > 0) language = lang;

        const char *kw = obs_data_get_string(data, "custom_keywords");
        if (kw) custom_keywords = kw;

        const char *asrc = obs_data_get_string(data, "audio_source_name");
        if (asrc && strlen(asrc) > 0) audio_source_name = asrc;

        pre_roll_ms = (int)obs_data_get_int(data, "pre_roll_ms");
        if (pre_roll_ms <= 0) pre_roll_ms = 350;

        silence_threshold_ms = (int)obs_data_get_int(data, "silence_threshold_ms");
        if (silence_threshold_ms <= 0) silence_threshold_ms = 600;

        double vad = obs_data_get_double(data, "vad_sensitivity");
        if (vad > 0.0) vad_sensitivity = (float)vad;
        else vad_sensitivity = 0.010f;

        const char *tsrc = obs_data_get_string(data, "text_source_name");
        if (tsrc && strlen(tsrc) > 0) text_source_name = tsrc;

        update_text_source = obs_data_get_bool(data, "update_text_source");
        auto_clear_text = obs_data_get_bool(data, "auto_clear_text");
        
        int timeout = (int)obs_data_get_int(data, "clear_text_timeout_ms");
        if (timeout > 0) clear_text_timeout_ms = timeout;

        srt_enabled = obs_data_get_bool(data, "srt_enabled");
        const char *folder = obs_data_get_string(data, "srt_output_folder");
        if (folder) srt_output_folder = folder;

        int line_len = (int)obs_data_get_int(data, "srt_max_line_length");
        if (line_len > 0) srt_max_line_length = line_len;

        sync_with_recording = obs_data_get_bool(data, "sync_with_recording");
        sync_with_streaming = obs_data_get_bool(data, "sync_with_streaming");
    }

    void save(obs_data_t *data) const {
        if (!data) return;
        obs_data_set_bool(data, "enabled", enabled);
        obs_data_set_string(data, "provider", provider.c_str());
        obs_data_set_string(data, "gemini_api_key", gemini_api_key.c_str());
        obs_data_set_string(data, "gemini_model", gemini_model.c_str());
        obs_data_set_string(data, "groq_api_key", groq_api_key.c_str());
        obs_data_set_string(data, "groq_model", groq_model.c_str());
        obs_data_set_string(data, "language", language.c_str());
        obs_data_set_string(data, "custom_keywords", custom_keywords.c_str());
        obs_data_set_string(data, "audio_source_name", audio_source_name.c_str());
        obs_data_set_int(data, "pre_roll_ms", pre_roll_ms);
        obs_data_set_int(data, "silence_threshold_ms", silence_threshold_ms);
        obs_data_set_double(data, "vad_sensitivity", (double)vad_sensitivity);
        obs_data_set_string(data, "text_source_name", text_source_name.c_str());
        obs_data_set_bool(data, "update_text_source", update_text_source);
        obs_data_set_bool(data, "auto_clear_text", auto_clear_text);
        obs_data_set_int(data, "clear_text_timeout_ms", clear_text_timeout_ms);
        obs_data_set_bool(data, "srt_enabled", srt_enabled);
        obs_data_set_string(data, "srt_output_folder", srt_output_folder.c_str());
        obs_data_set_int(data, "srt_max_line_length", srt_max_line_length);
        obs_data_set_bool(data, "sync_with_recording", sync_with_recording);
        obs_data_set_bool(data, "sync_with_streaming", sync_with_streaming);
    }
};
