#pragma once
#include <cstdint>
#include <string>
#include <set>
#include <vector>

namespace AlterEgo::Identity {
struct Account {
    std::string name;
    bool Known() const { return !name.empty(); }
    bool operator==(const Account& other) const { return name==other.name; }
};
struct Character {
    Account account;
    std::string name;
    bool operator==(const Character& other) const { return account==other.account && name==other.name; }
};
inline bool MatchesReply(const Account& expected,const Account& echoed,bool hasEcho,bool verifiedSingle) {
    return expected.Known() && (hasEcho ? expected==echoed : verifiedSingle);
}
// Exact returned identity set, not the response count alone. Reject duplicates,
// missing and foreign IDs before mutating any visible state.
inline bool ExactIds(const std::vector<uint32_t>& expected,const std::vector<uint32_t>& returned) {
    if(expected.size()!=returned.size())return false;
    std::set<uint32_t> a(expected.begin(),expected.end()), b(returned.begin(),returned.end());
    return a.size()==expected.size() && b.size()==returned.size() && a==b && !a.count(0);
}
}
