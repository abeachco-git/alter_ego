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
                if(!AlterEgo::Background::Workers().WaitFor(std::chrono::milliseconds(25)))break;
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
    HINTERNET OpenChecked(HINTERNET session,const std::string& initial,const char* headers,DWORD headerSize,DWORD flags,AlterEgo::HttpSafety::Deadline deadline,std::string* failure=nullptr) {
        using AlterEgo::HttpSafety::Failure;
        if(R::Classify(initial)==R::Origin::Unknown){Failure(failure,"URL rejected");return nullptr;}
        auto url=initial;
        for(unsigned hop=0;hop<=4;++hop){
            if(!AlterEgo::HttpSafety::Ready(deadline)){Failure(failure,"request cancelled or deadline");return nullptr;}
            AlterEgo::HttpSafety::Handle h(InternetOpenUrlA(session,url.c_str(),headers,headerSize,flags|INTERNET_FLAG_NO_AUTO_REDIRECT,0));
            if(!h){const auto code=GetLastError();Failure(failure,"open HTTP URL",code);return nullptr;}
            if(!AlterEgo::HttpSafety::Ready(deadline)){Failure(failure,"request cancelled or deadline");return nullptr;}
            const int status=StatusCode(h);
            if(status!=301&&status!=302&&status!=303&&status!=307&&status!=308)return h.Release();
            char location[16385]{};DWORD count=sizeof(location);
            const bool got=HttpQueryInfoA(h,HTTP_QUERY_LOCATION,location,&count,nullptr)!=FALSE;
            if(!got){const auto code=GetLastError();Failure(failure,"read redirect",code);return nullptr;}
            if(!count||count>=sizeof(location)||hop==4){Failure(failure,"invalid redirect or hop limit");return nullptr;}
            std::string_view raw(location,count);
            if(raw.find('\0')!=std::string_view::npos){Failure(failure,"invalid redirect location");return nullptr;}
            for(unsigned char c:raw)if(c<=32||c==127||c=='\\'){Failure(failure,"invalid redirect location");return nullptr;}
            char combined[16385]{};DWORD length=sizeof(combined);
            if(!InternetCombineUrlA(url.c_str(),location,combined,&length,0)){const auto code=GetLastError();Failure(failure,"combine redirect",code);return nullptr;}
            std::string next(combined);
            if(!R::SafeRedirect(url,next)){Failure(failure,"redirect rejected by URL policy");return nullptr;}
            // Never forward custom headers to a changed host.
            if(headers&&url.substr(0,url.find('/',8))!=next.substr(0,next.find('/',8))){Failure(failure,"redirect rejected for custom headers");return nullptr;}
            url=std::move(next);
        }
        return nullptr;
    }
    }
    std::size_t HttpClient::WikiWaiting(){return wikiGate.Waiting();}

    std::string HttpClient::Get(const std::string& url) {
        auto r=GetEx(url);
        return r.status_code>=200&&r.status_code<300?std::move(r.body):std::string{};
    }

    HttpResponse HttpClient::GetEx(const std::string& url) {
        if(AlterEgo::ArenaNet::IsApiUrl(url))return AlterEgo::ArenaNet::Get(url.substr(std::strlen(AlterEgo::ArenaNet::Origin)));
        HttpResponse result;
        using namespace AlterEgo::HttpSafety;
        const auto fail=[&](const char* stage,DWORD code=0){
            Failure(&result.failure,stage,code);return std::move(result);
        };
        if(AlterEgo::Background::Workers().Stopping())return fail("cancelled");
        WikiLease wiki(url);
        if(!wiki.Ready())return fail("Wiki admission cancelled or queue timeout");
        const auto deadline=BeginRequest();
        Handle hInternet(InternetOpenA("Skinventory/1.0",INTERNET_OPEN_TYPE_PRECONFIG,NULL,NULL,0));
        if(!hInternet){const auto code=GetLastError();return fail("open WinINet session",code);}
        Configure(hInternet);
        const DWORD flags=INTERNET_FLAG_RELOAD|INTERNET_FLAG_NO_CACHE_WRITE|INTERNET_FLAG_SECURE;
        if(!Ready(deadline))return fail("request cancelled or deadline");
        wiki.Starting();
        AlterEgo::ApiActivity::Attempt activity(ArtworkEndpoint(url),TrackedArtwork(url));
        result.attempted=true;
        Handle hUrl(OpenChecked(hInternet,url,NULL,0,flags,deadline,&result.failure));
        if(!hUrl){if(result.failure.empty())result.failure="open HTTP URL";return result;}
        if(!Ready(deadline))return fail("request cancelled or deadline");
        DWORD statusCode=0,statusSize=sizeof(statusCode);
        if(!HttpQueryInfoA(hUrl,HTTP_QUERY_STATUS_CODE|HTTP_QUERY_FLAG_NUMBER,&statusCode,&statusSize,NULL)){
            const auto code=GetLastError();return fail("read HTTP status",code);
        }
        result.status_code=static_cast<int>(statusCode);
        activity.Status(result.status_code);wiki.Status(result.status_code);
        const bool complete=ReadBody(hUrl,[&](const char* data,DWORD count){result.body.append(data,count);},LimitFor(url),deadline,&result.failure);
        if(complete)activity.Complete();
        else{result.body.clear();result.status_code=0;}
        return result;
    }

    HttpResponse HttpClient::GetEx(const std::string& url, const std::string& headers) {
        if(AlterEgo::ArenaNet::IsApiUrl(url))return AlterEgo::ArenaNet::Get(url.substr(std::strlen(AlterEgo::ArenaNet::Origin)),{},headers);
        HttpResponse result;
        if (AlterEgo::Background::Workers().Stopping()) return {};
        WikiLease wiki(url);
        if(!wiki.Ready())return {};
        const auto deadline=AlterEgo::HttpSafety::BeginRequest();
        AlterEgo::HttpSafety::Handle hInternet(InternetOpenA("Skinventory/1.0",
            INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0));
        if (!hInternet) return result;
        AlterEgo::HttpSafety::Configure(hInternet);

        DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE |
                      INTERNET_FLAG_SECURE;

        if (!AlterEgo::HttpSafety::Ready(deadline)) {
            return {};
        }
        wiki.Starting();
        AlterEgo::ApiActivity::Attempt activity(ArtworkEndpoint(url),TrackedArtwork(url));
        AlterEgo::HttpSafety::Handle hUrl(OpenChecked(hInternet, url, headers.c_str(), (DWORD)headers.size(), flags,deadline));
        if (!hUrl || !AlterEgo::HttpSafety::Ready(deadline)) {
            return result;
        }

        DWORD statusCode = 0;
        DWORD statusSize = sizeof(statusCode);
        HttpQueryInfoA(hUrl, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
                       &statusCode, &statusSize, NULL);
        result.status_code = (int)statusCode;
        activity.Status(result.status_code);wiki.Status(result.status_code);

        const bool complete = AlterEgo::HttpSafety::ReadBody(hUrl, [&](const char* data, DWORD count) { result.body.append(data, count); },LimitFor(url),deadline);

        if(complete)activity.Complete();
        return complete ? result : HttpResponse{};
    }

    HttpResponse HttpClient::PostJson(const std::string& url, const std::string& jsonBody, const std::string& headers) {
        HttpResponse result;
        if (R::Classify(url)==R::Origin::Unknown || AlterEgo::Background::Workers().Stopping()) return {};
        const auto deadline=AlterEgo::HttpSafety::BeginRequest();
        AlterEgo::HttpSafety::Handle hInternet(InternetOpenA("Skinventory/1.0",
            INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0));
        if (!hInternet) return result;
        AlterEgo::HttpSafety::Configure(hInternet);

        // Parse URL components
        URL_COMPONENTSA uc = {};
        uc.dwStructSize = sizeof(uc);
        char host[256] = {}, path[2048] = {};
        uc.lpszHostName = host; uc.dwHostNameLength = sizeof(host);
        uc.lpszUrlPath = path; uc.dwUrlPathLength = sizeof(path);
        if (!InternetCrackUrlA(url.c_str(), 0, 0, &uc)) {
            return result;
        }

        DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE |
                      INTERNET_FLAG_NO_AUTO_REDIRECT;
        if (uc.nScheme == INTERNET_SCHEME_HTTPS)
            flags |= INTERNET_FLAG_SECURE;

        AlterEgo::HttpSafety::Handle hConnect(InternetConnectA(hInternet, host, uc.nPort,
            NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0));
        if (!hConnect || !AlterEgo::HttpSafety::Ready(deadline)) {
            return result;
        }

        AlterEgo::HttpSafety::Handle hRequest(HttpOpenRequestA(hConnect, "POST", path, NULL, NULL, NULL, flags, 0));
        if (!hRequest || !AlterEgo::HttpSafety::Ready(deadline)) {
            return result;
        }

        std::string allHeaders = "Content-Type: application/json\r\n" + headers;
        BOOL sent = HttpSendRequestA(hRequest,
            allHeaders.c_str(), (DWORD)allHeaders.size(),
            (LPVOID)jsonBody.c_str(), (DWORD)jsonBody.size());
        if (!sent || !AlterEgo::HttpSafety::Ready(deadline)) {
            return result;
        }

        DWORD statusCode = 0;
        DWORD statusSize = sizeof(statusCode);
        HttpQueryInfoA(hRequest, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
                       &statusCode, &statusSize, NULL);
        result.status_code = (int)statusCode;

        const bool complete = AlterEgo::HttpSafety::ReadBody(hRequest, [&](const char* data, DWORD count) { result.body.append(data, count); },LimitFor(url),deadline);

        return complete ? result : HttpResponse{};
    }

    bool HttpClient::DownloadToFile(const std::string& url,const std::string& filePath,std::string* failure) {
        // Share the same GET implementation, admission, deadline and response
        // diagnostics instead of maintaining a second network pipeline.
        using AlterEgo::HttpSafety::Failure;
        if(failure)failure->clear();
        if(!R::ArtworkUrl(url)){Failure(failure,"artwork URL rejected");return false;}
        const auto response=GetEx(url);
        if(!response.failure.empty()){if(failure)*failure=response.failure;return false;}
        if(response.status_code<200||response.status_code>=300){
            if(failure)*failure="artwork HTTP "+std::to_string(response.status_code);return false;
        }
        if(AlterEgo::Background::Workers().Stopping()){Failure(failure,"cancelled");return false;}
        if(response.body.empty()){Failure(failure,"empty image response");return false;}
        if(!R::ImageHeader(response.body)){
            if(failure)*failure="image header or dimensions rejected ("+std::to_string(response.body.size())+" bytes)";return false;
        }
        const auto temporary=filePath+".part";
        std::ofstream file(temporary,std::ios::binary|std::ios::trunc);
        if(!file.is_open()){Failure(failure,"open cache temporary file");return false;}
        file.write(response.body.data(),static_cast<std::streamsize>(response.body.size()));
        file.close();
        if(!file.good()){DeleteFileA(temporary.c_str());Failure(failure,"write cache temporary file");return false;}
        if(AlterEgo::Background::Workers().Stopping()){
            DeleteFileA(temporary.c_str());Failure(failure,"cancelled");return false;
        }
        if(!MoveFileExA(temporary.c_str(),filePath.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){
            const auto code=GetLastError();DeleteFileA(temporary.c_str());Failure(failure,"publish cache file",code);return false;
        }
        return true;
    }

}
