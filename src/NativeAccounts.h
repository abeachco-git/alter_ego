#pragma once
#include <string>
#include <vector>
#include <set>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <nlohmann/json.hpp>
#include "CharacterIdentityRuntime.h"
#include "CharacterPreferenceStore.h"
#include "CharacterOrderStore.h"
#include "InventoryGroupCore.h"
namespace AlterEgo::NativeAccounts {
struct LinkedAccount {
    std::string id,name,label;
    std::set<std::string> permissions,characters;
    bool rosterKnown=false;
    uint64_t generation=0;
    CharacterIdentity::Bindings bindings; // immutable queue-admission snapshot
};
struct Result {
    int status=0;
    nlohmann::json data;
    std::string error;
    bool malformed=false; // successful HTTP response rejected by bounded parser
    explicit operator bool() const {return status==200;}
};
std::vector<LinkedAccount> Linked();
// Capture the credential generation at request admission, not after queueing.
LinkedAccount FindLinked(const std::string& accountId);
bool Ready();
bool HasCharacterBinding(const std::string& account,const std::string& name);
// Read-only identity projection; no SQL, transport, or native Fast Load changes.
std::optional<CharacterIdentity::Address> ResolveCharacterAddress(const std::string& account,const std::string& name);
std::optional<CharacterIdentity::Address> ResolveCharacterAddress(const CharacterIdentity::Ref& ref);
std::optional<CharacterIdentity::Address> ResolveObservedCharacter(const std::string& name);
// Evidence-only references are portable; resolution requires uniqueness
// across the retained registry, not merely across currently visible rows.
std::optional<CharacterIdentity::Address> ResolveCharacterEvidence(const std::string& account,const CharacterIdentity::Evidence& evidence);
bool CharacterReferenceRetired(const CharacterIdentity::Ref& ref,const CharacterIdentity::Evidence& evidence);
bool CharacterAccountLinked(const std::string& account);
CharacterLoadout::Choice ExpectedLoadout(const CharacterIdentity::Ref& ref);
uint64_t LoadoutPreferenceRevision();
bool SetExpectedLoadout(const CharacterIdentity::Address& address,const CharacterLoadout::Choice& choice);
bool StageLoadoutPreferences(const CharacterLoadout::PortableChoices& records);
CharacterLoadout::PortableChoices RecoverableLoadoutPreferences(const CharacterIdentity::Address& address);
bool RecoverLoadoutPreference(const CharacterIdentity::Address& address,const CharacterLoadout::PortableChoice& record);
bool Current(const LinkedAccount& account);
// Worker-only final publication guard. Commit must not perform HTTP, call Nexus,
// acquire the credential mutex again, or wait for another worker.
bool CommitIfCurrent(const LinkedAccount& account,const std::function<bool()>& commit);
bool CommitIfCurrent(const std::vector<LinkedAccount>& accounts,const std::function<bool()>& commit);
bool CommitIfCharacterCurrent(const LinkedAccount& account,const CharacterIdentity::Address& address,const std::function<bool()>& commit);
std::optional<CharacterIdentity::Address> EnsureCharacterAddress(const LinkedAccount& account,const std::string& name);
bool VerifyCharacterAddress(const LinkedAccount& account,const CharacterIdentity::Address& address);
void RecoverCharacterAddress(const LinkedAccount& account,const CharacterIdentity::Address& address);
void PollObservedIdentity(const std::string& name,bool inventory=false);
std::vector<CharacterIdentity::RenameNotice> TakeRenameNotices();
void AdoptPendingLoginHistory();
CharacterOrder::RanksByRef CustomOrderRanks();
uint64_t CustomOrderRevision();
bool StageCustomOrder(const CharacterOrder::PortableRanks& records);
bool SaveCustomOrder(const std::string& account,const std::vector<CharacterIdentity::Address>& ordered);
bool RecordIdentityObservation(const LinkedAccount& account,const CharacterIdentity::Address& address,
    const std::string& created,const std::string& profession,const std::string& race);
Result Request(const LinkedAccount& account,const std::string& path,
               const std::vector<std::string>& permissions,const std::string& headers={});
Result Request(const std::string& accountId,const std::string& path,
               const std::vector<std::string>& permissions);
bool RefreshRoster(const LinkedAccount& account,std::set<std::string>& names,bool verifyAll=false);
struct PhysicalEquipmentResult {Result response;int64_t started=0,success=0;bool inventoryCommitted=false;};
PhysicalEquipmentResult RequestPhysicalEquipment(const LinkedAccount& account,const CharacterIdentity::Address& address,bool manual);
void PollItemDemand(const std::string& selectedAccount);
struct Location {std::string account,scope,character,position,binding;int64_t count=0;};
std::vector<Location> Locations(uint32_t id);
void Initialize(const std::string& root);
void Shutdown();
void RenderOptions();
bool HasLinkedAccounts();
std::string VerifiedAccountForCharacter(const std::string& character);
struct CharacterAccessInspection {
    bool linked=false,verified=false,characters=false,builds=false,inventories=false;
    uint64_t generation=0;
    std::string label;
};
CharacterAccessInspection InspectCharacterAccess(const std::string& account,const std::string& character);
uint64_t InventoryGeneration();
int64_t LooseCount(uint32_t id,const std::string& account,const std::string& character={},int64_t stat=-1);
void RenderItemAvailability(uint32_t id,const std::string& account,const std::string& character,int64_t stat=-1);
void RevealItem(uint32_t id);
std::set<uint32_t> WalletPins();
void SetWalletPins(const std::set<uint32_t>& ids);
struct InventoryNavigation {
    int subview=0,sort=0,freshness=0,minQuantity=0,transfer=0,pinFilter=0,balanceFilter=0,guildSort=0;
    uint32_t item=0;
    bool currency=false,grouped=false;
    std::string query,walletQuery,guildQuery,scope,group;
    std::set<std::string> binding,rarity,type;
    NativeInventory::CharacterSelections characters;
};
InventoryNavigation CaptureInventoryNavigation();
void RestoreInventoryNavigation(const InventoryNavigation& state);
int InventorySubview();
void SetInventorySubview(int value);
bool InventoryGrouped();
void SetInventoryGrouped(bool value);
std::set<std::string> InventoryExpandedGroups();
void SetInventoryExpandedGroups(const std::set<std::string>& value);
std::set<std::string> GuildExpandedGroups();
void SetGuildExpandedGroups(const std::set<std::string>& value);
std::string RevealAccount(uint32_t id,const std::string& selectedAccount);
void RenderInventory(const std::string& selectedAccount,const std::function<void()>& saveSettings,float& listWidth);
void RenderInventoryStatusRow(const std::string& selectedAccount);
}
