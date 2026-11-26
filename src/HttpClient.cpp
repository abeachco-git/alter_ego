#include "HttpSafety.h"
#include "ExternalResourceSafety.h"
#include "UntrustedInput.h"
#include "HttpClient.h"
#include "ArenaNetTransport.h"
#include "ArenaNetAdmission.h"
#include "ApiActivity.h"
#include <thread>
#include <chrono>

#include <windows.h>
#include <wininet.h>
#include <fstream>
#include <cstring>
#include <utility>
#include <vector>

namespace Skinventory {
    namespace {
    using Clock=std::chrono::steady_clock;
    AlterEgo::ArenaNet::Admission wikiGate;
    Clock::time_point wikiNext{};
    // One shared admission gate for all runtime Wiki GETs and artwork. Hold
    // until the response completes; do not hold a domain or UI mutex here.
    struct WikiLease {
        bool wiki=false,acquired=false;
        explicit WikiLease(const std::string& url):wiki(AlterEgo::ApiActivity::WikiUrl(url)){
            if(!wiki)return;
            acquired=wikiGate.Acquire([]{return AlterEgo::Background::Workers().Stopping();},std::chrono::seconds(120),
                AlterEgo::ApiActivity::Priority(AlterEgo::ApiActivity::CurrentReason()));
            if(!acquired)return;
            while(Clock::now()<wikiNext&&!AlterEgo::Background::Workers().Stopping())
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
        ~WikiLease(){if(acquired)wikiGate.Release();}
        bool Ready()const{return !AlterEgo::Background::Workers().Stopping()&&(!wiki||acquired);}
        void Starting(){if(wiki)wikiNext=Clock::now()+std::chrono::seconds(1);}
        void Status(int status){if(wiki&&status==429)wikiNext=std::max(wikiNext,Clock::now()+std::chrono::seconds(60));}
    };
    AlterEgo::ApiActivity::Endpoint ArtworkEndpoint(const std::string& url){
        using namespace AlterEgo::ApiActivity;
        return RenderUrl(url)?Endpoint::RenderArtwork:WikiEndpoint(url);
    }
    bool TrackedArtwork(const std::string& url){return AlterEgo::ApiActivity::RenderUrl(url)||AlterEgo::ApiActivity::WikiUrl(url);}
    int StatusCode(HINTERNET h){DWORD code=0,size=sizeof(code);HttpQueryInfoA(h,HTTP_QUERY_STATUS_CODE|HTTP_QUERY_FLAG_NUMBER,&code,&size,nullptr);return static_cast<int>(code);}
    }
    namespace {
    namespace R=AlterEgo::ExternalResource;
    std::size_t LimitFor(const std::string& url) {
        if(R::ArtworkUrl(url))return R::ArtworkBytes;
        if(R::Classify(url)==R::Origin::Wiki)return AlterEgo::Untrusted::Wiki.bytes;
        if(R::Classify(url)==R::Origin::SnowCrows||R::Classify(url)==R::Origin::MetaBattle)return 3u*1024u*1024u;
        return 64u*1024u*1024u;
    }
    HINTERNET OpenChecked(HINTERNET session,const std::string& initial,const char* headers,DWORD headerSize,DWORD flags) {
        if(R::Classify(initial)==R::Origin::Unknown)return nullptr;
        auto url=initial;
        for(unsigned hop=0;hop<=4;++hop){
            if(AlterEgo::Background::Workers().Stopping())return nullptr;
            auto h=InternetOpenUrlA(session,url.c_str(),headers,headerSize,flags|INTERNET_FLAG_NO_AUTO_REDIRECT,0);
            if(!h)return nullptr;
            const int status=StatusCode(h);
            if(status!=301&&status!=302&&status!=303&&status!=307&&status!=308)return h;
            char location[16385]{};DWORD count=sizeof(location);
            const bool got=HttpQueryInfoA(h,HTTP_QUERY_LOCATION,location,&count,nullptr)!=FALSE;
            InternetCloseHandle(h);
            if(!got||!count||count>=sizeof(location)||hop==4)return nullptr;
            std::string_view raw(location,count);
            if(raw.find('\0')!=std::string_view::npos)return nullptr;
            for(unsigned char c:raw)if(c<=32||c==127||c=='\\')return nullptr;
            char combined[16385]{};DWORD length=sizeof(combined);
            if(!InternetCombineUrlA(url.c_str(),location,combined,&length,0))return nullptr;
            std::string next(combined);
            if(!R::SafeRedirect(url,next))return nullptr;
            // Never forward custom headers to a changed host.
            if(headers&&url.substr(0,url.find('/',8))!=next.substr(0,next.find('/',8)))return nullptr;
            url=std::move(next);
        }
        return nullptr;
    }
    }
    std::size_t HttpClient::WikiWaiting(){return wikiGate.Waiting();}

    std::string HttpClient::Get(const std::string& url) {
        if (AlterEgo::ArenaNet::IsApiUrl(url)) {
            auto r=AlterEgo::ArenaNet::Get(url.substr(std::strlen(AlterEgo::ArenaNet::Origin)));
            return r.status_code>=200&&r.status_code<300?std::move(r.body):std::string{};
        }
        if (AlterEgo::Background::Workers().Stopping()) return {};
        WikiLease wiki(url);
        if(!wiki.Ready())return {};
        HINTERNET hInternet = InternetOpenA("Skinventory/1.0",
            INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
        if (!hInternet) return "";
        AlterEgo::HttpSafety::Configure(hInternet);

        DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE |
                      INTERNET_FLAG_SECURE;

        if (AlterEgo::Background::Workers().Stopping()) {
            InternetCloseHandle(hInternet);
            return {};
        }
        wiki.Starting();
        AlterEgo::ApiActivity::Attempt activity(ArtworkEndpoint(url),TrackedArtwork(url));
        HINTERNET hUrl = OpenChecked(hInternet, url, NULL, 0, flags);
        if (!hUrl || AlterEgo::Background::Workers().Stopping()) {
            if (hUrl) InternetCloseHandle(hUrl);
            InternetCloseHandle(hInternet);
            return "";
        }

        const int status=StatusCode(hUrl);
        activity.Status(status);wiki.Status(status);
        std::string result;
        const bool complete = AlterEgo::HttpSafety::ReadBody(hUrl, [&](const char* data, DWORD count) { result.append(data, count); },LimitFor(url));

        if(complete)activity.Complete();
        InternetCloseHandle(hUrl);
        InternetCloseHandle(hInternet);
        return complete && status>=200 && status<300 ? result : std::string{};
    }

    HttpResponse HttpClient::GetEx(const std::string& url) {
        if(AlterEgo::ArenaNet::IsApiUrl(url))return AlterEgo::ArenaNet::Get(url.substr(std::strlen(AlterEgo::ArenaNet::Origin)));
        HttpResponse result;
        if (AlterEgo::Background::Workers().Stopping()) return {};
        WikiLease wiki(url);
        if(!wiki.Ready())return {};
        HINTERNET hInternet = InternetOpenA("Skinventory/1.0",
            INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
        if (!hInternet) return result;
        AlterEgo::HttpSafety::Configure(hInternet);

        DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE |
                      INTERNET_FLAG_SECURE;

        if (AlterEgo::Background::Workers().Stopping()) {
            InternetCloseHandle(hInternet);
            return {};
        }
        wiki.Starting();
        AlterEgo::ApiActivity::Attempt activity(ArtworkEndpoint(url),TrackedArtwork(url));
        HINTERNET hUrl = OpenChecked(hInternet, url, NULL, 0, flags);
        if (!hUrl || AlterEgo::Background::Workers().Stopping()) {
            if (hUrl) InternetCloseHandle(hUrl);
            InternetCloseHandle(hInternet);
            return result;
        }

        DWORD statusCode = 0;
        DWORD statusSize = sizeof(statusCode);
        HttpQueryInfoA(hUrl, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
                       &statusCode, &statusSize, NULL);
        result.status_code = (int)statusCode;
        activity.Status(result.status_code);wiki.Status(result.status_code);

        const bool complete = AlterEgo::HttpSafety::ReadBody(hUrl, [&](const char* data, DWORD count) { result.body.append(data, count); },LimitFor(url));

        if(complete)activity.Complete();
        InternetCloseHandle(hUrl);
        InternetCloseHandle(hInternet);
        return complete ? result : HttpResponse{};
    }

    HttpResponse HttpClient::GetEx(const std::string& url, const std::string& headers) {
        if(AlterEgo::ArenaNet::IsApiUrl(url))return AlterEgo::ArenaNet::Get(url.substr(std::strlen(AlterEgo::ArenaNet::Origin)),{},headers);
        HttpResponse result;
        if (AlterEgo::Background::Workers().Stopping()) return {};
        WikiLease wiki(url);
        if(!wiki.Ready())return {};
        HINTERNET hInternet = InternetOpenA("Skinventory/1.0",
            INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
        if (!hInternet) return result;
        AlterEgo::HttpSafety::Configure(hInternet);

        DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE |
                      INTERNET_FLAG_SECURE;

        if (AlterEgo::Background::Workers().Stopping()) {
            InternetCloseHandle(hInternet);
            return {};
        }
        wiki.Starting();
        AlterEgo::ApiActivity::Attempt activity(ArtworkEndpoint(url),TrackedArtwork(url));
        HINTERNET hUrl = OpenChecked(hInternet, url, headers.c_str(), (DWORD)headers.size(), flags);
        if (!hUrl || AlterEgo::Background::Workers().Stopping()) {
            if (hUrl) InternetCloseHandle(hUrl);
            InternetCloseHandle(hInternet);
            return result;
        }

        DWORD statusCode = 0;
        DWORD statusSize = sizeof(statusCode);
        HttpQueryInfoA(hUrl, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
                       &statusCode, &statusSize, NULL);
        result.status_code = (int)statusCode;
        activity.Status(result.status_code);wiki.Status(result.status_code);

        const bool complete = AlterEgo::HttpSafety::ReadBody(hUrl, [&](const char* data, DWORD count) { result.body.append(data, count); },LimitFor(url));

        if(complete)activity.Complete();
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
        if (!hConnect || AlterEgo::Background::Workers().Stopping()) {
            if (hConnect) InternetCloseHandle(hConnect);
            InternetCloseHandle(hInternet);
            return result;
        }

        HINTERNET hRequest = HttpOpenRequestA(hConnect, "POST", path, NULL, NULL, NULL, flags, 0);
        if (!hRequest || AlterEgo::Background::Workers().Stopping()) {
            if (hRequest) InternetCloseHandle(hRequest);
            InternetCloseHandle(hConnect);
            InternetCloseHandle(hInternet);
            return result;
        }

        std::string allHeaders = "Content-Type: application/json\r\n" + headers;
        BOOL sent = HttpSendRequestA(hRequest,
            allHeaders.c_str(), (DWORD)allHeaders.size(),
            (LPVOID)jsonBody.c_str(), (DWORD)jsonBody.size());
        if (!sent || AlterEgo::Background::Workers().Stopping()) {
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

        const bool complete = AlterEgo::HttpSafety::ReadBody(hRequest, [&](const char* data, DWORD count) { result.body.append(data, count); },LimitFor(url));

        InternetCloseHandle(hRequest);
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hInternet);
        return complete ? result : HttpResponse{};
    }

    bool HttpClient::DownloadToFile(const std::string& url, const std::string& filePath) {
        if(!R::ArtworkUrl(url))return false;
        if (AlterEgo::Background::Workers().Stopping()) return {};
        WikiLease wiki(url);
        if(!wiki.Ready())return {};
        HINTERNET hInternet = InternetOpenA("Skinventory/1.0",
            INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
        if (!hInternet) return false;
        AlterEgo::HttpSafety::Configure(hInternet);

        if (AlterEgo::Background::Workers().Stopping()) {
            InternetCloseHandle(hInternet);
            return {};
        }
        wiki.Starting();
        AlterEgo::ApiActivity::Attempt activity(ArtworkEndpoint(url),TrackedArtwork(url));
        HINTERNET hUrl = OpenChecked(hInternet, url, NULL, 0,
            INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_SECURE);
        if (!hUrl || AlterEgo::Background::Workers().Stopping()) {
            if (hUrl) InternetCloseHandle(hUrl);
            InternetCloseHandle(hInternet);
            return false;
        }

        const int status=StatusCode(hUrl);
        activity.Status(status);wiki.Status(status);
        std::vector<char> buffer;
        const bool complete = AlterEgo::HttpSafety::ReadBody(hUrl, [&](const char* data, DWORD count) { buffer.insert(buffer.end(), data, data + count); },R::ArtworkBytes);

        if(complete)activity.Complete();
        InternetCloseHandle(hUrl);
        InternetCloseHandle(hInternet);

        if (!complete || AlterEgo::Background::Workers().Stopping() || status<200 || status>=300 || buffer.empty()) return false;

        if(!R::ImageHeader(std::string_view(buffer.data(),buffer.size())))return false;
        const auto temporary=filePath+".part";
        std::ofstream file(temporary, std::ios::binary|std::ios::trunc);
        if (!file.is_open()) return false;
        file.write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        file.close();
        if(!file.good()||!MoveFileExA(temporary.c_str(),filePath.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){
            DeleteFileA(temporary.c_str());return false;
        }
        return true;
    }

}
