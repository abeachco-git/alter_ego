#pragma once
#include <string>
#include <vector>
#include <set>
#include <cstdint>
#include <functional>
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
uint64_t InventoryGeneration();
int64_t LooseCount(uint32_t id,const std::string& account,const std::string& character={},int64_t stat=-1);
void RenderItemAvailability(uint32_t id,const std::string& account,const std::string& character,int64_t stat=-1);
void RevealItem(uint32_t id);
std::set<uint32_t> WalletFavorites();
void SetWalletFavorites(const std::set<uint32_t>& ids);
struct InventoryNavigation {
    int subview=0,sort=0,walletSort=0,freshness=0,minQuantity=0,transfer=0,favoriteFilter=0,balanceFilter=0;
    uint32_t item=0;
    std::string query,walletQuery,guildQuery,scope;
    std::set<std::string> binding,rarity,type,characters;
};
InventoryNavigation CaptureInventoryNavigation();
void RestoreInventoryNavigation(const InventoryNavigation& state);
int InventorySubview();
void SetInventorySubview(int value);
std::string RevealAccount(uint32_t id,const std::string& selectedAccount);
void RenderInventory(const std::string& selectedAccount,const std::function<void()>& saveSettings);
void RenderInventoryStatusRow(const std::string& selectedAccount);
}
