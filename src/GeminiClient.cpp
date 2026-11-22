#include "GeminiClient.h"
#include <iostream>
#include <sstream>
#include <thread>
#include <chrono>
#include <obs.h>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#endif

static const char base64Chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";

std::string GeminiClient::base64Encode(const uint8_t *data, size_t length) {
    std::string ret;
    ret.reserve(((length + 2) / 3) * 4);
    size_t i = 0;
    while (i < length) {
        uint32_t octet_a = i < length ? data[i++] : 0;
        uint32_t octet_b = i < length ? data[i++] : 0;
        uint32_t octet_c = i < length ? data[i++] : 0;

        uint32_t triple = (octet_a << 16) + (octet_b << 8) + octet_c;

        ret.push_back(base64Chars[(triple >> 18) & 0x3F]);
        ret.push_back(base64Chars[(triple >> 12) & 0x3F]);
        ret.push_back((i > length + 1) ? '=' : base64Chars[(triple >> 6) & 0x3F]);
        ret.push_back((i > length) ? '=' : base64Chars[triple & 0x3F]);
    }
    return ret;
}

std::string GeminiClient::escapeJson(const std::string &s) {
    std::ostringstream o;
    for (char c : s) {
        switch (c) {
            case '"': o << "\\\""; break;
            case '\\': o << "\\\\"; break;
            case '\b': o << "\\b"; break;
            case '\f': o << "\\f"; break;
            case '\n': o << "\\n"; break;
            case '\r': o << "\\r"; break;
            case '\t': o << "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    o << "\\u00"
                      << "0123456789abcdef"[(c >> 4) & 0xF]
                      << "0123456789abcdef"[c & 0xF];
                } else {
                    o << c;
                }
        }
    }
    return o.str();
}

// Safe, linear-time O(N) JSON string parser (Immune to regex recursion and stack overflow)
std::string GeminiClient::safeExtractJsonString(const std::string &json, const std::string &key) {
    if (json.empty() || key.empty()) return "";

    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return "";

    pos += needle.length();
    pos = json.find(':', pos);
    if (pos == std::string::npos) return "";
    pos++;

    // Skip whitespace until opening quote
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\r' || json[pos] == '\n')) {
        pos++;
    }
    if (pos >= json.length() || json[pos] != '"') return "";
    pos++; // Skip opening quote

    std::string result;
    result.reserve(128);

    while (pos < json.length()) {
        char c = json[pos];
        if (c == '\\') {
            if (pos + 1 >= json.length()) break;
            char next = json[pos + 1];
            if (next == '"') { result += '"'; pos += 2; }
            else if (next == '\\') { result += '\\'; pos += 2; }
            else if (next == 'n') { result += '\n'; pos += 2; }
            else if (next == 'r') { result += '\r'; pos += 2; }
            else if (next == 't') { result += '\t'; pos += 2; }
            else if (next == 'u' && pos + 5 < json.length()) {
                // Decode 4-hex-digit unicode escape \uXXXX into UTF-8
                std::string hexStr = json.substr(pos + 2, 4);
                try {
                    unsigned long codePoint = std::stoul(hexStr, nullptr, 16);
                    if (codePoint < 0x80) {
                        result += static_cast<char>(codePoint);
                    } else if (codePoint < 0x800) {
                        result += static_cast<char>(0xC0 | ((codePoint >> 6) & 0x1F));
                        result += static_cast<char>(0x80 | (codePoint & 0x3F));
                    } else {
                        result += static_cast<char>(0xE0 | ((codePoint >> 12) & 0x0F));
                        result += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
                        result += static_cast<char>(0x80 | (codePoint & 0x3F));
                    }
                } catch (...) {}
                pos += 6;
            } else {
                result += next;
                pos += 2;
            }
        } else if (c == '"') {
            break; // Closing quote reached
        } else {
            result += c;
            pos++;
        }
    }

    // Trim whitespace and quotation marks
    while (!result.empty() && (result.front() == ' ' || result.front() == '\t' || result.front() == '\r' || result.front() == '\n' || result.front() == '"')) {
        result.erase(result.begin());
    }
    while (!result.empty() && (result.back() == ' ' || result.back() == '\t' || result.back() == '\r' || result.back() == '\n' || result.back() == '"')) {
        result.pop_back();
    }
    return result;
}

std::string GeminiClient::extractCandidateText(const std::string &json) {
    try {
        return safeExtractJsonString(json, "text");
    } catch (...) {
        return "";
    }
}

std::string GeminiClient::transcribe(
    const std::vector<uint8_t> &wavData,
    const std::string &apiKey,
    const std::string &model,
    const std::string &customKeywords,
    const std::string &context,
    std::string *outError
) {
    if (wavData.empty()) {
        if (outError) *outError = "بيانات الصوت فارغة";
        return "";
    }
    if (apiKey.empty()) {
        if (outError) *outError = "مفتاح Google Gemini API Key فارغ";
        return "";
    }

#ifdef _WIN32
    try {
        auto startTime = std::chrono::steady_clock::now();

        HINTERNET hSession = WinHttpOpen(
            L"OBS-AI-Captions-Gemini/2.0",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS,
            0
        );
        if (!hSession) {
            DWORD err = GetLastError();
            blog(LOG_ERROR, "[AI Captions] WinHttpOpen failed: %lu", err);
            if (outError) *outError = "فشل إنشاء اتصال WinHTTP (Error: " + std::to_string(err) + ")";
            return "";
        }

        HINTERNET hConnect = WinHttpConnect(
            hSession,
            L"generativelanguage.googleapis.com",
            INTERNET_DEFAULT_HTTPS_PORT,
            0
        );
        if (!hConnect) {
            DWORD err = GetLastError();
            blog(LOG_ERROR, "[AI Captions] WinHttpConnect failed: %lu", err);
            if (outError) *outError = "تعذر الاتصال بسيرفر Google Gemini API (Error: " + std::to_string(err) + ")";
            WinHttpCloseHandle(hSession);
            return "";
        }

        // Default to gemini-3.5-flash: high quota (15+ RPM), multimodal intelligence, understands Egyptian Arabic + English
        std::string modelName = model.empty() ? "gemini-3.5-flash" : model;
        if (modelName == "gemini-2.0-flash" || modelName == "gemini-2.5-flash") {
            modelName = "gemini-3.5-flash";
        }

        std::string path = "/v1beta/models/" + modelName + ":generateContent?key=" + apiKey;
        std::wstring wPath(path.begin(), path.end());

        HINTERNET hRequest = WinHttpOpenRequest(
            hConnect,
            L"POST",
            wPath.c_str(),
            NULL,
            WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES,
            WINHTTP_FLAG_SECURE
        );
        if (!hRequest) {
            DWORD err = GetLastError();
            blog(LOG_ERROR, "[AI Captions] WinHttpOpenRequest failed: %lu", err);
            if (outError) *outError = "فشل فتح طلب HTTP لـ Gemini (Error: " + std::to_string(err) + ")";
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return "";
        }

        // Base64 encode WAV audio
        std::string b64Audio = base64Encode(wavData.data(), wavData.size());

        // Antigravity-grade Prompt: Egyptian Arabic speech with English technical terminology
        std::string instruction =
            "You are an expert speech transcriber. Transcribe the spoken audio verbatim in authentic Egyptian Arabic (لهجة مصرية عامية). "
            "CRITICAL REQUIREMENT: Always write software names, photography, and video editing terminology in English script (e.g. Lightroom, Photoshop, Capture One, Premiere Pro, Masking, Exposure, LUT, White Balance, Highlights, Shadows, Color Grading, Contrast, Preset, Curves, Saturation, Vibrance, Brush, Radial, Linear, Blacks, Whites";
        if (!customKeywords.empty()) {
            instruction += ", " + customKeywords;
        }
        instruction += "). Never transcribe English software or technical terms with Arabic letters. "
                       "Return ONLY the transcribed text. Do not output quotes, markdown, timestamps, or commentary.";

        if (!context.empty()) {
            instruction += " (Preceding context: \"" + escapeJson(context) + "\")";
        }

        // Construct JSON payload
        std::ostringstream jsonStream;
        jsonStream << "{"
                   << "\"contents\":[{"
                   << "\"parts\":["
                   << "{\"inline_data\":{\"mime_type\":\"audio/wav\",\"data\":\"" << b64Audio << "\"}},"
                   << "{\"text\":\"" << escapeJson(instruction) << "\"}"
                   << "]"
                   << "}],"
                   << "\"generationConfig\":{"
                   << "\"temperature\":0.0,"
                   << "\"maxOutputTokens\":250"
                   << "}"
                   << "}";

        std::string payload = jsonStream.str();
        std::wstring headers = L"Content-Type: application/json\r\n";

        blog(LOG_INFO, "[AI Captions] Sending %zu bytes audio to Google Gemini (%s)...",
             wavData.size(), modelName.c_str());

        BOOL bResults = WinHttpSendRequest(
            hRequest,
            headers.c_str(),
            (DWORD)headers.length(),
            (LPVOID)payload.data(),
            (DWORD)payload.size(),
            (DWORD)payload.size(),
            0
        );

        if (!bResults) {
            DWORD err = GetLastError();
            blog(LOG_ERROR, "[AI Captions] WinHttpSendRequest failed: %lu", err);
            if (outError) *outError = "فشل إرسال الصوت لـ Gemini (Error: " + std::to_string(err) + ")";
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return "";
        }

        bResults = WinHttpReceiveResponse(hRequest, NULL);
        if (!bResults) {
            DWORD err = GetLastError();
            blog(LOG_ERROR, "[AI Captions] WinHttpReceiveResponse failed: %lu", err);
            if (outError) *outError = "فشل استقبال الرد من Gemini (Error: " + std::to_string(err) + ")";
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return "";
        }

        // Check HTTP status code
        DWORD dwStatusCode = 0;
        DWORD dwSize = sizeof(dwStatusCode);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &dwStatusCode, &dwSize, WINHTTP_NO_HEADER_INDEX);

        std::string responseString = "";
        DWORD dwDownloaded = 0;
        do {
            dwSize = 0;
            if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
            if (dwSize == 0) break;

            std::vector<char> buffer(dwSize + 1, 0);
            if (WinHttpReadData(hRequest, buffer.data(), dwSize, &dwDownloaded)) {
                responseString.append(buffer.data(), dwDownloaded);
            }
        } while (dwSize > 0);

        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);

        auto endTime = std::chrono::steady_clock::now();
        int64_t elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

        if (dwStatusCode != 200) {
            blog(LOG_ERROR, "[AI Captions] Gemini API HTTP %lu error: %s", dwStatusCode, responseString.c_str());
            std::string errPart = safeExtractJsonString(responseString, "message");
            if (errPart.empty()) {
                errPart = responseString.substr(0, 150);
            }
            if (dwStatusCode == 429) {
                errPart = "تجاوز حد الطلبات المسموح (Rate Limit 429) - يرجى الانتظار أو استخدام موديل gemini-3.5-flash ذو الكوتا العالية";
            }
            if (outError) *outError = "Gemini HTTP " + std::to_string(dwStatusCode) + ": " + errPart;
            return "";
        }

        std::string text = extractCandidateText(responseString);
        blog(LOG_INFO, "[AI Captions] Google Gemini (%s) responded in %lld ms: \"%s\"",
             modelName.c_str(), elapsedMs, text.c_str());
        return text;
    } catch (const std::exception &e) {
        blog(LOG_ERROR, "[AI Captions] Exception in Gemini transcribe: %s", e.what());
        if (outError) *outError = std::string("خطأ برمجي: ") + e.what();
        return "";
    } catch (...) {
        blog(LOG_ERROR, "[AI Captions] Unknown exception in Gemini transcribe");
        if (outError) *outError = "خطأ غير معروف أثناء الاتصال بالسيرفر";
        return "";
    }
#else
    if (outError) *outError = "Non-Windows platform not supported";
    return "";
#endif
}

void GeminiClient::transcribeAsync(
    const std::vector<uint8_t> &wavData,
    int64_t startMs,
    int64_t endMs,
    const std::string &apiKey,
    const std::string &model,
    const std::string &customKeywords,
    const std::string &context,
    TranscriptionCallback callback
) {
    std::thread([=]() {
        try {
            std::string err;
            std::string result = transcribe(wavData, apiKey, model, customKeywords, context, &err);
            bool ok = !result.empty();
            if (callback) {
                callback(ok, result, startMs, endMs);
            }
        } catch (const std::exception &e) {
            blog(LOG_ERROR, "[AI Captions] Exception in Gemini transcribeAsync thread: %s", e.what());
            if (callback) {
                callback(false, "", startMs, endMs);
            }
        } catch (...) {
            blog(LOG_ERROR, "[AI Captions] Unknown exception in Gemini transcribeAsync thread");
            if (callback) {
                callback(false, "", startMs, endMs);
            }
        }
    }).detach();
}
