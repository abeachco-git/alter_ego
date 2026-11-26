#pragma once
// External data is never trusted just because it is JSON or uses HTTPS.
#include <nlohmann/json.hpp>
#include <cstddef>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace AlterEgo::Untrusted {
using Json = nlohmann::json;
struct Limits {
    std::size_t bytes, events, stringBytes;
    int depth;
};
inline constexpr Limits Clipboard{4u*1024u*1024u, 250000, 1024u*1024u, 48};
inline constexpr Limits Library{48u*1024u*1024u, 4000000, 4u*1024u*1024u, 64};
// Public catalog batches and full catalogs share the transport's 64 MiB ceiling.
inline constexpr Limits PublicApi{64u*1024u*1024u, 6000000, 4u*1024u*1024u, 64};
inline constexpr Limits Wiki{4u*1024u*1024u, 500000, 4u*1024u*1024u, 48};

inline Json Parse(std::string_view input, Limits limits) {
    if (input.empty() || input.size() > limits.bytes)
        throw std::runtime_error("JSON payload exceeds size limit or is empty");
    // nlohmann accepts a NUL as an end-of-input marker in some input adapters.
    // Escaped NUL inside a JSON string remains valid data.
    if (input.find('\0') != std::string_view::npos)
        throw std::runtime_error("Unexpected NUL in JSON input");
    std::size_t events=0;
    std::vector<std::set<std::string>> keys;
    auto callback=[&](int depth, Json::parse_event_t event, Json& value) {
        if(depth>limits.depth || ++events>limits.events)
            throw std::runtime_error("JSON complexity limit exceeded");
        using E=Json::parse_event_t;
        if((event==E::key || event==E::value) && value.is_string() &&
           value.get_ref<const std::string&>().size()>limits.stringBytes)
            throw std::runtime_error("JSON string exceeds size limit");
        if(event==E::object_start) keys.emplace_back();
        else if(event==E::object_end) keys.pop_back();
        else if(event==E::key && !keys.back().insert(value.get_ref<const std::string&>()).second)
            throw std::runtime_error("Duplicate JSON object key");
        return true;
    };
    return Json::parse(input.begin(),input.end(),callback);
}
// Validate known visible fields without rewriting opaque extension data.
inline bool DisplayText(std::string_view value,bool multiline=false,std::size_t maxBytes=4096) {
    if(value.size()>maxBytes)return false;
    for(unsigned char c:value)
        if((c<32 && !(multiline&&(c=='\n'||c=='\r'||c=='\t'))) || c==127)return false;
    return true;
}
inline bool TextField(const Json& obj,const char* key,bool multiline=false,std::size_t maxBytes=4096){
    auto it=obj.find(key);
    return it==obj.end() || (it->is_string() && DisplayText(it->get_ref<const std::string&>(),multiline,maxBytes));
}
inline bool LoadoutMetadata(const Json& obj){
    if(!obj.is_object())return false;
    for(const auto* key:{"id","name","source_character","source_url","chat_link","ae2_code","game_mode"})
        if(!TextField(obj,key))return false;
    if(!TextField(obj,"notes",true,256u*1024u)||!TextField(obj,"import_warning",true,256u*1024u))return false;
    if(obj.contains("game_modes")){
        const auto& modes=obj.at("game_modes");
        if(!modes.is_array()||modes.size()>32)return false;
        for(const auto& mode:modes)if(!mode.is_string()||!DisplayText(mode.get_ref<const std::string&>()))return false;
    }
    return true;
}
inline Json ParseClipboard(const char* text) {
    if(!text) throw std::runtime_error("Clipboard is empty");
    std::size_t size=0;
    while(size<Clipboard.bytes && text[size]) ++size;
    if(size==Clipboard.bytes && text[size])
        throw std::runtime_error("Clipboard JSON exceeds size limit");
    return Parse(std::string_view(text,size),Clipboard);
}
} // namespace AlterEgo::Untrusted
