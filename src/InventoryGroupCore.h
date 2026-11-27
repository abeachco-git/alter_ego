#pragma once
#include "GroupBrowseCore.h"
#include "CharacterSelectionCore.h"
#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>
namespace AlterEgo::NativeInventory {
using CharacterSelections=std::map<CharacterIdentity::Ref,CharacterIdentity::Selection>;
// A restricted selection with zero resolved addresses matches nothing,
// never All. This same projection drives listing, status and manual refresh.
struct CharacterFilter {
    using Key=std::pair<std::string,std::string>;
    bool restricted=false;
    std::map<Key,CharacterIdentity::Address> addresses;
    bool Matches(const std::string& account,const std::string& name)const{
        return !restricted||addresses.count({account,name})!=0;
    }
    bool Accepts(const CharacterIdentity::Address& address)const{
        if(!restricted)return true;
        const auto it=addresses.find({address.account,address.name});
        return it!=addresses.end()&&CharacterIdentity::SameAddress(it->second,address);
    }
    bool empty()const{return !restricted;}
};
inline CharacterFilter ProjectCharacters(CharacterSelections& selections,
    const std::map<std::string,CharacterIdentity::Bindings>& bindings,uint64_t epoch){
    CharacterFilter out;out.restricted=!selections.empty();
    for(auto& [ref,selection]:selections){
        const auto account=bindings.find(ref.account);
        if(account==bindings.end())continue;
        for(const auto& [name,b]:account->second){
            CharacterIdentity::Address address{ref.account,name,b,epoch};
            if(selection.Matches(address)&&selection.Accept(address)){
                out.addresses.emplace(CharacterFilter::Key{ref.account,name},address);break;
            }
        }
    }
    return out;
}
struct GroupPath {
    std::string account,character,storage;
    std::string identity{}; // stable evidence token, not a display name
    bool operator<(const GroupPath& o) const {
        return std::tie(account,character,storage,identity)<std::tie(o.account,o.character,o.storage,o.identity);
    }
};
// Length-prefix each part; names containing separators cannot collide.
inline std::string GroupKey(const GroupPath& p){
    if(!p.character.empty()&&!p.identity.empty())
        return "identity:"+GroupBrowse::PathKey({p.account,p.identity,p.storage});
    return GroupBrowse::PathKey({p.account,p.character,p.storage});
}
inline std::string GroupIdentity(const CharacterIdentity::Binding& b){
    return GroupBrowse::PathKey({std::to_string(b.id),b.evidence.created,b.evidence.profession,b.evidence.race});
}
inline int StorageRank(const std::string& s){
    const std::vector<std::string> order={"Bank","Shared inventory","Materials","Legendary Armory","Delivery box","Wallet","Equipment","Bags"};
    auto it=std::find(order.begin(),order.end(),s);
    return static_cast<int>(it-order.begin());
}
inline std::string StorageGroupLabel(const std::string& s){
    if(s=="Materials")return "Material Storage";
    if(s=="Shared inventory")return "Shared Inventory";
    if(s=="Delivery box")return "Trading Post Delivery";
    return s;
}
struct GroupLeaf {size_t item=0;int64_t count=0;};
struct GroupNode {
    std::string key,label,account,character;
    std::vector<GroupNode> children;
    std::vector<GroupLeaf> leaves;
};
struct GroupLine {const GroupNode* node=nullptr;const GroupLeaf* leaf=nullptr;int depth=0;};
class GroupTree {
    std::vector<GroupNode> roots_;
    static GroupNode& Ensure(std::vector<GroupNode>& nodes,const GroupPath& path,const std::string& label,bool character){
        auto key=GroupKey(path);
        auto it=std::find_if(nodes.begin(),nodes.end(),[&](const auto& n){return n.key==key;});
        if(it!=nodes.end())return *it;
        nodes.push_back({key,label,path.account,character?path.character:std::string{}, {}, {}});
        return nodes.back();
    }
    static void FlattenNodes(const std::vector<GroupNode>& nodes,const std::set<std::string>& expanded,bool force,int depth,std::vector<GroupLine>& out){
        for(const auto& n:nodes){
            out.push_back({&n,nullptr,depth});
            if(!GroupBrowse::Expanded(expanded,n.key,false,force))continue;
            FlattenNodes(n.children,expanded,force,depth+1,out);
            for(const auto& leaf:n.leaves)out.push_back({&n,&leaf,depth+1});
        }
    }
    static bool ExpandNodes(const std::vector<GroupNode>& nodes,const std::string& key,std::set<std::string>& expanded){
        for(const auto& n:nodes)if(n.key==key||ExpandNodes(n.children,key,expanded)){
            GroupBrowse::Reveal(expanded,n.key,false);return true;
        }
        return false;
    }
public:
    void Clear(){roots_.clear();}
    // Caller supplies buckets in logical sibling order, and leaves in the chosen sort.
    void Add(const GroupPath& path,const std::string& accountLabel,bool accountParent,std::vector<GroupLeaf> leaves){
        if(leaves.empty())return;
        auto* nodes=&roots_;
        if(accountParent)nodes=&Ensure(*nodes,{path.account,{},{}},accountLabel,false).children;
        if(!path.character.empty())nodes=&Ensure(*nodes,{path.account,path.character,{},path.identity},path.character,true).children;
        auto& branch=Ensure(*nodes,path,StorageGroupLabel(path.storage),false);
        branch.leaves=std::move(leaves);
    }
    bool Expand(const std::string& key,std::set<std::string>& expanded)const{return ExpandNodes(roots_,key,expanded);}
    std::vector<GroupLine> Flatten(const std::set<std::string>& expanded,bool force=false)const{
        std::vector<GroupLine> out;FlattenNodes(roots_,expanded,force,0,out);return out;
    }
};
}
