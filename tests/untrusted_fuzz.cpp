// Deterministic adversarial corpus with ASan/UBSan; no Nexus/Windows dependencies.
#include "../src/UntrustedInput.h"
#include "../src/ExternalResourceSafety.h"
#include "../src/BackupZip.h"
#include "../src/ChatLink.h"
#include <random>
#include <string>
#include <vector>
int main(){
    namespace U=AlterEgo::Untrusted;
    namespace R=AlterEgo::ExternalResource;
    std::mt19937 rng(0xAE2026u);
    const std::vector<std::string> seeds={
        R"({"name":"test","nested":[{"x":1}]})",
        R"({"name":"test","name":"duplicate"})",
        "[&DQEeHjElPy5LFwAAhgAAAEgBAACGAAAALRcAAAAAAAAAAAAAAAAAAAAAAAA=]",
        "https://wiki.guildwars2.com/images/icon.png",
        std::string("\x89PNG\r\n\x1a\n",8),std::string("PK\x03\x04",4)
    };
    for(unsigned i=0;i<12000;++i){
        std::string data=seeds[rng()%seeds.size()];
        const auto n=rng()%32;
        for(unsigned j=0;j<n;++j){
            if(data.empty()||rng()%3==0)data.insert(data.begin()+static_cast<std::ptrdiff_t>(rng()%(data.size()+1)),static_cast<char>(rng()%256));
            else if(rng()%2)data[rng()%data.size()]=static_cast<char>(rng()%256);
            else data.erase(rng()%data.size(),1);
        }
        try{(void)U::Parse(data,U::Clipboard);}catch(const std::exception&){}
        (void)R::ImageHeader(data);(void)R::SafeCacheKey(data);(void)R::Classify(data);
        (void)AlterEgo::ChatLink::Base64Decode(data);
        AlterEgo::DecodedBuildLink build{}; (void)AlterEgo::ChatLink::DecodeBuild(data,build);
        AlterEgo::DecodedWardrobeLink wardrobe{}; (void)AlterEgo::ChatLink::DecodeWardrobe(data,wardrobe);
        AlterEgo::BackupZip::Entries entries;std::string error;
        (void)AlterEgo::BackupZip::ReadStored(data,entries,error);
    }
}
