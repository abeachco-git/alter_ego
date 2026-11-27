#pragma once
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace AlterEgo::ArtworkRequest {
// Logical artwork IDs are not version identities. A legend uses the ordinary
// swap skill ID, never a template legend code. Skins have their own namespace.
inline constexpr uint32_t SkillKey(uint32_t id) {
    return id && id <= (std::numeric_limits<uint32_t>::max)() - 5000000u
        ? id + 5000000u : 0u;
}
inline constexpr uint32_t PetKey(uint32_t id) {
    return id && id <= 255u ? id + 9400000u : 0u;
}

inline constexpr uint32_t SkinKey(uint32_t id){
    return id&&id<=0xFFFFFFu?0x20000000u|id:0u;
}
inline bool DigestValid(std::string_view digest){
    if(digest.size()!=64)return false;
    for(char c:digest)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;
    return true;
}
inline std::string VersionKey(uint32_t id,std::string_view digest){
    return id&&DigestValid(digest)?std::to_string(id)+"_"+std::string(digest):std::string{};
}
inline uint32_t VersionOwner(std::string_view key){
    const auto split=key.find('_');
    if(split==std::string_view::npos||split==0||split>10||key.front()=='0'||!DigestValid(key.substr(split+1)))return 0;
    uint64_t id=0;
    for(char c:key.substr(0,split)){if(c<'0'||c>'9')return 0;id=id*10+static_cast<unsigned>(c-'0');}
    return id&&id<=UINT32_MAX?static_cast<uint32_t>(id):0;
}
inline std::string OfflineKey(uint32_t id,const std::string& last){
    if(!id)return {};
    return VersionOwner(last)==id?last:std::to_string(id);
}
// A known key can resolve offline even before its metadata URL arrives.
// A supplied URL is part of lookup identity, not just a download argument.
// Request owns admission, validation, retries, publication and deduplication.
template<class Lookup, class Request>
auto Resolve(uint32_t key, const std::string& url, bool priority,
             Lookup lookup, Request request) -> decltype(lookup(key,url)) {
    if (!key) return {};
    auto texture = lookup(key,url);
    if (!texture && !url.empty()) request(key, url, priority);
    return texture;
}
} // namespace AlterEgo::ArtworkRequest
