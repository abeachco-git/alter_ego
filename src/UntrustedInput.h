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
inline Json ParseClipboard(const char* text) {
    if(!text) throw std::runtime_error("Clipboard is empty");
    std::size_t size=0;
    while(size<Clipboard.bytes && text[size]) ++size;
    if(size==Clipboard.bytes && text[size])
        throw std::runtime_error("Clipboard JSON exceeds size limit");
    return Parse(std::string_view(text,size),Clipboard);
}
} // namespace AlterEgo::Untrusted
