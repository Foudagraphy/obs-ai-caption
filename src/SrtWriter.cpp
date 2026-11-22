#include "SrtWriter.h"
#include <fstream>
#include <sstream>
#include <iomanip>

SrtWriter::SrtWriter(int maxLineLength) : maxLineLength(maxLineLength) {
}

SrtWriter::~SrtWriter() {
}

void SrtWriter::setMaxLineLength(int maxLen) {
    std::lock_guard<std::mutex> lock(mtx);
    maxLineLength = maxLen > 20 ? maxLen : 80;
}

void SrtWriter::reset() {
    std::lock_guard<std::mutex> lock(mtx);
    entries.clear();
}

size_t SrtWriter::getEntryCount() {
    std::lock_guard<std::mutex> lock(mtx);
    return entries.size();
}

std::string SrtWriter::formatTimestamp(int64_t ms) {
    if (ms < 0) ms = 0;
    int64_t totalSeconds = ms / 1000;
    int64_t millis = ms % 1000;
    int64_t seconds = totalSeconds % 60;
    int64_t minutes = (totalSeconds / 60) % 60;
    int64_t hours = totalSeconds / 3600;

    std::ostringstream ss;
    ss << std::setfill('0') << std::setw(2) << hours << ":"
       << std::setfill('0') << std::setw(2) << minutes << ":"
       << std::setfill('0') << std::setw(2) << seconds << ","
       << std::setfill('0') << std::setw(3) << millis;
    return ss.str();
}

std::string SrtWriter::wrapText(const std::string &text, int maxChars) {
    if (static_cast<int>(text.length()) <= maxChars) {
        return text;
    }

    std::istringstream words(text);
    std::string word;
    std::ostringstream result;
    std::string currentLine = "";

    while (words >> word) {
        if (currentLine.empty()) {
            currentLine = word;
        } else if (static_cast<int>(currentLine.length() + 1 + word.length()) <= maxChars) {
            currentLine += " " + word;
        } else {
            if (result.tellp() > 0) result << "\n";
            result << currentLine;
            currentLine = word;
        }
    }
    if (!currentLine.empty()) {
        if (result.tellp() > 0) result << "\n";
        result << currentLine;
    }

    return result.str();
}

void SrtWriter::addEntry(int64_t startMs, int64_t endMs, const std::string &text) {
    if (text.empty()) return;

    std::lock_guard<std::mutex> lock(mtx);
    SrtEntry entry;
    entry.index = static_cast<int>(entries.size()) + 1;
    entry.startMs = startMs;
    entry.endMs = endMs > startMs ? endMs : (startMs + 1000);
    entry.text = wrapText(text, maxLineLength);

    entries.push_back(entry);
}

std::string SrtWriter::getSrtContent() {
    std::lock_guard<std::mutex> lock(mtx);
    std::ostringstream ss;

    for (const auto &e : entries) {
        ss << e.index << "\n";
        ss << formatTimestamp(e.startMs) << " --> " << formatTimestamp(e.endMs) << "\n";
        ss << e.text << "\n\n";
    }
    return ss.str();
}

bool SrtWriter::saveToFile(const std::string &filePath) {
    if (filePath.empty()) return false;

    std::string content = getSrtContent();
    if (content.empty()) return false;

    // Save with UTF-8 BOM so Windows media players and editors display Arabic perfectly
    std::ofstream out(filePath, std::ios::binary);
    if (!out.is_open()) return false;

    unsigned char bom[3] = { 0xEF, 0xBB, 0xBF };
    out.write(reinterpret_cast<char*>(bom), 3);
    out.write(content.c_str(), content.length());
    out.close();

    return true;
}
