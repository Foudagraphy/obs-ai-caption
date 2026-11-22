#include "TextOutputWriter.h"
#include <obs.h>
#include <chrono>

TextOutputWriter::TextOutputWriter()
    : sourceName("Captions"),
      autoClear(true),
      timeoutMs(4000),
      running(true),
      resetTimer(false) {
    timerThread = std::thread(&TextOutputWriter::timerLoop, this);
}

TextOutputWriter::~TextOutputWriter() {
    running = false;
    cv.notify_all();
    if (timerThread.joinable()) {
        timerThread.join();
    }
}

void TextOutputWriter::setSourceName(const std::string &name) {
    std::lock_guard<std::mutex> lock(mtx);
    sourceName = name;
}

void TextOutputWriter::setAutoClear(bool enable, int ms) {
    std::lock_guard<std::mutex> lock(mtx);
    autoClear = enable;
    timeoutMs = ms > 0 ? ms : 4000;
}

void TextOutputWriter::updateText(const std::string &text) {
    applyToObsSource(text);
    if (autoClear) {
        resetTimer = true;
        cv.notify_one();
    }
}

void TextOutputWriter::clearText() {
    applyToObsSource("");
}

void TextOutputWriter::applyToObsSource(const std::string &text) {
    std::string name;
    {
        std::lock_guard<std::mutex> lock(mtx);
        name = sourceName;
    }

    if (name.empty()) return;

    obs_source_t *source = obs_get_source_by_name(name.c_str());
    if (source) {
        obs_data_t *settings = obs_data_create();
        obs_data_set_string(settings, "text", text.c_str());
        obs_source_update(source, settings);
        obs_data_release(settings);
        obs_source_release(source);
    }
}

void TextOutputWriter::timerLoop() {
    while (running) {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [this]() { return !running || resetTimer.load(); });
        if (!running) break;

        resetTimer = false;
        int delay = timeoutMs;
        lock.unlock();

        std::this_thread::sleep_for(std::chrono::milliseconds(delay));

        if (!resetTimer.load()) {
            applyToObsSource("");
        }
    }
}
