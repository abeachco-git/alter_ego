#pragma once
#include <string>
#include "ClearsRotation.h"
namespace AlterEgo {
inline std::string StrikeDisplayName(const std::string& canonical) {
    const auto key = ClearsRotation::Normalize(canonical);
    if (key == "voiceofthefallenandclawofthefallen" ||
        key == "voiceandclawofthefallen" ||
        key == "voiceandclaw" || key == "bears" ||
        key == "voiceofthefallenclawofthefallen")
        return "Bears";
    if (key == "guardiansglade" || key == "kela") return "Kela";
    if (key == "fraenirofjormag" || key == "fraenir") return "Fraenir";
    if (key == "whisperofjormag" || key == "whisper") return "Whisper";
    return canonical;
}
} // namespace AlterEgo
