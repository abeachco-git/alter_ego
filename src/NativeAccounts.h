#pragma once
#include <string>
#include <vector>
#include <set>
#include <cstdint>
#include <nlohmann/json.hpp>
namespace AlterEgo::NativeAccounts {
struct LinkedAccount {
    std::string id,name,label;
    std::set<std::string> permissions,characters;
    bool rosterKnown=false;
    uint64_t generation=0;
};
struct Result {
    int status=0;
    nlohmann::json data;
    std::string error;
    explicit operator bool() const {return status==200;}
};
std::vector<LinkedAccount> Linked();
bool Ready();
bool Current(const LinkedAccount& account);
Result Request(const LinkedAccount& account,const std::string& path,
               const std::vector<std::string>& permissions);
Result Request(const std::string& accountId,const std::string& path,
               const std::vector<std::string>& permissions);
bool RefreshRoster(const LinkedAccount& account,std::set<std::string>& names);
struct Location {std::string account,scope,character,position,binding;int64_t count=0;};
std::vector<Location> Locations(uint32_t id);
void Initialize(const std::string& root);
void Shutdown();
void RenderOptions();
bool HasLinkedAccounts();
std::string VerifiedAccountForCharacter(const std::string& character);
void RenderInventory(const std::string& activeCharacter);
}
