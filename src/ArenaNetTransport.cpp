#include "ArenaNetTransport.h"
#include "ArenaNetAdmission.h"
#include "ApiActivity.h"
#include "HttpSafety.h"
#include <chrono>
#include <mutex>
#include <thread>
#include <cstring>
#include <algorithm>
#include <cstdlib>

namespace AlterEgo::ArenaNet {
using Clock=std::chrono::steady_clock;
bool IsApiUrl(const std::string& url){return url.rfind(std::string(Origin)+"/v2/",0)==0;}
namespace {
Admission gate;
struct GateLease {~GateLease(){gate.Release();}};
Clock::time_point next{}, rateLimit{};
struct Handle {
    HINTERNET value=nullptr;
    explicit Handle(HINTERNET h):value(h){}
    ~Handle(){if(value)InternetCloseHandle(value);}
    Handle(const Handle&)=delete;
    Handle& operator=(const Handle&)=delete;
};
}
size_t Waiting(){return gate.Waiting();}
Skinventory::HttpResponse Get(const std::string& path,const std::string& key,const std::string& extraHeaders){
    if(path.rfind("/v2/",0)!=0||path.find_first_of("\r\n\\#")!=std::string::npos||
       key.find_first_of("\r\n")!=std::string::npos)return {};
    // Interactive/objective work overtakes background discovery before
    // admission; FIFO within priority and aging prevent starvation.
    if(!gate.Acquire([]{return Background::Workers().Stopping();},std::chrono::seconds(120),
        ApiActivity::Priority(ApiActivity::CurrentReason())))return {};
    GateLease lease;
    while(Clock::now()<std::max(next,rateLimit)){
        if(Background::Workers().Stopping())return {};
        if(!Background::Workers().WaitFor(std::chrono::milliseconds(25)))return {};
    }
    if(Background::Workers().Stopping())return {};
    next=Clock::now()+std::chrono::milliseconds(250);
    const auto deadline=HttpSafety::BeginRequest();
    Handle session(InternetOpenA("AlterEgo/1.0",key.empty()?INTERNET_OPEN_TYPE_PRECONFIG:INTERNET_OPEN_TYPE_DIRECT,nullptr,nullptr,0));
    if(!session.value||!HttpSafety::Ready(deadline))return {};
    HttpSafety::Configure(session.value);
    Handle connection(InternetConnectA(session.value,"api.guildwars2.com",INTERNET_DEFAULT_HTTPS_PORT,
                                      nullptr,nullptr,INTERNET_SERVICE_HTTP,0,0));
    if(!connection.value||!HttpSafety::Ready(deadline))return {};
    Handle request(HttpOpenRequestA(connection.value,"GET",path.c_str(),nullptr,nullptr,nullptr,
        INTERNET_FLAG_SECURE|INTERNET_FLAG_NO_AUTO_REDIRECT|INTERNET_FLAG_RELOAD|
        INTERNET_FLAG_NO_CACHE_WRITE|INTERNET_FLAG_NO_COOKIES|INTERNET_FLAG_NO_AUTH,0));
    if(!request.value||!HttpSafety::Ready(deadline))return {};
    std::string headers=extraHeaders;
    if(!key.empty())headers+="Authorization: Bearer "+key+"\r\n";
    ApiActivity::Attempt activity(ApiActivity::ApiEndpoint(path));
    const BOOL sent=HttpSendRequestA(request.value,headers.empty()?nullptr:headers.c_str(),
        static_cast<DWORD>(headers.size()),nullptr,0);
    if(!headers.empty())SecureZeroMemory(&headers[0],headers.size());
    if(!sent||!HttpSafety::Ready(deadline))return {};
    DWORD code=0,size=sizeof(code);
    if(!HttpQueryInfoA(request.value,HTTP_QUERY_STATUS_CODE|HTTP_QUERY_FLAG_NUMBER,&code,&size,nullptr))return {};
    activity.Status(static_cast<int>(code));
    if(code==429){
        char retry[64]{};size=sizeof(retry);unsigned long seconds=5;
        if(HttpQueryInfoA(request.value,HTTP_QUERY_RETRY_AFTER,retry,&size,nullptr)){
            char* end=nullptr;const auto n=std::strtoul(retry,&end,10);
            if(end!=retry&&*end=='\0')seconds=std::max(1UL,std::min(300UL,n));
        }
        rateLimit=Clock::now()+std::chrono::seconds(seconds);
    }
    Skinventory::HttpResponse result;result.status_code=static_cast<int>(code);
    if(!HttpSafety::ReadBody(request.value,[&](const char* data,DWORD count){result.body.append(data,count);},64u*1024u*1024u,deadline))return {};
    activity.Complete();
    return result;
}
}
