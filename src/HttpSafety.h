#pragma once
#include "BackgroundWorkers.h"
#include <windows.h>
#include <wininet.h>
#include <chrono>
#include <cstddef>
#include <algorithm>

namespace AlterEgo::HttpSafety {
using Clock = std::chrono::steady_clock;
using Deadline = Clock::time_point;
// Cooperative overall budget begins after admission, not at the first read.
// Synchronous WinINet calls may still overrun it; only the owner closes handles.
inline Deadline BeginRequest() { return Clock::now()+std::chrono::seconds(60); }
inline bool Ready(Deadline deadline) {
    return !Background::Workers().Stopping() && Clock::now()<deadline;
}
class Handle {
    HINTERNET handle_;
public:
    explicit Handle(HINTERNET h) noexcept : handle_(h) {}
    ~Handle() { if(handle_) InternetCloseHandle(handle_); }
    Handle(const Handle&)=delete;
    Handle& operator=(const Handle&)=delete;
    operator HINTERNET() const noexcept { return handle_; }
    HINTERNET Release() noexcept { auto h=handle_;handle_=nullptr;return h; }
};
inline void Configure(HINTERNET session) {
    DWORD timeout = 12000;
    DWORD retries = 1;
    InternetSetOptionA(session, INTERNET_OPTION_CONNECT_RETRIES, &retries, sizeof(retries));
    InternetSetOptionA(session, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    InternetSetOptionA(session, INTERNET_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
    InternetSetOptionA(session, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
}
// Do not return a successful partial body after a read failure or shutdown.
template<class Append>
bool ReadBody(HINTERNET request, Append append, std::size_t maxBody=64u*1024u*1024u,
              Deadline overall=Deadline::max()) {
    char buffer[8192];
    const auto deadline = std::min(overall,Clock::now() + std::chrono::seconds(30));
    std::size_t total = 0;
    while (Ready(deadline)) {
        DWORD count = 0;
        if (!InternetReadFile(request, buffer, sizeof(buffer), &count)) return false;
        if (!Ready(deadline)) return false;
        if (!count) return true;
        if (count > maxBody - total) return false;
        total += count;
        append(buffer, count);
    }
    return false;
}
}
