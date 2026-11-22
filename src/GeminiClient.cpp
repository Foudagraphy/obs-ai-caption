#include "GeminiClient.h"
#include <iostream>
#include <sstream>
#include <thread>
#include <regex>
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
                if ('\x00' <= c && c <= '\x1f') {
                    o << "\\u" << std::hex << (int)c;
                } else {
                    o << c;
                }
        }
    }
    return o.str();
}

std::string GeminiClient::extractCandidateText(const std::string &json) {
    // Look for "text": "..." within candidates
    std::string pattern = "\"text\"\\s*:\\s*\"((?:\\\\\"|[^\"])*)\"";
    std::regex re(pattern);
    std::smatch match;
    if (std::regex_search(json, match, re) && match.size() > 1) {
        std::string raw = match[1].str();
        std::string res;
        for (size_t i = 0; i < raw.length(); ++i) {
            if (raw[i] == '\\' && i + 1 < raw.length()) {
                char next = raw[i + 1];
                if (next == '"') { res += '"'; i++; }
                else if (next == '\\') { res += '\\'; i++; }
                else if (next == 'n') { res += '\n'; i++; }
                else if (next == 'r') { res += '\r'; i++; }
                else if (next == 't') { res += '\t'; i++; }
                else { res += raw[i]; }
            } else {
                res += raw[i];
            }
        }
        // Trim leading and trailing whitespace and surrounding quotes
        while (!res.empty() && (res.front() == ' ' || res.front() == '\t' || res.front() == '\n' || res.front() == '\r' || res.front() == '"')) {
            res.erase(res.begin());
        }
        while (!res.empty() && (res.back() == ' ' || res.back() == '\t' || res.back() == '\n' || res.back() == '\r' || res.back() == '"')) {
            res.pop_back();
        }
        return res;
    }
    return "";
}

std::string GeminiClient::transcribe(
    const std::vector<uint8_t> &wavData,
    const std::string &apiKey,
    const std::string &model,
    const std::string &customKeywords,
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
    auto startTime = std::chrono::steady_clock::now();

    HINTERNET hSession = WinHttpOpen(
        L"OBS-AI-Captions-Gemini/1.0",
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
        blog(LOG_ERROR, "[AI Captions] WinHttpConnect to generativelanguage.googleapis.com failed: %lu", err);
        if (outError) *outError = "تعذر الاتصال بسيرفر Google Gemini API (Error: " + std::to_string(err) + ")";
        WinHttpCloseHandle(hSession);
        return "";
    }

    std::string modelName = model.empty() ? "gemini-3.5-transcribe" : model;
    if (modelName == "gemini-2.0-flash" || modelName == "gemini-2.5-flash") {
        modelName = "gemini-3.5-transcribe";
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

    // Prompt instructing Gemini for Egyptian Arabic + English technical code-switching
    std::string instruction =
        "Transcribe the audio exactly in Egyptian Arabic as spoken. "
        "Keep software names, tools, and technical terms in English letters (e.g. Lightroom, Photoshop, Masking, Exposure, LUT, Color Grading, Highlights, Shadows";
    if (!customKeywords.empty()) {
        instruction += ", " + customKeywords;
    }
    instruction += "). Return ONLY the transcribed text. Do not add quotes, markdown, or commentary.";

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

    blog(LOG_INFO, "[AI Captions] Sending %zu bytes audio (%zu b64 chars) to Google Gemini (%s)...",
         wavData.size(), b64Audio.size(), modelName.c_str());

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
        blog(LOG_ERROR, "[AI Captions] WinHttpSendRequest to Gemini failed: %lu", err);
        if (outError) *outError = "فشل إرسال الصوت لـ Gemini (Error: " + std::to_string(err) + ")";
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return "";
    }

    bResults = WinHttpReceiveResponse(hRequest, NULL);
    if (!bResults) {
        DWORD err = GetLastError();
        blog(LOG_ERROR, "[AI Captions] WinHttpReceiveResponse from Gemini failed: %lu", err);
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
        blog(LOG_ERROR, "[AI Captions] Gemini API HTTP error %lu: %s", dwStatusCode, responseString.c_str());
        std::string errPart;
        std::regex errRe("\"message\"\\s*:\\s*\"((?:\\\\\"|[^\"])*)\"");
        std::smatch m;
        if (std::regex_search(responseString, m, errRe) && m.size() > 1) {
            errPart = m[1].str();
        } else {
            errPart = responseString;
        }
        if (outError) *outError = "Gemini HTTP " + std::to_string(dwStatusCode) + ": " + errPart;
        return "";
    }

    std::string text = extractCandidateText(responseString);
    blog(LOG_INFO, "[AI Captions] Google Gemini responded in %lld ms: \"%s\"", elapsedMs, text.c_str());
    return text;
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
    TranscriptionCallback callback
) {
    std::thread([=]() {
        std::string err;
        std::string result = transcribe(wavData, apiKey, model, customKeywords, &err);
        bool ok = !result.empty();
        if (callback) {
            callback(ok, result, startMs, endMs);
        }
    }).detach();
}
