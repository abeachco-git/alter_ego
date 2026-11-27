#include "../src/ChatLink.h"
#include <cassert>
int case_chat_link_entities(int, char**) {
    using namespace AlterEgo;
    assert(ChatLink::EncodeEntity(LINK_SKILL,5491)=="[&BnMVAAA=]");
    assert(ChatLink::EncodeEntity(LINK_TRAIT,743)=="[&B+cCAAA=]");
    assert(ChatLink::EncodeEntity(LINK_OUTFIT,4)=="[&CwQAAAA=]");
    assert(ChatLink::EncodeEntity(LINK_RECIPE,2)=="[&CQIAAAA=]");
    assert(ChatLink::DetectType(ChatLink::EncodeEntity(LINK_MAP,123))==LINK_MAP);
    assert(ChatLink::DetectType(ChatLink::EncodeEntity(LINK_SKIN,123))==LINK_SKIN);
    for(auto type:{LINK_ITEM,LINK_SKILL,LINK_TRAIT,LINK_SKIN,LINK_MAP,LINK_RECIPE,LINK_OUTFIT}){
        assert(ChatLink::EncodeEntity(type,0).empty());
        assert(ChatLink::EncodeEntity(type,0x1000000u).empty());
        assert(!ChatLink::EncodeEntity(type,0xFFFFFFu).empty());
    }
    for(auto type:{LINK_ACHIEVEMENT,LINK_BUILD,LINK_WARDROBE_TMPL,LINK_USER,LINK_COIN})
        assert(ChatLink::EncodeEntity(type,1).empty());
    assert(ChatLink::EncodeEntity(LINK_SKILL,1,2).empty());
    assert(ChatLink::EncodeEntity(LINK_ITEM,1,0x1000000u).empty());
    assert(ChatLink::EncodeEntity(LINK_ITEM,1,0,0x1000000u).empty());
    DecodedItemLink item;
    assert(ChatLink::DecodeItem(ChatLink::EncodeEntity(LINK_ITEM,12345,55,67,89),item));
    assert(item.item_id==12345&&item.skin_id==55&&item.upgrade1_id==67&&item.upgrade2_id==89);
    return 0;
}
