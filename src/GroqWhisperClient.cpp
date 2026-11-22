#include "GroqWhisperClient.h"
#include <iostream>
#include <sstream>
#include <thread>
#include <regex>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#endif

std::string GroqWhisperClient::extractJsonField(const std::string &json, const std::string &fieldName) {
    std::string pattern = "\"" + fieldName + "\"\\s*:\\s*\"((?:\\\\\"|[^\"])*)\"";
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
        return res;
    }
    return "";
}

std::string GroqWhisperClient::transcribe(
    const std::vector<uint8_t> &wavData,
    const std::string &apiKey,
    const std::string &model,
    const std::string &language,
    const std::string &customKeywords
) {
    if (wavData.empty() || apiKey.empty()) {
        return "";
    }

#ifdef _WIN32
    HINTERNET hSession = WinHttpOpen(
        L"OBS-AI-Caption-Plugin/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );
    if (!hSession) return "";

    HINTERNET hConnect = WinHttpConnect(
        hSession,
        L"api.groq.com",
        INTERNET_DEFAULT_HTTPS_PORT,
        0
    );
    if (!hConnect) {
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
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return "";
    }

    // Prepare multipart form data
    std::string boundary = "----OBSCaptionBoundaryX992819";
    std::string promptText = "ترجمة بالعامية المصرية بمصطلحات إنجليزية تقنية وتطبيقات: " + customKeywords;

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

    BOOL bResults = WinHttpSendRequest(
        hRequest,
        headers.c_str(),
        (DWORD)headers.length(),
        payload.data(),
        (DWORD)payload.size(),
        (DWORD)payload.size(),
        0
    );

    std::string responseString = "";
    if (bResults) {
        bResults = WinHttpReceiveResponse(hRequest, NULL);
    }

    if (bResults) {
        DWORD dwSize = 0;
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
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    return extractJsonField(responseString, "text");
#else
    // Fallback for non-windows
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
        std::string result = transcribe(wavData, apiKey, model, language, customKeywords);
        bool ok = !result.empty();
        if (callback) {
            callback(ok, result, startMs, endMs);
        }
    }).detach();
}
