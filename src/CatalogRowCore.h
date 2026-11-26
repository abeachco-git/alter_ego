#pragma once
#include <algorithm>
#include <cmath>
namespace AlterEgo::UI {
// ImGui 1.80 floors the cursor after ItemSize. Use an integral pitch
// consistently for drawing, hit testing, dummy rows and clipper seeks.
inline float CatalogRowPitch(float textHeight){
    return std::ceil(std::max(22.f,textHeight+6.f));
}
// Tile layout uses a single native line per grid row; spacing is explicit.
inline float CatalogGridPitch(float tileHeight,float gap){return std::floor(tileHeight+gap);}
inline float CatalogCenteredScroll(int row,float pitch,float viewportHeight){
    return std::max(0.f,(static_cast<float>(row)+.5f)*pitch-viewportHeight*.5f);
}
}
