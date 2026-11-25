#pragma once
#include "HttpClient.h"
#include <cstddef>
namespace AlterEgo::ArenaNet {
inline constexpr const char* Origin="https://api.guildwars2.com";
std::size_t Waiting();
bool IsApiUrl(const std::string& url);
// path must be a relative /v2/ endpoint, never a user-supplied host.
// Credential is accepted only by this fixed-origin, no-redirect transport.
Skinventory::HttpResponse Get(const std::string& path,const std::string& key={},
                              const std::string& extraHeaders={});
}
