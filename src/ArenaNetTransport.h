#pragma once
#include "HttpClient.h"
namespace AlterEgo::ArenaNet {
inline constexpr const char* Origin="https://api.guildwars2.com";
bool IsApiUrl(const std::string& url);
// path must be a relative /v2/ endpoint, never a user-supplied host.
// Credential is accepted only by this fixed-origin, no-redirect transport.
Skinventory::HttpResponse Get(const std::string& path,const std::string& key={},
                              const std::string& extraHeaders={});
}
