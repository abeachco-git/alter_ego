#pragma once
#include <cstdint>
#include <limits>
#include <string>

namespace AlterEgo::ArtworkRequest {
// Preserve existing disk and Nexus texture identities. A legend uses the
// ordinary swap skill ID, never a template legend code.
inline constexpr uint32_t SkillKey(uint32_t id) {
    return id && id <= (std::numeric_limits<uint32_t>::max)() - 5000000u
        ? id + 5000000u : 0u;
}
inline constexpr uint32_t PetKey(uint32_t id) {
    return id && id <= 255u ? id + 9400000u : 0u;
}

// A known key can resolve offline even before its metadata URL arrives.
// Request owns admission, validation, retries, publication and deduplication.
template<class Lookup, class Request>
auto Resolve(uint32_t key, const std::string& url, bool priority,
             Lookup lookup, Request request) -> decltype(lookup(key)) {
    if (!key) return {};
    auto texture = lookup(key);
    if (!texture && !url.empty()) request(key, url, priority);
    return texture;
}
} // namespace AlterEgo::ArtworkRequest
