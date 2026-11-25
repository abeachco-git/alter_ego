#include "ArenaNetTransport.h"
#include "HttpSafety.h"
#include <chrono>
#include <mutex>
#include <thread>
#include <cstring>
#include <algorithm>

namespace AlterEgo::ArenaNet {
using Clock=std::chrono::steady_clock;
bool IsApiUrl(const std::string& url){return url.rfind(std::string(Origin)+"/v2/",0)==0;}
namespace {
std::mutex gate;
Clock::time_point next{}, rateLimit{};
struct Handle {
    HINTERNET value=nullptr;
    explicit Handle(HINTERNET h):value(h){}
    ~Handle(){if(value)InternetCloseHandle(value);}
    Handle(const Handle&)=delete;
    Handle& operator=(const Handle&)=delete;
};
}
Skinventory::HttpResponse Get(const std::string& path,const std::string& key,const std::string& extraHeaders){
    if(path.rfind("/v2/",0)!=0||path.find_first_of("\r\n\\#")!=std::string::npos||
       key.find_first_of("\r\n")!=std::string::npos)return {};
    // One bounded in-flight request. Waiting workers check cancellation.
    std::unique_lock<std::mutex> lock(gate,std::defer_lock);
    while(!lock.try_lock()){
        if(Background::Workers().Stopping())return {};
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    while(Clock::now()<std::max(next,rateLimit)){
        if(Background::Workers().Stopping())return {};
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    if(Background::Workers().Stopping())return {};
    next=Clock::now()+std::chrono::milliseconds(250);
    Handle session(InternetOpenA("AlterEgo/1.0",key.empty()?INTERNET_OPEN_TYPE_PRECONFIG:INTERNET_OPEN_TYPE_DIRECT,nullptr,nullptr,0));
    if(!session.value)return {};
    HttpSafety::Configure(session.value);
    Handle connection(InternetConnectA(session.value,"api.guildwars2.com",INTERNET_DEFAULT_HTTPS_PORT,
                                      nullptr,nullptr,INTERNET_SERVICE_HTTP,0,0));
    if(!connection.value)return {};
    Handle request(HttpOpenRequestA(connection.value,"GET",path.c_str(),nullptr,nullptr,nullptr,
        INTERNET_FLAG_SECURE|INTERNET_FLAG_NO_AUTO_REDIRECT|INTERNET_FLAG_RELOAD|
        INTERNET_FLAG_NO_CACHE_WRITE|INTERNET_FLAG_NO_COOKIES|INTERNET_FLAG_NO_AUTH,0));
    if(!request.value)return {};
    std::string headers=extraHeaders;
    if(!key.empty())headers+="Authorization: Bearer "+key+"\r\n";
    const BOOL sent=HttpSendRequestA(request.value,headers.empty()?nullptr:headers.c_str(),
        static_cast<DWORD>(headers.size()),nullptr,0);
    if(!headers.empty())SecureZeroMemory(&headers[0],headers.size());
    if(!sent)return {};
    DWORD code=0,size=sizeof(code);
    if(!HttpQueryInfoA(request.value,HTTP_QUERY_STATUS_CODE|HTTP_QUERY_FLAG_NUMBER,&code,&size,nullptr))return {};
    if(code==429){
        char retry[64]{};size=sizeof(retry);unsigned long seconds=5;
        if(HttpQueryInfoA(request.value,HTTP_QUERY_RETRY_AFTER,retry,&size,nullptr)){
            char* end=nullptr;const auto n=std::strtoul(retry,&end,10);
            if(end!=retry&&*end=='\0')seconds=std::max(1UL,std::min(300UL,n));
        }
        rateLimit=Clock::now()+std::chrono::seconds(seconds);
    }
    Skinventory::HttpResponse result;result.status_code=static_cast<int>(code);
    const auto deadline=Clock::now()+std::chrono::seconds(30);
    char buffer[8192];
    while(!Background::Workers().Stopping()&&Clock::now()<deadline){
        DWORD n=0;
        if(!InternetReadFile(request.value,buffer,sizeof(buffer),&n))return {};
        if(!n)return result;
        if(result.body.size()+n>64u*1024u*1024u)return {};
        result.body.append(buffer,n);
    }
    return {};
}
}
