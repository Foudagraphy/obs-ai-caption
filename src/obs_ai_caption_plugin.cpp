#include <obs.h>
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <QAction>
#include <QMainWindow>
#include "PluginManager.h"
#include "ui/CaptionSettingsDialog.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-ai-caption", "en-US")

static void on_tools_menu_action() {
    QMainWindow *mainWin = static_cast<QMainWindow*>(obs_frontend_get_main_window());
    CaptionSettingsDialog dlg(mainWin);
    dlg.loadSettings(PluginManager::instance().getSettings());

    if (dlg.exec() == QDialog::Accepted) {
        PluginManager::instance().updateSettings(dlg.getSettings());
    }
}

static void obs_frontend_event_callback(enum obs_frontend_event event, void *) {
    switch (event) {
        case OBS_FRONTEND_EVENT_FINISHED_LOADING: {
            QAction *action = static_cast<QAction*>(obs_frontend_add_tools_menu_qaction("AI Closed Captions (مساعد الترجمة الذكي)"));
            if (action) {
                QObject::connect(action, &QAction::triggered, []() {
                    on_tools_menu_action();
                });
            }
            break;
        }
        case OBS_FRONTEND_EVENT_RECORDING_STARTED:
            PluginManager::instance().onRecordingStarted();
            break;
        case OBS_FRONTEND_EVENT_RECORDING_STOPPED:
            PluginManager::instance().onRecordingStopped();
            break;
        case OBS_FRONTEND_EVENT_STREAMING_STARTED:
            PluginManager::instance().onStreamingStarted();
            break;
        case OBS_FRONTEND_EVENT_STREAMING_STOPPED:
            PluginManager::instance().onStreamingStopped();
            break;
        default:
            break;
    }
}

static void obs_save_load_callback(obs_data_t *save_data, bool saving, void *) {
    if (saving) {
        PluginManager::instance().saveSettings(save_data);
    } else {
        PluginManager::instance().loadSettings(save_data);
    }
}

bool obs_module_load(void) {
    PluginManager::instance().initialize();
    obs_frontend_add_event_callback(obs_frontend_event_callback, nullptr);
    obs_frontend_add_save_callback(obs_save_load_callback, nullptr);
    blog(LOG_INFO, "[AI Closed Captions] Plugin loaded successfully.");
    return true;
}

void obs_module_unload(void) {
    PluginManager::instance().shutdown();
    obs_frontend_remove_event_callback(obs_frontend_event_callback, nullptr);
    obs_frontend_remove_save_callback(obs_save_load_callback, nullptr);
    blog(LOG_INFO, "[AI Closed Captions] Plugin unloaded.");
}

MODULE_EXPORT const char *obs_module_name(void) {
    return "AI Closed Captions";
}

MODULE_EXPORT const char *obs_module_description(void) {
    return "Real-time AI captions for OBS Studio supporting Egyptian Arabic and English technical code-switching powered by Groq Whisper.";
}
