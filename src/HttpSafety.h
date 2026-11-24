#pragma once
#include "BackgroundWorkers.h"
#include <windows.h>
#include <wininet.h>

namespace AlterEgo::HttpSafety {
inline void Configure(HINTERNET session) {
    DWORD timeout = 12000;
    InternetSetOptionA(session, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    InternetSetOptionA(session, INTERNET_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
    InternetSetOptionA(session, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
}
// Do not return a successful partial body after a read failure or shutdown.
template<class Append>
bool ReadBody(HINTERNET request, Append append) {
    char buffer[8192];
    while (!Background::Workers().Stopping()) {
        DWORD count = 0;
        if (!InternetReadFile(request, buffer, sizeof(buffer), &count)) return false;
        if (!count) return !Background::Workers().Stopping();
        append(buffer, count);
    }
    return false;
}
}
