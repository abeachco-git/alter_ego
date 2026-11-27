#pragma once
#include "NativeInventoryStore.h"
#include "RefreshEngine.h"
#include "BackgroundWorkers.h"
#include "ArenaNetTransport.h"
#include "UntrustedInput.h"
namespace AlterEgo::NativeInventory {
// Currency definitions are a single public catalog, not per-account wallet
// data. Existing wallet scope cadence controls when missing definitions are
// demanded; do not add an independent auto refresh or change that cadence.
inline constexpr const char* CurrencyDataset="currency-definitions";
template<class Send>
inline void RefreshCurrencyDefinitionsWith(const std::string& root,const std::set<uint32_t>& ids,Send&& send){
    if(ids.empty()||!MissingCurrencyDefinitions(root,ids))return;
    auto claim=RefreshEngine::ClaimIds(CurrencyDataset,{1},true);
    if(!claim->ids.empty()){
        // Recheck after admission in case a competing worker completed
        // between the original missing check and this claim.
        if(MissingCurrencyDefinitions(root,ids)){
            Background::Checkpoint();
            const auto stamp=Cache::ReferenceSnapshotTime();
            auto reply=send("/v2/currencies?ids=all");
            Background::Checkpoint();
            if(reply.status_code!=200)throw std::runtime_error("Currency definitions unavailable");
            auto rows=Untrusted::Parse(reply.body,Untrusted::PublicApi);
            StoreCurrencies(root,rows,stamp);
        }
    }
    claim.reset();
    (void)RefreshEngine::WaitForIds(CurrencyDataset,{1},[]{return Background::Workers().Stopping();});
    Background::Checkpoint();
}
inline void RefreshCurrencyDefinitions(const std::string& root,const std::set<uint32_t>& ids){
    RefreshCurrencyDefinitionsWith(root,ids,[](const std::string& path){return ArenaNet::Get(path);});
}
} // namespace AlterEgo::NativeInventory
