#pragma once
#include "BackgroundWorkers.h"
#include <windows.h>
#include <wininet.h>
#include <chrono>
#include <cstddef>

namespace AlterEgo::HttpSafety {
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
bool ReadBody(HINTERNET request, Append append) {
    char buffer[8192];
    using Clock = std::chrono::steady_clock;
    const auto deadline = Clock::now() + std::chrono::seconds(30);
    constexpr std::size_t maxBody = 64u * 1024u * 1024u;
    std::size_t total = 0;
    while (!Background::Workers().Stopping() && Clock::now() < deadline) {
        DWORD count = 0;
        if (!InternetReadFile(request, buffer, sizeof(buffer), &count)) return false;
        if (Background::Workers().Stopping() || Clock::now() >= deadline) return false;
        if (!count) return true;
        if (count > maxBody - total) return false;
        total += count;
        append(buffer, count);
    }
    return false;
}
}
