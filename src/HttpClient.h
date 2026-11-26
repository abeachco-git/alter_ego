#pragma once

#include <string>
#include <cstddef>
#include <vector>

namespace Skinventory {

    struct HttpResponse {
        int status_code = 0;
        std::string body;
        // ArenaNet transport entered HttpSendRequest; local admission rejection is not an attempt.
        bool attempted=false;
    };

    class HttpClient {
    public:
        static std::size_t WikiWaiting();

        // Simple GET returning body only (empty on failure)
        static std::string Get(const std::string& url);

        // GET with full response (status code + body)
        static HttpResponse GetEx(const std::string& url);

        // GET with full response and custom headers (e.g. "X-App-Secret: value\r\n")
        static HttpResponse GetEx(const std::string& url, const std::string& headers);

        // POST with JSON body, custom headers, full response
        static HttpResponse PostJson(const std::string& url, const std::string& jsonBody, const std::string& headers);

        // Download a URL to a file on disk. Returns true on success.
        static bool DownloadToFile(const std::string& url, const std::string& filePath);
    };

}
