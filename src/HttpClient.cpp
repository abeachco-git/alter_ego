#include "HttpSafety.h"
#include "HttpClient.h"
#include "ArenaNetTransport.h"

#include <windows.h>
#include <wininet.h>
#include <fstream>
#include <cstring>
#include <utility>
#include <vector>

namespace Skinventory {

    std::string HttpClient::Get(const std::string& url) {
        if (AlterEgo::ArenaNet::IsApiUrl(url)) {
            auto r=AlterEgo::ArenaNet::Get(url.substr(std::strlen(AlterEgo::ArenaNet::Origin)));
            return r.status_code>=200&&r.status_code<300?std::move(r.body):std::string{};
        }
        if (AlterEgo::Background::Workers().Stopping()) return {};
        HINTERNET hInternet = InternetOpenA("Skinventory/1.0",
            INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
        if (!hInternet) return "";
        AlterEgo::HttpSafety::Configure(hInternet);

        DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE |
                      INTERNET_FLAG_SECURE;

        HINTERNET hUrl = InternetOpenUrlA(hInternet, url.c_str(), NULL, 0, flags, 0);
        if (!hUrl) {
            InternetCloseHandle(hInternet);
            return "";
        }

        std::string result;
        const bool complete = AlterEgo::HttpSafety::ReadBody(hUrl, [&](const char* data, DWORD count) { result.append(data, count); });

        InternetCloseHandle(hUrl);
        InternetCloseHandle(hInternet);
        return complete ? result : std::string{};
    }

    HttpResponse HttpClient::GetEx(const std::string& url) {
        if(AlterEgo::ArenaNet::IsApiUrl(url))return AlterEgo::ArenaNet::Get(url.substr(std::strlen(AlterEgo::ArenaNet::Origin)));
        HttpResponse result;
        if (AlterEgo::Background::Workers().Stopping()) return {};
        HINTERNET hInternet = InternetOpenA("Skinventory/1.0",
            INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
        if (!hInternet) return result;
        AlterEgo::HttpSafety::Configure(hInternet);

        DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE |
                      INTERNET_FLAG_SECURE;

        HINTERNET hUrl = InternetOpenUrlA(hInternet, url.c_str(), NULL, 0, flags, 0);
        if (!hUrl) {
            InternetCloseHandle(hInternet);
            return result;
        }

        DWORD statusCode = 0;
        DWORD statusSize = sizeof(statusCode);
        HttpQueryInfoA(hUrl, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
                       &statusCode, &statusSize, NULL);
        result.status_code = (int)statusCode;

        const bool complete = AlterEgo::HttpSafety::ReadBody(hUrl, [&](const char* data, DWORD count) { result.body.append(data, count); });

        InternetCloseHandle(hUrl);
        InternetCloseHandle(hInternet);
        return complete ? result : HttpResponse{};
    }

    HttpResponse HttpClient::GetEx(const std::string& url, const std::string& headers) {
        if(AlterEgo::ArenaNet::IsApiUrl(url))return AlterEgo::ArenaNet::Get(url.substr(std::strlen(AlterEgo::ArenaNet::Origin)),{},headers);
        HttpResponse result;
        if (AlterEgo::Background::Workers().Stopping()) return {};
        HINTERNET hInternet = InternetOpenA("Skinventory/1.0",
            INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
        if (!hInternet) return result;
        AlterEgo::HttpSafety::Configure(hInternet);

        DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE |
                      INTERNET_FLAG_SECURE;

        HINTERNET hUrl = InternetOpenUrlA(hInternet, url.c_str(),
            headers.c_str(), (DWORD)headers.size(), flags, 0);
        if (!hUrl) {
            InternetCloseHandle(hInternet);
            return result;
        }

        DWORD statusCode = 0;
        DWORD statusSize = sizeof(statusCode);
        HttpQueryInfoA(hUrl, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
                       &statusCode, &statusSize, NULL);
        result.status_code = (int)statusCode;

        const bool complete = AlterEgo::HttpSafety::ReadBody(hUrl, [&](const char* data, DWORD count) { result.body.append(data, count); });

        InternetCloseHandle(hUrl);
        InternetCloseHandle(hInternet);
        return complete ? result : HttpResponse{};
    }

    HttpResponse HttpClient::PostJson(const std::string& url, const std::string& jsonBody, const std::string& headers) {
        HttpResponse result;
        if (AlterEgo::Background::Workers().Stopping()) return {};
        HINTERNET hInternet = InternetOpenA("Skinventory/1.0",
            INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
        if (!hInternet) return result;
        AlterEgo::HttpSafety::Configure(hInternet);

        // Parse URL components
        URL_COMPONENTSA uc = {};
        uc.dwStructSize = sizeof(uc);
        char host[256] = {}, path[2048] = {};
        uc.lpszHostName = host; uc.dwHostNameLength = sizeof(host);
        uc.lpszUrlPath = path; uc.dwUrlPathLength = sizeof(path);
        if (!InternetCrackUrlA(url.c_str(), 0, 0, &uc)) {
            InternetCloseHandle(hInternet);
            return result;
        }

        DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE |
                      INTERNET_FLAG_NO_AUTO_REDIRECT;
        if (uc.nScheme == INTERNET_SCHEME_HTTPS)
            flags |= INTERNET_FLAG_SECURE;

        HINTERNET hConnect = InternetConnectA(hInternet, host, uc.nPort,
            NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
        if (!hConnect) {
            InternetCloseHandle(hInternet);
            return result;
        }

        HINTERNET hRequest = HttpOpenRequestA(hConnect, "POST", path, NULL, NULL, NULL, flags, 0);
        if (!hRequest) {
            InternetCloseHandle(hConnect);
            InternetCloseHandle(hInternet);
            return result;
        }

        std::string allHeaders = "Content-Type: application/json\r\n" + headers;
        BOOL sent = HttpSendRequestA(hRequest,
            allHeaders.c_str(), (DWORD)allHeaders.size(),
            (LPVOID)jsonBody.c_str(), (DWORD)jsonBody.size());
        if (!sent) {
            InternetCloseHandle(hRequest);
            InternetCloseHandle(hConnect);
            InternetCloseHandle(hInternet);
            return result;
        }

        DWORD statusCode = 0;
        DWORD statusSize = sizeof(statusCode);
        HttpQueryInfoA(hRequest, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
                       &statusCode, &statusSize, NULL);
        result.status_code = (int)statusCode;

        const bool complete = AlterEgo::HttpSafety::ReadBody(hRequest, [&](const char* data, DWORD count) { result.body.append(data, count); });

        InternetCloseHandle(hRequest);
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hInternet);
        return complete ? result : HttpResponse{};
    }

    bool HttpClient::DownloadToFile(const std::string& url, const std::string& filePath) {
        if (AlterEgo::Background::Workers().Stopping()) return {};
        HINTERNET hInternet = InternetOpenA("Skinventory/1.0",
            INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
        if (!hInternet) return false;
        AlterEgo::HttpSafety::Configure(hInternet);

        HINTERNET hUrl = InternetOpenUrlA(hInternet, url.c_str(), NULL, 0,
            INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_SECURE, 0);
        if (!hUrl) {
            InternetCloseHandle(hInternet);
            return false;
        }

        std::vector<char> buffer;
        const bool complete = AlterEgo::HttpSafety::ReadBody(hUrl, [&](const char* data, DWORD count) { buffer.insert(buffer.end(), data, data + count); });

        InternetCloseHandle(hUrl);
        InternetCloseHandle(hInternet);

        if (!complete || buffer.empty()) return false;

        std::ofstream file(filePath, std::ios::binary);
        if (!file.is_open()) return false;
        file.write(buffer.data(), buffer.size());
        file.close();
        return true;
    }

}
