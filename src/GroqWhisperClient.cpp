#include "GroqWhisperClient.h"
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

std::string GroqWhisperClient::extractJsonField(const std::string &json, const std::string &fieldName) {
    try {
        return GeminiClient::safeExtractJsonString(json, fieldName);
    } catch (...) {
        return "";
    }
}

std::string GroqWhisperClient::transcribe(
    const std::vector<uint8_t> &wavData,
    const std::string &apiKey,
    const std::string &model,
    const std::string &language,
    const std::string &customKeywords,
    std::string *outError
) {
    if (wavData.empty()) {
        if (outError) *outError = "بيانات الصوت فارغة (No audio data)";
        return "";
    }
    if (apiKey.empty()) {
        if (outError) *outError = "مفتاح Groq API Key غير محدد";
        return "";
    }

#ifdef _WIN32
    auto startTime = std::chrono::steady_clock::now();

    HINTERNET hSession = WinHttpOpen(
        L"OBS-AI-Caption-Plugin/1.0",
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
        L"api.groq.com",
        INTERNET_DEFAULT_HTTPS_PORT,
        0
    );
    if (!hConnect) {
        DWORD err = GetLastError();
        blog(LOG_ERROR, "[AI Captions] WinHttpConnect to api.groq.com failed: %lu", err);
        if (outError) *outError = "تعذر الاتصال بسيرفر api.groq.com (Error: " + std::to_string(err) + ")";
        WinHttpCloseHandle(hSession);
        return "";
    }

    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect,
        L"POST",
        L"/openai/v1/audio/transcriptions",
        NULL,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE
    );
    if (!hRequest) {
        DWORD err = GetLastError();
        blog(LOG_ERROR, "[AI Captions] WinHttpOpenRequest failed: %lu", err);
        if (outError) *outError = "فشل فتح طلب HTTP (Error: " + std::to_string(err) + ")";
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return "";
    }

    // Prepare multipart form data
    std::string boundary = "----OBSCaptionBoundaryX992819";
    // Vocabulary biasing prompt without "ترجمة" to prevent YouTube translator credits hallucinations
    std::string promptText = customKeywords;
    if (!promptText.empty()) {
        promptText += ". مصطلحات تقنية وتصميم وتصوير ومونتاج بالعامية.";
    }

    std::ostringstream bodyStream;
    // 1. model
    bodyStream << "--" << boundary << "\r\n";
    bodyStream << "Content-Disposition: form-data; name=\"model\"\r\n\r\n";
    bodyStream << (model.empty() ? "whisper-large-v3" : model) << "\r\n";

    // 2. language
    if (!language.empty() && language != "auto") {
        bodyStream << "--" << boundary << "\r\n";
        bodyStream << "Content-Disposition: form-data; name=\"language\"\r\n\r\n";
        bodyStream << language << "\r\n";
    }

    // 3. response_format
    bodyStream << "--" << boundary << "\r\n";
    bodyStream << "Content-Disposition: form-data; name=\"response_format\"\r\n\r\n";
    bodyStream << "json\r\n";

    // 4. temperature
    bodyStream << "--" << boundary << "\r\n";
    bodyStream << "Content-Disposition: form-data; name=\"temperature\"\r\n\r\n";
    bodyStream << "0.0\r\n";

    // 5. prompt (vocabulary context biasing)
    bodyStream << "--" << boundary << "\r\n";
    bodyStream << "Content-Disposition: form-data; name=\"prompt\"\r\n\r\n";
    bodyStream << promptText << "\r\n";

    // 6. file header
    bodyStream << "--" << boundary << "\r\n";
    bodyStream << "Content-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\n";
    bodyStream << "Content-Type: audio/wav\r\n\r\n";

    std::string prefixStr = bodyStream.str();
    std::string suffixStr = "\r\n--" + boundary + "--\r\n";

    size_t totalPayloadSize = prefixStr.size() + wavData.size() + suffixStr.size();
    std::vector<uint8_t> payload;
    payload.reserve(totalPayloadSize);
    payload.insert(payload.end(), prefixStr.begin(), prefixStr.end());
    payload.insert(payload.end(), wavData.begin(), wavData.end());
    payload.insert(payload.end(), suffixStr.begin(), suffixStr.end());

    // Headers
    std::wstring headers = L"Content-Type: multipart/form-data; boundary=" + std::wstring(boundary.begin(), boundary.end()) + L"\r\n";
    std::wstring auth = L"Authorization: Bearer " + std::wstring(apiKey.begin(), apiKey.end()) + L"\r\n";
    headers += auth;

    blog(LOG_INFO, "[AI Captions] Sending %zu bytes audio to Groq Whisper (%s)...", wavData.size(), model.c_str());

    BOOL bResults = WinHttpSendRequest(
        hRequest,
        headers.c_str(),
        (DWORD)headers.length(),
        payload.data(),
        (DWORD)payload.size(),
        (DWORD)payload.size(),
        0
    );

    if (!bResults) {
        DWORD err = GetLastError();
        blog(LOG_ERROR, "[AI Captions] WinHttpSendRequest failed with error: %lu", err);
        if (outError) *outError = "فشل إرسال الصوت لسيرفر Groq (Error: " + std::to_string(err) + ")";
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return "";
    }

    bResults = WinHttpReceiveResponse(hRequest, NULL);
    if (!bResults) {
        DWORD err = GetLastError();
        blog(LOG_ERROR, "[AI Captions] WinHttpReceiveResponse failed with error: %lu", err);
        if (outError) *outError = "فشل استقبال الرد من Groq (Error: " + std::to_string(err) + ")";
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
        blog(LOG_ERROR, "[AI Captions] Groq API returned HTTP error %lu: %s", dwStatusCode, responseString.c_str());
        std::string apiErr = extractJsonField(responseString, "message");
        if (apiErr.empty()) apiErr = responseString;
        if (outError) *outError = "Groq HTTP " + std::to_string(dwStatusCode) + ": " + apiErr;
        return "";
    }

    std::string text = extractJsonField(responseString, "text");
    blog(LOG_INFO, "[AI Captions] Groq Whisper responded in %lld ms: \"%s\"", elapsedMs, text.c_str());
    return text;
#else
    if (outError) *outError = "Non-Windows platform not supported";
    return "";
#endif
}

void GroqWhisperClient::transcribeAsync(
    const std::vector<uint8_t> &wavData,
    int64_t startMs,
    int64_t endMs,
    const std::string &apiKey,
    const std::string &model,
    const std::string &language,
    const std::string &customKeywords,
    TranscriptionCallback callback
) {
    std::thread([=]() {
        try {
            std::string err;
            std::string result = transcribe(wavData, apiKey, model, language, customKeywords, &err);
            bool ok = !result.empty();
            if (callback) {
                callback(ok, result, startMs, endMs);
            }
        } catch (const std::exception &e) {
            blog(LOG_ERROR, "[AI Captions] Exception in Groq transcribeAsync thread: %s", e.what());
            if (callback) {
                callback(false, "", startMs, endMs);
            }
        } catch (...) {
            blog(LOG_ERROR, "[AI Captions] Unknown exception in Groq transcribeAsync thread");
            if (callback) {
                callback(false, "", startMs, endMs);
            }
        }
    }).detach();
}
