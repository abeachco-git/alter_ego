#pragma once
// Single, thread-safe policy for background/automatic API refresh requests.
// Manual refresh actions bypass the automatic policy, but a pending retry
// still requires current view demand. Leaving the view ends manual retry consent.
#include <atomic>
#include <cstdint>
#include <array>
#include <cstddef>
#include "RefreshEngine.h"
namespace AlterEgo::AutoRefreshPolicy {
inline std::atomic<bool>& Switch() {
    static std::atomic<bool> enabled{false}; // opt-in, including before settings load
    return enabled;
}
inline void SetEnabled(bool enabled) {
    Switch().store(enabled, std::memory_order_release);
}
inline bool Enabled() {
    return Switch().load(std::memory_order_acquire);
}
enum class Dataset : uint32_t { Characters, Achievements, SkinOwnership, PublicCatalog, Clears, Vault,
    Bank, SharedInventory, Materials, Bags, Equipment, Armory, Delivery, Wallet, Guild, TradingPrices, Count };
inline constexpr std::array<const char*,static_cast<size_t>(Dataset::Count)> Labels{
    "Characters","Achievements","Skin ownership","Public catalogs","Clears","Wizard's Vault",
    "Bank","Shared inventory","Material storage","Character bags","Equipment","Legendary Armory",
    "Trading Post delivery","Wallet","Guild bank","Trading Post prices"};
inline constexpr uint32_t AllMask=(1u<<static_cast<uint32_t>(Dataset::Count))-1;
inline std::atomic<uint32_t>& Mask(){static std::atomic<uint32_t> value{AllMask};return value;}
inline uint32_t GetMask(){return Mask().load(std::memory_order_acquire);}
inline void SetMask(uint32_t value){Mask().store(value&AllMask,std::memory_order_release);}
inline bool Included(Dataset d){return (GetMask()&(1u<<static_cast<uint32_t>(d)))!=0;}
inline void SetIncluded(Dataset d,bool included){
    const auto bit=1u<<static_cast<uint32_t>(d);
    if(included)Mask().fetch_or(bit);else Mask().fetch_and(~bit);
}
inline bool Enabled(Dataset d){return Enabled()&&Included(d);}
inline bool AllowRequest(bool userInitiated,Dataset d){return userInitiated||Enabled(d);}
inline bool AllowRequest(bool userInitiated) {
    return userInitiated || Enabled();
}
// A manual click authorizes retries only while the initiating view stays open.
// Ordinary auto policy applies if the user later returns. This does not cancel
// already-dispatched work or change the domain's retry timing.
struct RetryIntent {
    bool manual=false;
    void EndDemand(){manual=false;}
    bool Allowed(bool visible,Dataset d) const {return visible&&AllowRequest(manual,d);}
};
} // namespace AlterEgo::AutoRefreshPolicy
