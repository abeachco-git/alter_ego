#pragma once
#include <string>
#include <cstdint>
#include <cstddef>
namespace AlterEgo::GameMemory {
// Fingerprint complete inspected function bodies, not just their prologues.
// This is a structural compatibility check, not cryptographic authentication.
inline uint64_t CodeFingerprint(const unsigned char* bytes,std::size_t size) {
    uint64_t hash=14695981039346656037ull;
    for(std::size_t i=0;i<size;++i) hash=(hash^bytes[i])*1099511628211ull;
    return hash;
}
inline bool PlausibleCharacter(unsigned level,unsigned profession,unsigned race,unsigned gender) {
    return level>=1 && level<=80 && profession>=1 && profession<=9 && race<=4 && gender<=1;
}
// Names are globally unique in GW2; the validated live roster is required
// separately. A known conflicting cached account is still a reason to block.
inline bool CompatibleAccount(const std::string& current,const std::string& target) {
    return current.empty() || target.empty() || current==target;
}
inline bool EligibleTarget(bool gameplay,bool pending,bool activePresent,bool targetPresent,
                           const std::string& active,const std::string& target,
                           const std::string& currentAccount,const std::string& targetAccount) {
    return gameplay && !pending && activePresent && targetPresent && !active.empty() &&
           !target.empty() && active!=target && CompatibleAccount(currentAccount,targetAccount);
}
} // namespace AlterEgo::GameMemory
