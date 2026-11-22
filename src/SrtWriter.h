#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <mutex>

struct SrtEntry {
    int index;
    int64_t startMs;
    int64_t endMs;
    std::string text;
};

class SrtWriter {
public:
    SrtWriter(int maxLineLength = 80);
    ~SrtWriter();

    void setMaxLineLength(int maxLen);
    void reset();

    void addEntry(int64_t startMs, int64_t endMs, const std::string &text);
    bool saveToFile(const std::string &filePath);
    std::string getSrtContent();
    size_t getEntryCount();

    static std::string formatTimestamp(int64_t ms);
    static std::string wrapText(const std::string &text, int maxChars);

private:
    int maxLineLength;
    std::vector<SrtEntry> entries;
    std::mutex mtx;
};
