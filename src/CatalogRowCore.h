#pragma once
#include <algorithm>
#include <cmath>
namespace AlterEgo::UI {
// ImGui 1.80 floors the cursor after ItemSize. Use an integral pitch
// consistently for drawing, hit testing, dummy rows and clipper seeks.
inline float CatalogRowPitch(float textHeight){
    return std::ceil(std::max(22.f,textHeight+6.f));
}
}
