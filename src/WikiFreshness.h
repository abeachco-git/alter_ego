#pragma once
#include <cstdint>
#include <string>
namespace AlterEgo::WikiFreshness {
inline constexpr int64_t SpecialInterval=6*3600, SpecialRetry=60*60;
inline constexpr int64_t SkinInterval=30LL*86400, SkinRetry=86400;
struct Stamp {int64_t success=0,attempt=0;};
inline bool Due(Stamp s,int64_t now,int64_t interval,int64_t retry) {
    return (!s.success||s.success>now||now-s.success>=interval) &&
           (!s.attempt||s.attempt>now||now-s.attempt>=retry);
}
inline bool ValidSpecialBody(const std::string& body) {
    return !body.empty() && body.size()<=4*1024*1024 &&
           body.find("== Active events ==")!=std::string::npos;
}
inline bool DateMatches(int start,int end,int today) {
    return start>0&&end>=start&&today>=start&&today<=end;
}
}
