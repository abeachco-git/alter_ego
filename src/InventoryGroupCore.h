#pragma once
#include "GroupBrowseCore.h"
#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>
namespace AlterEgo::NativeInventory {
struct GroupPath {
    std::string account,character,storage;
    bool operator<(const GroupPath& o) const {
        return std::tie(account,character,storage)<std::tie(o.account,o.character,o.storage);
    }
};
// Length-prefix each part; names containing separators cannot collide.
inline std::string GroupKey(const GroupPath& p){
    return GroupBrowse::PathKey({p.account,p.character,p.storage});
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
        if(!path.character.empty())nodes=&Ensure(*nodes,{path.account,path.character,{}},path.character,true).children;
        auto& branch=Ensure(*nodes,path,StorageGroupLabel(path.storage),false);
        branch.leaves=std::move(leaves);
    }
    bool Expand(const std::string& key,std::set<std::string>& expanded)const{return ExpandNodes(roots_,key,expanded);}
    std::vector<GroupLine> Flatten(const std::set<std::string>& expanded,bool force=false)const{
        std::vector<GroupLine> out;FlattenNodes(roots_,expanded,force,0,out);return out;
    }
};
}
