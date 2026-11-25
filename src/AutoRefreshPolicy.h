#pragma once
// Single, thread-safe policy for background/automatic API refresh requests.
// Manual refresh actions and the completion/retry phases of an already-started
// manual request remain independent of this option.
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
    Bank, SharedInventory, Materials, Bags, Equipment, Armory, Delivery, Wallet, Guild, Count };
inline constexpr std::array<const char*,static_cast<size_t>(Dataset::Count)> Labels{
    "Characters","Achievements","Skin ownership","Public catalogs","Clears","Wizard's Vault",
    "Bank","Shared inventory","Material storage","Character bags","Equipment","Legendary Armory",
    "Trading Post delivery","Wallet","Guild bank"};
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
} // namespace AlterEgo::AutoRefreshPolicy
