#pragma once
#include <cstdint>
#include <string_view>

namespace AlterEgo::ExternalResource {
enum class Origin { Unknown, Wiki, Render, SnowCrows, MetaBattle, Relay };
inline Origin Classify(std::string_view url) {
    if(url.size()>16384) return Origin::Unknown;
    for(unsigned char c:url) if(c<=32 || c==127 || c=='\\')return Origin::Unknown;
    auto starts=[&](std::string_view p){return url.substr(0,p.size())==p;};
    if(starts("https://wiki.guildwars2.com/"))return Origin::Wiki;
    if(starts("https://render.guildwars2.com/"))return Origin::Render;
    if(starts("https://snowcrows.com/")||starts("https://www.snowcrows.com/"))return Origin::SnowCrows;
    if(starts("https://metabattle.com/")||starts("https://www.metabattle.com/"))return Origin::MetaBattle;
    if(starts("https://pie.rocks.cc/"))return Origin::Relay;
    return Origin::Unknown;
}
inline bool ArtworkUrl(std::string_view url) {
    auto origin=Classify(url);
    return (origin==Origin::Wiki && url.substr(0,std::string_view("https://wiki.guildwars2.com/images/").size())=="https://wiki.guildwars2.com/images/") ||
           origin==Origin::Render;
}
inline bool SafeRedirect(std::string_view from,std::string_view to) {
    const auto origin=Classify(from);
    if(origin==Origin::Unknown||origin!=Classify(to))return false;
    // Artwork must stay within the artwork path policy.
    if(ArtworkUrl(from)&&!ArtworkUrl(to))return false;
    return true;
}
inline constexpr std::size_t ArtworkBytes=32u*1024u*1024u;
inline bool Dimensions(std::uint32_t w,std::uint32_t h) {
    return w && h && w<=8192 && h<=8192 && std::uint64_t(w)*h<=32u*1024u*1024u;
}
inline bool ImageHeader(std::string_view s) {
    if(s.size()<3||s.size()>ArtworkBytes)return false;
    auto b=[&](std::size_t i){return static_cast<unsigned char>(s[i]);};
    auto u16=[&](std::size_t i){return (std::uint32_t(b(i))<<8)|b(i+1);};
    auto u32=[&](std::size_t i){return (std::uint32_t(b(i))<<24)|(std::uint32_t(b(i+1))<<16)|(std::uint32_t(b(i+2))<<8)|b(i+3);};
    if(s.size()>=33 && s.substr(0,8)==std::string_view("\x89PNG\r\n\x1a\n",8))
        return u32(8)==13 && s.substr(12,4)=="IHDR" && Dimensions(u32(16),u32(20));
    if(b(0)!=0xff||b(1)!=0xd8)return false;
    std::size_t p=2;
    while(p<s.size()){
        if(b(p++)!=0xff)return false;
        while(p<s.size()&&b(p)==0xff)++p;
        if(p==s.size())return false;
        const auto m=b(p++);
        if(m==0xd9||m==0xda||m==0x00)return false;
        if(m==0x01||(m>=0xd0&&m<=0xd7))continue;
        if(s.size()-p<2)return false;
        const auto len=u16(p);
        if(len<2||len>s.size()-p)return false;
        const bool sof=(m>=0xc0&&m<=0xcf&&m!=0xc4&&m!=0xc8&&m!=0xcc);
        if(sof)return len>=8 && Dimensions(u16(p+5),u16(p+3));
        p+=len;
    }
    return false;
}
inline bool SafeCacheKey(std::string_view key){
    if(key.empty()||key.size()>120||key=="."||key=="..")return false;
    for(unsigned char c:key)if(c<32||c==127||c=='/'||c=='\\'||c==':'||c=='*'||c=='?'||c=='"'||c=='<'||c=='>'||c=='|')return false;
    return true;
}
} // namespace AlterEgo::ExternalResource
