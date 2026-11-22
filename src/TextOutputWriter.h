#pragma once

#include <string>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>

class TextOutputWriter {
public:
    TextOutputWriter();
    ~TextOutputWriter();

    void setSourceName(const std::string &sourceName);
    void setAutoClear(bool enable, int timeoutMs);

    void updateText(const std::string &text);
    void clearText();

private:
    std::string sourceName;
    bool autoClear;
    int timeoutMs;

    std::mutex mtx;
    std::condition_variable cv;
    std::atomic<bool> running;
    std::atomic<bool> resetTimer;
    std::thread timerThread;

    void timerLoop();
    void applyToObsSource(const std::string &text);
};
